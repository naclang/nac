#include "server.h"
#include <stddef.h>

#ifndef _WIN32

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../runtime/eval.h"
#include "../util/error.h"
#include "server_common.h"

#define REQ_BUFFER_SIZE 65536
#define RESP_BUFFER_SIZE (MAX_STRING_LEN + 8192)
#define MAX_CONCURRENT_CONNECTIONS 64
#define MAX_HANDLER_NAME_LEN 256

/* The NaC interpreter keeps its call stack, variables, and error state in
 * plain global C variables (see eval.c / vartable.c) -- it was never
 * designed to be reentrant. serve() now handles many connections at once
 * (one thread per connection), so slow clients / slow network I/O no
 * longer block every other request. But the actual call into the NaC
 * handler function still touches that shared global state, so it stays
 * serialized behind this mutex. Everything else -- accepting connections,
 * reading the request, parsing HTTP, building the response, writing it
 * back -- runs fully in parallel across connections. */
static pthread_mutex_t g_nac_exec_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Caps how many connections are being handled at once, so a burst of
 * traffic can't spawn unbounded threads. accept() simply waits for room
 * once the cap is hit. */
static pthread_mutex_t g_conn_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_conn_count_cond = PTHREAD_COND_INITIALIZER;
static int g_active_connections = 0;

typedef struct {
    int client_fd;
    char client_ip[64];
    char handler_name[MAX_HANDLER_NAME_LEN];
} ConnArgs;

static void release_connection_slot(void) {
    pthread_mutex_lock(&g_conn_count_mutex);
    g_active_connections--;
    pthread_cond_signal(&g_conn_count_cond);
    pthread_mutex_unlock(&g_conn_count_mutex);
}

static void lock_exec(void) { pthread_mutex_lock(&g_nac_exec_mutex); }
static void unlock_exec(void) { pthread_mutex_unlock(&g_nac_exec_mutex); }

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

/* recv() with a total-request deadline. Returns the byte count, 0 on
 * orderly close, -1 on error, -2 on timeout. */
static int recv_deadline(int fd, char *buf, int len, long deadline) {
    for (;;) {
        long remain = deadline - now_ms();
        if (remain <= 0) return -2;

        struct timeval tv;
        tv.tv_sec = (time_t)(remain / 1000);
        tv.tv_usec = (suseconds_t)((remain % 1000) * 1000);

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        int ready = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ready == 0) return -2;

        ssize_t n = recv(fd, buf, (size_t)len, 0);
        if (n < 0 && errno == EINTR) continue;
        return (int)n;
    }
}

static int send_all(int fd, const char *buf, int len) {
    int sent_total = 0;
    while (sent_total < len) {
        ssize_t sent = send(fd, buf + sent_total, (size_t)(len - sent_total), 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (sent == 0) return 0;
        sent_total += (int)sent;
    }
    return 1;
}

static void send_http_error(int fd, int status, const char *message) {
    char out[1024];
    int n = nac_http_build_error_response(status, message, out, sizeof(out));
    if (n > 0) send_all(fd, out, n);
}

static void *handle_connection(void *arg) {
    ConnArgs *ca = (ConnArgs*)arg;
    int client_fd = ca->client_fd;
    char handler_name[MAX_HANDLER_NAME_LEN];
    char client_ip[64];
    strncpy(handler_name, ca->handler_name, sizeof(handler_name) - 1);
    handler_name[sizeof(handler_name) - 1] = '\0';
    strncpy(client_ip, ca->client_ip, sizeof(client_ip) - 1);
    client_ip[sizeof(client_ip) - 1] = '\0';
    free(ca);

    /* Each thread gets its own request/response buffers -- these must not
     * be shared across connections handled concurrently. */
    char *raw = (char*)malloc(REQ_BUFFER_SIZE);
    char *resp = (char*)malloc(RESP_BUFFER_SIZE);
    if (!raw || !resp) {
        free(raw);
        free(resp);
        close(client_fd);
        release_connection_slot();
        return NULL;
    }

    /* Socket-level fallback timeouts (the read loop also enforces one
     * deadline for the whole request, so slow trickling clients cannot
     * keep a worker alive forever). */
    struct timeval timeout;
    timeout.tv_sec = NAC_READ_TIMEOUT_MS / 1000;
    timeout.tv_usec = (NAC_READ_TIMEOUT_MS % 1000) * 1000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    long deadline = now_ms() + NAC_READ_TIMEOUT_MS;
    size_t total = 0;
    int header_end = -1;
    int early_status = 0;

    while (total < NAC_MAX_HEADER_BYTES) {
        int n = recv_deadline(client_fd, raw + total,
                              (int)(REQ_BUFFER_SIZE - 1 - total), deadline);
        if (n == -2) {
            early_status = 408;
            break;
        }
        if (n <= 0) {
            early_status = 400;
            break;
        }

        total += (size_t)n;
        raw[total] = '\0';

        char *found = strstr(raw, "\r\n\r\n");
        if (found) {
            header_end = (int)(found - raw) + 4;
            break;
        }
    }

    if (header_end < 0 || header_end > NAC_MAX_HEADER_BYTES) {
        int status = early_status ? early_status : 431;
        send_http_error(client_fd, status,
                        status == 408 ? "408 Request Timeout"
                                      : status == 431 ? "431 Request Header Fields Too Large"
                                                      : "400 Bad Request");
        close(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return NULL;
    }

    NacHttpRequest req;
    nac_http_parse_request(raw, header_end, &req);
    strncpy(req.client_ip, client_ip, sizeof(req.client_ip) - 1);
    req.client_ip[sizeof(req.client_ip) - 1] = '\0';

    if (req.method[0] == '\0' || req.path[0] != '/') {
        send_http_error(client_fd, 400, "400 Bad Request");
        nac_http_free_request(&req);
        close(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return NULL;
    }

    if (req.content_length < 0) {
        send_http_error(client_fd, 400, "400 Bad Request: invalid Content-Length");
        nac_http_free_request(&req);
        close(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return NULL;
    }
    if (req.content_length > NAC_MAX_BODY_BYTES) {
        send_http_error(client_fd, 413,
                        "413 Payload Too Large (request body limit is 8191 bytes)");
        nac_http_free_request(&req);
        close(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return NULL;
    }

    /* Keep reading until we have the whole body (bounded by the language's
     * fixed string size and the request deadline). */
    int want = req.content_length;
    int body_error = 0;
    while ((int)(total - header_end) < want) {
        int n = recv_deadline(client_fd, raw + total,
                              (int)(REQ_BUFFER_SIZE - 1 - total), deadline);
        if (n == -2) {
            body_error = 408;
            break;
        }
        if (n <= 0) {
            body_error = 400;
            break;
        }
        total += (size_t)n;
        raw[total] = '\0';
    }

    if (body_error) {
        send_http_error(client_fd, body_error,
                        body_error == 408 ? "408 Request Timeout" : "400 Bad Request");
        nac_http_free_request(&req);
        close(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return NULL;
    }

    int body_available = (int)total - header_end;
    if (body_available > want) body_available = want;
    if (body_available > 0) {
        int copy_len = body_available;
        if (copy_len >= MAX_STRING_LEN) copy_len = MAX_STRING_LEN - 1;
        memcpy(req.body, raw + header_end, (size_t)copy_len);
        req.body[copy_len] = '\0';
    }

    Value result;
    char *raw_resp = NULL;
    int raw_len = 0;
    int status = 0;
    int is_raw = nac_dispatch_request(&req, handler_name, lock_exec, unlock_exec,
                                      &result, &raw_resp, &raw_len, &status);

    if (is_raw) {
        send_all(client_fd, raw_resp, raw_len);
        free(raw_resp);
    } else {
        int resp_len = nac_http_build_response(1, handler_name, result, resp,
                                               RESP_BUFFER_SIZE, &status);
        if (resp_len > 0) {
            send_all(client_fd, resp, resp_len);
        } else {
            send_http_error(client_fd, 500, "500 Internal Server Error");
        }
        free_value(&result);
    }

    printf("%s %s -> %d\n", req.method, req.path, status);
    fflush(stdout);

    nac_http_free_request(&req);
    close(client_fd);
    free(raw);
    free(resp);

    release_connection_slot();
    return NULL;
}

int server_start(int port, const char *handler_name) {
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        report_error("HTTP server: could not create socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        report_error("HTTP server: bind failed (port may already be in use)");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 128) < 0) {
        report_error("HTTP server: listen failed");
        close(server_fd);
        return 1;
    }

    if (handler_name && handler_name[0]) {
        printf("NaC HTTP server listening on http://localhost:%d (handler: %s)\n", port, handler_name);
    } else {
        printf("NaC HTTP server listening on http://localhost:%d\n", port);
    }
    fflush(stdout);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        pthread_mutex_lock(&g_conn_count_mutex);
        while (g_active_connections >= MAX_CONCURRENT_CONNECTIONS) {
            pthread_cond_wait(&g_conn_count_cond, &g_conn_count_mutex);
        }
        g_active_connections++;
        pthread_mutex_unlock(&g_conn_count_mutex);

        ConnArgs *ca = (ConnArgs*)malloc(sizeof(ConnArgs));
        if (!ca) {
            close(client_fd);
            release_connection_slot();
            continue;
        }
        ca->client_fd = client_fd;
        ca->client_ip[0] = '\0';
        inet_ntop(AF_INET, &client_addr.sin_addr, ca->client_ip, sizeof(ca->client_ip));
        strncpy(ca->handler_name, handler_name ? handler_name : "", sizeof(ca->handler_name) - 1);
        ca->handler_name[sizeof(ca->handler_name) - 1] = '\0';

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_connection, ca) != 0) {
            close(client_fd);
            free(ca);
            release_connection_slot();
            continue;
        }
        pthread_detach(tid);
    }

    /* Unreachable in normal operation. */
    close(server_fd);
    return 0;
}

#endif
