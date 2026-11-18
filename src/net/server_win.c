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
#define MAX_HANDLER_NAME_LEN 256
#define MAX_REQUESTS_PER_CONN 1000
#define MAX_KEEPALIVE_IDLE_MS 5000

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

/* Graceful shutdown. Ctrl+C / Ctrl+Break / console close only set this flag
 * (the handler runs on its own thread); the accept loop polls it. */
static volatile LONG g_shutdown_requested = 0;
static volatile LONG g_drain_finished = 0;
#define SHUTDOWN_POLL_MS 100

static BOOL WINAPI console_ctrl_handler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            break;
        default:
            return FALSE;
    }
    if (InterlockedExchange(&g_shutdown_requested, 1)) {
        /* Second signal while draining: stop waiting and exit now. */
        TerminateProcess(GetCurrentProcess(), 130);
        return TRUE;
    }
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        /* Windows ends the process as soon as this handler returns (and after
         * ~5 s regardless), so hold it open until the drain has finished. */
        for (int i = 0; i < 45 && !g_drain_finished; i++) Sleep(100);
    }
    return TRUE;
}

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

static int keepalive_idle_ms(void) {
    int ms = nac_cfg.read_timeout_ms;
    if (ms > MAX_KEEPALIVE_IDLE_MS) ms = MAX_KEEPALIVE_IDLE_MS;
    if (ms < 1000) ms = 1000;
    return ms;
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

    size_t raw_cap = REQ_BUFFER_SIZE;
    char *raw = (char*)malloc(raw_cap);
    if (!raw) {
        closesocket(client_fd);
        release_connection_slot();
        return 0;
    }
    raw[0] = '\0';

    DWORD timeout = (DWORD)nac_cfg.read_timeout_ms;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    size_t buffered = 0;
    int served = 0;

    for (;;) {
        if (g_shutdown_requested) break;
        if (served >= MAX_REQUESTS_PER_CONN) break;

        /* Ignore empty lines between requests (RFC 9112 section 2.2). */
        while (buffered >= 2 && raw[0] == '\r' && raw[1] == '\n') {
            memmove(raw, raw + 2, buffered - 2);
            buffered -= 2;
            raw[buffered] = '\0';
        }

        int header_end = -1;
        int early_status = 0;
        int silent_close = 0;
        int got_any = buffered > 0;
        long long deadline = (served == 0 || got_any)
            ? now_ms() + nac_cfg.read_timeout_ms
            : now_ms() + keepalive_idle_ms();

        while (header_end < 0) {
            char *found = strstr(raw, "\r\n\r\n");
            if (found) {
                header_end = (int)(found - raw) + 4;
                break;
            }
            if ((int)buffered >= nac_cfg.max_header_bytes) break;

            int n = recv_deadline(client_fd, raw + buffered,
                                  (int)(raw_cap - 1 - buffered), deadline);
            if (n == -2) {
                if (buffered == 0) silent_close = 1;
                else early_status = 408;
                break;
            }
            if (n <= 0) {
                if (buffered == 0) silent_close = 1;
                else early_status = 400;
                break;
            }
            if (!got_any) {
                got_any = 1;
                deadline = now_ms() + nac_cfg.read_timeout_ms;
            }
            buffered += (size_t)n;
            raw[buffered] = '\0';
        }

        if (header_end < 0) {
            if (silent_close) break;
            int err_status = early_status ? early_status : 431;
            send_http_error(client_fd, err_status,
                            err_status == 408 ? "408 Request Timeout"
                                              : err_status == 431 ? "431 Request Header Fields Too Large"
                                                                  : "400 Bad Request");
            break;
        }
        if (header_end > nac_cfg.max_header_bytes) {
            send_http_error(client_fd, 431, "431 Request Header Fields Too Large");
            break;
        }

        NacHttpRequest req;
        nac_http_parse_request(raw, header_end, &req);
        strncpy(req.client_ip, client_ip, sizeof(req.client_ip) - 1);
        req.client_ip[sizeof(req.client_ip) - 1] = '\0';

        if (req.method[0] == '\0' || req.path[0] != '/') {
            send_http_error(client_fd, 400, "400 Bad Request");
            nac_http_free_request(&req);
            break;
        }

        if (req.content_length < 0) {
            send_http_error(client_fd, 400, "400 Bad Request: invalid Content-Length");
            nac_http_free_request(&req);
            break;
        }
        if (req.content_length > NAC_MAX_BODY_BYTES) {
            char too_big[128];
            snprintf(too_big, sizeof(too_big),
                     "413 Payload Too Large (request body limit is %d bytes)", NAC_MAX_BODY_BYTES);
            send_http_error(client_fd, 413, too_big);
            nac_http_free_request(&req);
            break;
        }

        /* Message framing is Content-Length only; chunked bodies would
         * desynchronise the next request on a persistent connection. */
        Value *te = map_get(&req.headers, "transfer-encoding");
        if (te) {
            send_http_error(client_fd, 400,
                            "400 Bad Request: Transfer-Encoding is not supported");
            nac_http_free_request(&req);
            break;
        }

        int want = req.content_length;
    if ((size_t)header_end + (size_t)want + 1 > raw_cap) {
        size_t need = (size_t)header_end + (size_t)want + 1;
        char *grown = (char*)realloc(raw, need);
        if (!grown) {
            send_http_error(client_fd, 500, "500 Internal Server Error");
            nac_http_free_request(&req);
            break;
        }
        raw = grown;
        raw_cap = need;
    }
        int body_error = 0;
        while ((int)buffered - header_end < want) {
            int n = recv_deadline(client_fd, raw + buffered,
                                  (int)(raw_cap - 1 - buffered), deadline);
            if (n == -2) {
                body_error = 408;
                break;
            }
            if (n <= 0) {
                body_error = 400;
                break;
            }
            buffered += (size_t)n;
            raw[buffered] = '\0';
        }

        if (body_error) {
            send_http_error(client_fd, body_error,
                            body_error == 408 ? "408 Request Timeout" : "400 Bad Request");
            nac_http_free_request(&req);
            break;
        }

        int body_available = (int)buffered - header_end;
        if (body_available > want) body_available = want;
        if (body_available > 0) nac_http_set_body(&req, raw + header_end, body_available);

        int keep_alive = !g_shutdown_requested &&
                         served + 1 < MAX_REQUESTS_PER_CONN &&
                         nac_http_request_keep_alive(&req);

        Value result;
        char *raw_resp = NULL;
        int raw_len = 0;
        int status = 0;
        int is_raw = nac_dispatch_request(&req, handler_name, keep_alive,
                                          lock_exec, unlock_exec,
                                          &result, &raw_resp, &raw_len, &status);

        int sent_ok = 1;
        if (is_raw) {
            sent_ok = send_all(client_fd, raw_resp, raw_len);
            free(raw_resp);
        } else {
            int resp_len = 0;
            char *resp = nac_http_build_response_alloc(1, handler_name, result, keep_alive,
                                                       strcmp(req.method, "HEAD") == 0,
                                                       &resp_len, &status);
            if (resp && resp_len > 0) {
                sent_ok = send_all(client_fd, resp, resp_len);
            } else {
                send_http_error(client_fd, 500, "500 Internal Server Error");
                sent_ok = 0;
            }
            free(resp);
            free_value(&result);
        }

        printf("%s %s -> %d\n", req.method, req.path, status);
        fflush(stdout);

        int request_end = header_end + req.content_length;
        nac_http_free_request(&req);

        /* Keep any pipelined bytes that arrived with this request. */
        if (request_end < 0 || (size_t)request_end > buffered) {
            buffered = 0;
        } else {
            size_t leftover = buffered - (size_t)request_end;
            if (leftover > 0) memmove(raw, raw + request_end, leftover);
            buffered = leftover;
        }
        raw[buffered] = '\0';
        if (raw_cap > REQ_BUFFER_SIZE && buffered < REQ_BUFFER_SIZE) {
            char *small = (char*)realloc(raw, REQ_BUFFER_SIZE);
            if (small) {
                raw = small;
                raw_cap = REQ_BUFFER_SIZE;
            }
        }
        served++;

        if (!sent_ok || !keep_alive || g_shutdown_requested) break;
    }

    closesocket(client_fd);
    free(raw);

    release_connection_slot();
    return 0;
}

/* Resolves `host` (dotted IPv4 or a name such as "localhost") to an IPv4
 * address. NULL or "" means every interface. WSAStartup must have run. */
static int resolve_host(const char *host, struct in_addr *out) {
    if (!host || !host[0]) {
        out->s_addr = htonl(INADDR_ANY);
        return 1;
    }

    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;

    *out = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return 1;
}

int server_start(int port, const char *host, const char *handler_name) {
    ensure_locks_initialized();

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        report_error("HTTP server: WSAStartup failed");
        return 1;
    }

    struct in_addr bind_addr;
    if (!resolve_host(host, &bind_addr)) {
        char msg[400];
        snprintf(msg, sizeof(msg),
                 "HTTP server: cannot resolve host '%s' to an IPv4 address", host);
        report_error(msg);
        WSACleanup();
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
    addr.sin_addr = bind_addr;
    addr.sin_port = htons((unsigned short)port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        report_error("HTTP server: bind failed (port may already be in use, or the address is not on this machine)");
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

    const char *shown_host = (host && host[0] && strcmp(host, NAC_DEFAULT_HOST) != 0) ? host : "localhost";
    if (handler_name && handler_name[0]) {
        printf("NaC HTTP server listening on http://%s:%d (handler: %s)\n", shown_host, port, handler_name);
    } else {
        printf("NaC HTTP server listening on http://%s:%d\n", shown_host, port);
    }
    fflush(stdout);

    g_shutdown_requested = 0;
    g_drain_finished = 0;
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    while (!g_shutdown_requested) {
        /* Wait for a connection, but wake up regularly to notice a shutdown
         * request. */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        struct timeval poll_tv;
        poll_tv.tv_sec = 0;
        poll_tv.tv_usec = SHUTDOWN_POLL_MS * 1000;
        int ready = select(0, &rfds, NULL, NULL, &poll_tv);
        if (ready <= 0) continue; /* timeout or transient error */

        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == INVALID_SOCKET) continue;

        EnterCriticalSection(&g_conn_count_lock);
        while (g_active_connections >= nac_cfg.max_connections && !g_shutdown_requested) {
            SleepConditionVariableCS(&g_conn_count_cond, &g_conn_count_lock, SHUTDOWN_POLL_MS);
        }
        if (g_shutdown_requested) {
            LeaveCriticalSection(&g_conn_count_lock);
            send_http_error(client_fd, 503, "503 Service Unavailable");
            closesocket(client_fd);
            break;
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

    /* Graceful shutdown: stop accepting (new connections are refused from
     * here on), then give the requests already running time to finish. */
    closesocket(server_fd);

    EnterCriticalSection(&g_conn_count_lock);
    int in_flight = g_active_connections;
    LeaveCriticalSection(&g_conn_count_lock);
    printf("NaC: shutdown requested, no longer accepting connections; %d request(s) in flight (waiting up to %d ms)\n",
           in_flight, nac_cfg.shutdown_timeout_ms);
    fflush(stdout);

    long long drain_deadline = now_ms() + nac_cfg.shutdown_timeout_ms;
    EnterCriticalSection(&g_conn_count_lock);
    while (g_active_connections > 0) {
        long long remain = drain_deadline - now_ms();
        if (remain <= 0) break;
        SleepConditionVariableCS(&g_conn_count_cond, &g_conn_count_lock, (DWORD)remain);
    }
    int still_running = g_active_connections;
    LeaveCriticalSection(&g_conn_count_lock);

    if (still_running > 0) {
        /* Workers share interpreter state that teardown would free under
         * them, so do not return: cut them off and exit. */
        fprintf(stderr, "NaC: shutdown timed out with %d request(s) still running; exiting\n", still_running);
        fflush(stdout);
        fflush(stderr);
        ExitProcess(1);
    }

    printf("NaC: shutdown complete\n");
    fflush(stdout);

    InterlockedExchange(&g_drain_finished, 1);
    SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
    WSACleanup();
    return 0;
}

#endif
