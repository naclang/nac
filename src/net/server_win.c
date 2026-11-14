#include "server.h"
#include <stddef.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/eval.h"
#include "../util/error.h"
#include "server_common.h"

#define REQ_BUFFER_SIZE 65536
#define RESP_BUFFER_SIZE (MAX_STRING_LEN + 8192)
#define MAX_CONCURRENT_CONNECTIONS 64
#define MAX_HANDLER_NAME_LEN 256

/* Same concurrency model as server_unix.c: one thread per connection so
 * socket I/O runs in parallel, but the actual call into the NaC handler
 * is serialized behind this critical section, since the interpreter's
 * variables/call-stack are shared global state. */
static CRITICAL_SECTION g_nac_exec_lock;
static CRITICAL_SECTION g_conn_count_lock;
static CONDITION_VARIABLE g_conn_count_cond;
static int g_active_connections = 0;
static volatile LONG g_locks_ready = 0;

static void ensure_locks_initialized(void) {
    if (InterlockedCompareExchange(&g_locks_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_nac_exec_lock);
        InitializeCriticalSection(&g_conn_count_lock);
        InitializeConditionVariable(&g_conn_count_cond);
    }
}

typedef struct {
    SOCKET client_fd;
    char client_ip[64];
    char handler_name[MAX_HANDLER_NAME_LEN];
} ConnArgs;

static void release_connection_slot(void) {
    EnterCriticalSection(&g_conn_count_lock);
    g_active_connections--;
    WakeConditionVariable(&g_conn_count_cond);
    LeaveCriticalSection(&g_conn_count_lock);
}

static void lock_exec(void) { EnterCriticalSection(&g_nac_exec_lock); }
static void unlock_exec(void) { LeaveCriticalSection(&g_nac_exec_lock); }

static long long now_ms(void) {
    return (long long)GetTickCount64();
}

/* recv() with a total-request deadline. Returns the byte count, 0 on
 * orderly close, -1 on error, -2 on timeout. */
static int recv_deadline(SOCKET fd, char *buf, int len, long long deadline) {
    for (;;) {
        long long remain = deadline - now_ms();
        if (remain <= 0) return -2;

        struct timeval tv;
        tv.tv_sec = (long)(remain / 1000);
        tv.tv_usec = (long)((remain % 1000) * 1000);

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        int ready = select(0, &rfds, NULL, NULL, &tv);
        if (ready == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEINTR) continue;
            return -1;
        }
        if (ready == 0) return -2;

        int n = recv(fd, buf, len, 0);
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) continue;
        return n;
    }
}

static int send_all(SOCKET fd, const char *buf, int len) {
    int sent_total = 0;
    while (sent_total < len) {
        int sent = send(fd, buf + sent_total, len - sent_total, 0);
        if (sent == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEINTR) continue;
            return 0;
        }
        if (sent == 0) return 0;
        sent_total += sent;
    }
    return 1;
}

static void send_http_error(SOCKET fd, int status, const char *message) {
    char out[1024];
    int n = nac_http_build_error_response(status, message, out, sizeof(out));
    if (n > 0) send_all(fd, out, n);
}

static DWORD WINAPI handle_connection(LPVOID arg) {
    ConnArgs *ca = (ConnArgs*)arg;
    SOCKET client_fd = ca->client_fd;
    char handler_name[MAX_HANDLER_NAME_LEN];
    char client_ip[64];
    strncpy(handler_name, ca->handler_name, sizeof(handler_name) - 1);
    handler_name[sizeof(handler_name) - 1] = '\0';
    strncpy(client_ip, ca->client_ip, sizeof(client_ip) - 1);
    client_ip[sizeof(client_ip) - 1] = '\0';
    free(ca);

    char *raw = (char*)malloc(REQ_BUFFER_SIZE);
    char *resp = (char*)malloc(RESP_BUFFER_SIZE);
    if (!raw || !resp) {
        free(raw);
        free(resp);
        closesocket(client_fd);
        release_connection_slot();
        return 0;
    }

    DWORD timeout = NAC_READ_TIMEOUT_MS;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    long long deadline = now_ms() + NAC_READ_TIMEOUT_MS;
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
        closesocket(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return 0;
    }

    NacHttpRequest req;
    nac_http_parse_request(raw, header_end, &req);
    strncpy(req.client_ip, client_ip, sizeof(req.client_ip) - 1);
    req.client_ip[sizeof(req.client_ip) - 1] = '\0';

    if (req.method[0] == '\0' || req.path[0] != '/') {
        send_http_error(client_fd, 400, "400 Bad Request");
        nac_http_free_request(&req);
        closesocket(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return 0;
    }

    if (req.content_length < 0) {
        send_http_error(client_fd, 400, "400 Bad Request: invalid Content-Length");
        nac_http_free_request(&req);
        closesocket(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return 0;
    }
    if (req.content_length > NAC_MAX_BODY_BYTES) {
        send_http_error(client_fd, 413,
                        "413 Payload Too Large (request body limit is 8191 bytes)");
        nac_http_free_request(&req);
        closesocket(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return 0;
    }

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
        closesocket(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return 0;
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
    closesocket(client_fd);
    free(raw);
    free(resp);

    release_connection_slot();
    return 0;
}

int server_start(int port, const char *handler_name) {
    ensure_locks_initialized();

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        report_error("HTTP server: WSAStartup failed");
        return 1;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCKET) {
        report_error("HTTP server: could not create socket");
        WSACleanup();
        return 1;
    }

    BOOL opt = TRUE;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        report_error("HTTP server: bind failed (port may already be in use)");
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, 128) == SOCKET_ERROR) {
        report_error("HTTP server: listen failed");
        closesocket(server_fd);
        WSACleanup();
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
        int client_len = sizeof(client_addr);
        SOCKET client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == INVALID_SOCKET) continue;

        EnterCriticalSection(&g_conn_count_lock);
        while (g_active_connections >= MAX_CONCURRENT_CONNECTIONS) {
            SleepConditionVariableCS(&g_conn_count_cond, &g_conn_count_lock, INFINITE);
        }
        g_active_connections++;
        LeaveCriticalSection(&g_conn_count_lock);

        ConnArgs *ca = (ConnArgs*)malloc(sizeof(ConnArgs));
        if (!ca) {
            closesocket(client_fd);
            release_connection_slot();
            continue;
        }
        ca->client_fd = client_fd;
        ca->client_ip[0] = '\0';
        char *ip = inet_ntoa(client_addr.sin_addr);
        if (ip) {
            strncpy(ca->client_ip, ip, sizeof(ca->client_ip) - 1);
            ca->client_ip[sizeof(ca->client_ip) - 1] = '\0';
        }
        strncpy(ca->handler_name, handler_name ? handler_name : "", sizeof(ca->handler_name) - 1);
        ca->handler_name[sizeof(ca->handler_name) - 1] = '\0';

        HANDLE thread = CreateThread(NULL, 0, handle_connection, ca, 0, NULL);
        if (!thread) {
            closesocket(client_fd);
            free(ca);
            release_connection_slot();
            continue;
        }
        CloseHandle(thread); /* detach; the thread frees its own resources */
    }

    /* Unreachable in normal operation. */
    closesocket(server_fd);
    WSACleanup();
    return 0;
}

#endif
