#include "server.h"
#include <stddef.h>

#ifndef _WIN32

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
    char handler_name[MAX_HANDLER_NAME_LEN];
} ConnArgs;

static void release_connection_slot(void) {
    pthread_mutex_lock(&g_conn_count_mutex);
    g_active_connections--;
    pthread_cond_signal(&g_conn_count_cond);
    pthread_mutex_unlock(&g_conn_count_mutex);
}

static void *handle_connection(void *arg) {
    ConnArgs *ca = (ConnArgs*)arg;
    int client_fd = ca->client_fd;
    char handler_name[MAX_HANDLER_NAME_LEN];
    strncpy(handler_name, ca->handler_name, sizeof(handler_name) - 1);
    handler_name[sizeof(handler_name) - 1] = '\0';
    free(ca);

    /* Each thread gets its own request/response buffers -- these must not
     * be shared across connections handled concurrently. */
    char *raw = (char*)malloc(REQ_BUFFER_SIZE);
    char *resp = (char*)malloc(RESP_BUFFER_SIZE);

    size_t total = 0;
    int header_end = -1;

    while (total < REQ_BUFFER_SIZE - 1) {
        ssize_t n = recv(client_fd, raw + total, REQ_BUFFER_SIZE - 1 - total, 0);
        if (n <= 0) break;
        total += (size_t)n;
        raw[total] = '\0';
        char *found = strstr(raw, "\r\n\r\n");
        if (found) {
            header_end = (int)(found - raw) + 4;
            break;
        }
    }

    if (header_end < 0) {
        close(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return NULL;
    }

    NacHttpRequest req;
    nac_http_parse_request(raw, header_end, &req);

    /* Keep reading until we have the whole body (bounded by both the
     * socket buffer and the language's fixed string size). */
    int want = req.content_length;
    int max_body = REQ_BUFFER_SIZE - header_end - 1;
    if (want > max_body) want = max_body;

    while ((int)(total - header_end) < want) {
        ssize_t n = recv(client_fd, raw + total, REQ_BUFFER_SIZE - 1 - total, 0);
        if (n <= 0) break;
        total += (size_t)n;
        raw[total] = '\0';
    }

    int body_available = (int)total - header_end;
    if (body_available > 0) {
        int copy_len = body_available;
        if (copy_len >= MAX_STRING_LEN) copy_len = MAX_STRING_LEN - 1;
        memcpy(req.body, raw + header_end, copy_len);
        req.body[copy_len] = '\0';
    }

    Value args[5];
    args[0] = make_string(req.method);
    args[1] = make_string(req.path);
    args[2] = req.query;
    args[3] = req.headers;
    args[4] = make_string(req.body);

    Value result;
    pthread_mutex_lock(&g_nac_exec_mutex);
    int found = call_nac_function(handler_name, args, 5, &result);
    pthread_mutex_unlock(&g_nac_exec_mutex);

    int status = 0;
    int resp_len = nac_http_build_response(found, handler_name, result, resp, RESP_BUFFER_SIZE, &status);

    if (resp_len > 0) {
        ssize_t sent_total = 0;
        while (sent_total < resp_len) {
            ssize_t sent = send(client_fd, resp + sent_total, resp_len - sent_total, 0);
            if (sent <= 0) break;
            sent_total += sent;
        }
    }

    printf("%s %s -> %d\n", req.method, req.path, status);
    fflush(stdout);

    if (found) {
        free_value(&result);
    }
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

    printf("NaC HTTP server listening on http://localhost:%d (handler: %s)\n", port, handler_name);
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
        ca->client_fd = client_fd;
        strncpy(ca->handler_name, handler_name, sizeof(ca->handler_name) - 1);
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
