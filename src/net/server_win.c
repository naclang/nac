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
    char handler_name[MAX_HANDLER_NAME_LEN];
} ConnArgs;

static void release_connection_slot(void) {
    EnterCriticalSection(&g_conn_count_lock);
    g_active_connections--;
    WakeConditionVariable(&g_conn_count_cond);
    LeaveCriticalSection(&g_conn_count_lock);
}

static DWORD WINAPI handle_connection(LPVOID arg) {
    ConnArgs *ca = (ConnArgs*)arg;
    SOCKET client_fd = ca->client_fd;
    char handler_name[MAX_HANDLER_NAME_LEN];
    strncpy(handler_name, ca->handler_name, sizeof(handler_name) - 1);
    handler_name[sizeof(handler_name) - 1] = '\0';
    free(ca);

    char *raw = (char*)malloc(REQ_BUFFER_SIZE);
    char *resp = (char*)malloc(RESP_BUFFER_SIZE);

    size_t total = 0;
    int header_end = -1;

    while (total < REQ_BUFFER_SIZE - 1) {
        int n = recv(client_fd, raw + total, (int)(REQ_BUFFER_SIZE - 1 - total), 0);
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
        closesocket(client_fd);
        free(raw);
        free(resp);
        release_connection_slot();
        return 0;
    }

    NacHttpRequest req;
    nac_http_parse_request(raw, header_end, &req);

    int want = req.content_length;
    int max_body = REQ_BUFFER_SIZE - header_end - 1;
    if (want > max_body) want = max_body;

    while ((int)(total - header_end) < want) {
        int n = recv(client_fd, raw + total, (int)(REQ_BUFFER_SIZE - 1 - total), 0);
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
    EnterCriticalSection(&g_nac_exec_lock);
    int found = call_nac_function(handler_name, args, 5, &result);
    LeaveCriticalSection(&g_nac_exec_lock);

    int status = 0;
    int resp_len = nac_http_build_response(found, handler_name, result, resp, RESP_BUFFER_SIZE, &status);

    if (resp_len > 0) {
        int sent_total = 0;
        while (sent_total < resp_len) {
            int sent = send(client_fd, resp + sent_total, resp_len - sent_total, 0);
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

    printf("NaC HTTP server listening on http://localhost:%d (handler: %s)\n", port, handler_name);
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
        ca->client_fd = client_fd;
        strncpy(ca->handler_name, handler_name, sizeof(ca->handler_name) - 1);
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
