#include "server.h"
#include <stddef.h>

#ifndef _WIN32

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "../runtime/eval.h"
#include "../util/error.h"
#include "server_common.h"

#define REQ_BUFFER_SIZE 65536
#define RESP_BUFFER_SIZE (MAX_STRING_LEN + 8192)
#define MAX_HANDLER_NAME_LEN 256
#define MAX_REQUESTS_PER_CONN 1000
#define MAX_KEEPALIVE_IDLE_MS 5000

/* The NaC interpreter keeps its call stack, variables, and error state in
 * plain global C variables (see eval.c / vartable.c) -- it was never
 * designed to be reentrant. Requests run on a fixed pool of worker threads
 * (see "Worker pool" below), but the actual call into the NaC handler
 * function still touches that shared global state, so it stays serialized
 * behind this mutex. Everything else -- accepting connections, waiting for
 * bytes, reading the request, parsing HTTP, building the response, writing
 * it back -- runs in parallel across the workers. */
static pthread_mutex_t g_nac_exec_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Caps how many connections are being handled at once (nac_cfg.max_connections),
 * so a burst of traffic can't spawn unbounded threads. accept() simply waits
 * for room once the cap is hit. */
static pthread_mutex_t g_conn_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_conn_count_cond = PTHREAD_COND_INITIALIZER;
static int g_active_connections = 0;

/* Graceful shutdown. SIGINT/SIGTERM only set this flag; the accept loop
 * polls it (a signal may be delivered to any thread, so we cannot rely on
 * accept() being interrupted). */
static volatile sig_atomic_t g_shutdown_requested = 0;
#define SHUTDOWN_POLL_MS 100

static void on_shutdown_signal(int sig) {
    (void)sig;
    if (g_shutdown_requested) {
        /* Second signal while draining: stop waiting and exit now. */
        static const char msg[] = "\nNaC: second signal received, exiting now\n";
        ssize_t ignored = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void)ignored;
        _exit(130);
    }
    g_shutdown_requested = 1;
}

static void install_shutdown_handlers(struct sigaction *old_int, struct sigaction *old_term) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_shutdown_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART: let blocking calls return EINTR */
    sigaction(SIGINT, &sa, old_int);
    sigaction(SIGTERM, &sa, old_term);
}

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

static int keepalive_idle_ms(void) {
    int ms = nac_cfg.read_timeout_ms;
    if (ms > MAX_KEEPALIVE_IDLE_MS) ms = MAX_KEEPALIVE_IDLE_MS;
    if (ms < 1000) ms = 1000;
    return ms;
}

/* ---- Worker pool -------------------------------------------------------
 *
 * Connections do not own a thread. Three kinds of thread cooperate:
 *
 *   acceptor  (server_start)  accepts sockets and hands them to the poller;
 *   poller    (poller_main)   watches idle connections with poll() and moves
 *                             one to the ready queue once bytes arrive; it
 *                             also closes connections that stay idle too long;
 *   workers   (worker_main)   a fixed number (nac_cfg.workers); each takes a
 *                             ready connection, serves exactly one request
 *                             and gives the connection back to the poller.
 *
 * An idle keep-alive connection therefore costs a file descriptor and a few
 * hundred bytes, not a thread, and a client that connects and sends nothing
 * never occupies a worker. A worker is only busy for the time one request is
 * being read (bounded by --read-timeout-ms), run and answered. */

typedef struct Conn {
    int fd;
    char ip[64];
    char *raw;          /* request buffer; NULL while idle with nothing buffered */
    size_t buffered;    /* bytes in raw (pipelined data may follow a request) */
    int served;         /* requests answered on this connection */
    long deadline;      /* poller: close the connection if still idle at this time */
    struct Conn *next;
} Conn;

enum { CONN_CLOSE = 0, CONN_IDLE = 1, CONN_READY = 2 };

static char g_handler_name[MAX_HANDLER_NAME_LEN];

/* Connections with a request to read, waiting for a free worker. */
static pthread_mutex_t g_ready_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ready_cond = PTHREAD_COND_INITIALIZER;
static Conn *g_ready_head = NULL;
static Conn *g_ready_tail = NULL;
static int g_pool_stop = 0;

/* Connections handed to the poller (new, or finished a request). */
static pthread_mutex_t g_inbox_mutex = PTHREAD_MUTEX_INITIALIZER;
static Conn *g_inbox = NULL;
static int g_wake_pipe[2] = { -1, -1 };

static void ready_push(Conn *c) {
    c->next = NULL;
    pthread_mutex_lock(&g_ready_mutex);
    if (g_ready_tail) g_ready_tail->next = c;
    else g_ready_head = c;
    g_ready_tail = c;
    pthread_cond_signal(&g_ready_cond);
    pthread_mutex_unlock(&g_ready_mutex);
}

/* Blocks for a ready connection; NULL once the pool is stopping and the queue
 * is empty. */
static Conn *ready_pop(void) {
    pthread_mutex_lock(&g_ready_mutex);
    while (!g_ready_head && !g_pool_stop) {
        pthread_cond_wait(&g_ready_cond, &g_ready_mutex);
    }
    Conn *c = g_ready_head;
    if (c) {
        g_ready_head = c->next;
        if (!g_ready_head) g_ready_tail = NULL;
        c->next = NULL;
    }
    pthread_mutex_unlock(&g_ready_mutex);
    return c;
}

static void wake_poller(void) {
    char b = 1;
    ssize_t ignored = write(g_wake_pipe[1], &b, 1);
    (void)ignored;
}

static void idle_add(Conn *c, long deadline) {
    c->deadline = deadline;
    pthread_mutex_lock(&g_inbox_mutex);
    c->next = g_inbox;
    g_inbox = c;
    pthread_mutex_unlock(&g_inbox_mutex);
    wake_poller();
}

static void conn_close(Conn *c) {
    close(c->fd);
    free(c->raw);
    free(c);
    release_connection_slot();
}

/* Drops leading blank lines between requests (RFC 9112 section 2.2). */
static void skip_blank_lines(char *raw, size_t *buffered) {
    while (*buffered >= 2 && raw[0] == '\r' && raw[1] == '\n') {
        memmove(raw, raw + 2, *buffered - 2);
        *buffered -= 2;
        raw[*buffered] = '\0';
    }
}

/* Reads and answers one request on `c`. Returns CONN_CLOSE when the
 * connection is finished, CONN_READY when another request is already
 * buffered (pipelining), and CONN_IDLE when it should wait for more bytes. */
static int serve_one(Conn *c, char *resp) {
    int client_fd = c->fd;
    const char *handler_name = g_handler_name;
    const char *client_ip = c->ip;

    if (!c->raw) {
        c->raw = (char*)malloc(REQ_BUFFER_SIZE);
        if (!c->raw) return CONN_CLOSE;
        c->raw[0] = '\0';
        c->buffered = 0;
    }
    char *raw = c->raw;
    size_t buffered = c->buffered;

    skip_blank_lines(raw, &buffered);

    int header_end = -1;
    int early_status = 0;
    int silent_close = 0;
    long deadline = now_ms() + nac_cfg.read_timeout_ms;

    while (header_end < 0) {
        char *found = strstr(raw, "\r\n\r\n");
        if (found) {
            header_end = (int)(found - raw) + 4;
            break;
        }
        if ((int)buffered >= nac_cfg.max_header_bytes) break;

        int n = recv_deadline(client_fd, raw + buffered,
                              (int)(REQ_BUFFER_SIZE - 1 - buffered), deadline);
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
        buffered += (size_t)n;
        raw[buffered] = '\0';
    }

    if (header_end < 0) {
        if (silent_close) return CONN_CLOSE;
        int err_status = early_status ? early_status : 431;
        send_http_error(client_fd, err_status,
                        err_status == 408 ? "408 Request Timeout"
                                          : err_status == 431 ? "431 Request Header Fields Too Large"
                                                              : "400 Bad Request");
        return CONN_CLOSE;
    }
    if (header_end > nac_cfg.max_header_bytes) {
        send_http_error(client_fd, 431, "431 Request Header Fields Too Large");
        return CONN_CLOSE;
    }

    NacHttpRequest req;
    nac_http_parse_request(raw, header_end, &req);
    strncpy(req.client_ip, client_ip, sizeof(req.client_ip) - 1);
    req.client_ip[sizeof(req.client_ip) - 1] = '\0';

    if (req.method[0] == '\0' || req.path[0] != '/') {
        send_http_error(client_fd, 400, "400 Bad Request");
        nac_http_free_request(&req);
        return CONN_CLOSE;
    }

    if (req.content_length < 0) {
        send_http_error(client_fd, 400, "400 Bad Request: invalid Content-Length");
        nac_http_free_request(&req);
        return CONN_CLOSE;
    }
    if (req.content_length > NAC_MAX_BODY_BYTES) {
        send_http_error(client_fd, 413,
                        "413 Payload Too Large (request body limit is 8191 bytes)");
        nac_http_free_request(&req);
        return CONN_CLOSE;
    }

    /* Message framing is Content-Length only; chunked bodies would
     * desynchronise the next request on a persistent connection. */
    Value *te = map_get(&req.headers, "transfer-encoding");
    if (te) {
        send_http_error(client_fd, 400,
                        "400 Bad Request: Transfer-Encoding is not supported");
        nac_http_free_request(&req);
        return CONN_CLOSE;
    }

    int want = req.content_length;
    int body_error = 0;
    while ((int)buffered - header_end < want) {
        int n = recv_deadline(client_fd, raw + buffered,
                              (int)(REQ_BUFFER_SIZE - 1 - buffered), deadline);
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
        return CONN_CLOSE;
    }

    int body_available = (int)buffered - header_end;
    if (body_available > want) body_available = want;
    if (body_available > 0) {
        int copy_len = body_available;
        if (copy_len >= MAX_STRING_LEN) copy_len = MAX_STRING_LEN - 1;
        memcpy(req.body, raw + header_end, (size_t)copy_len);
        req.body[copy_len] = '\0';
    }

    int keep_alive = !g_shutdown_requested &&
                     c->served + 1 < MAX_REQUESTS_PER_CONN &&
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
        int resp_len = nac_http_build_response(1, handler_name, result, keep_alive,
                                               strcmp(req.method, "HEAD") == 0,
                                               resp, RESP_BUFFER_SIZE, &status);
        if (resp_len > 0) {
            sent_ok = send_all(client_fd, resp, resp_len);
        } else {
            send_http_error(client_fd, 500, "500 Internal Server Error");
            sent_ok = 0;
        }
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
    skip_blank_lines(raw, &buffered);
    c->buffered = buffered;
    c->served++;

    if (!sent_ok || !keep_alive || g_shutdown_requested) return CONN_CLOSE;
    if (buffered > 0) return CONN_READY;

    free(c->raw);
    c->raw = NULL;
    return CONN_IDLE;
}

static void *worker_main(void *arg) {
    (void)arg;
    char *resp = (char*)malloc(RESP_BUFFER_SIZE);
    if (!resp) return NULL;

    for (;;) {
        Conn *c = ready_pop();
        if (!c) break;

        switch (serve_one(c, resp)) {
        case CONN_READY:
            ready_push(c);
            break;
        case CONN_IDLE:
            idle_add(c, now_ms() + keepalive_idle_ms());
            break;
        default:
            conn_close(c);
            break;
        }
    }
    free(resp);
    return NULL;
}

static void *poller_main(void *arg) {
    (void)arg;
    int cap = nac_cfg.max_connections + 1;
    Conn **idle = (Conn**)malloc(sizeof(Conn*) * (size_t)cap);
    struct pollfd *pfds = (struct pollfd*)malloc(sizeof(struct pollfd) * (size_t)(cap + 1));
    if (!idle || !pfds) {
        free(idle);
        free(pfds);
        return NULL;
    }
    int count = 0;

    for (;;) {
        pthread_mutex_lock(&g_ready_mutex);
        int stop = g_pool_stop;
        pthread_mutex_unlock(&g_ready_mutex);

        /* Take over connections handed in by the acceptor and the workers. */
        pthread_mutex_lock(&g_inbox_mutex);
        Conn *in = g_inbox;
        g_inbox = NULL;
        pthread_mutex_unlock(&g_inbox_mutex);
        while (in) {
            Conn *next = in->next;
            in->next = NULL;
            if (count < cap) idle[count++] = in;
            else conn_close(in);
            in = next;
        }

        /* No request is in progress on an idle connection, so on shutdown
         * (or pool stop) they can all go immediately. */
        if (stop || g_shutdown_requested) {
            for (int i = 0; i < count; i++) conn_close(idle[i]);
            count = 0;
            if (stop) break;
        }

        long now = now_ms();
        long wait = SHUTDOWN_POLL_MS;
        pfds[0].fd = g_wake_pipe[0];
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        for (int i = 0; i < count; i++) {
            pfds[i + 1].fd = idle[i]->fd;
            pfds[i + 1].events = POLLIN;
            pfds[i + 1].revents = 0;
            long left = idle[i]->deadline - now;
            if (left < wait) wait = left;
        }
        if (wait < 0) wait = 0;

        int ready = poll(pfds, (nfds_t)(count + 1), (int)wait);
        if (ready < 0 && errno != EINTR) continue;

        if (ready > 0 && (pfds[0].revents & POLLIN)) {
            char drain[64];
            while (read(g_wake_pipe[0], drain, sizeof(drain)) > 0) {}
        }

        now = now_ms();
        int kept = 0;
        for (int i = 0; i < count; i++) {
            Conn *c = idle[i];
            short ev = (ready > 0) ? pfds[i + 1].revents : 0;
            if (ev & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
                ready_push(c);   /* a worker reads the bytes, or sees the close */
            } else if (now >= c->deadline) {
                conn_close(c);   /* idle too long: close without a response */
            } else {
                idle[kept++] = c;
            }
        }
        count = kept;
    }

    free(idle);
    free(pfds);
    return NULL;
}

/* Resolves `host` (dotted IPv4 or a name such as "localhost") to an IPv4
 * address. NULL or "" means every interface. */
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
    signal(SIGPIPE, SIG_IGN);

    struct in_addr bind_addr;
    if (!resolve_host(host, &bind_addr)) {
        char msg[400];
        snprintf(msg, sizeof(msg),
                 "HTTP server: cannot resolve host '%s' to an IPv4 address", host);
        report_error(msg);
        return 1;
    }

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
    addr.sin_addr = bind_addr;
    addr.sin_port = htons((unsigned short)port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        report_error("HTTP server: bind failed (port may already be in use, or the address is not on this machine)");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 128) < 0) {
        report_error("HTTP server: listen failed");
        close(server_fd);
        return 1;
    }

    const char *shown_host = (host && host[0] && strcmp(host, NAC_DEFAULT_HOST) != 0) ? host : "localhost";
    if (handler_name && handler_name[0]) {
        printf("NaC HTTP server listening on http://%s:%d (handler: %s)\n", shown_host, port, handler_name);
    } else {
        printf("NaC HTTP server listening on http://%s:%d\n", shown_host, port);
    }
    fflush(stdout);

    strncpy(g_handler_name, handler_name ? handler_name : "", sizeof(g_handler_name) - 1);
    g_handler_name[sizeof(g_handler_name) - 1] = '\0';
    g_pool_stop = 0;
    g_ready_head = g_ready_tail = NULL;
    g_inbox = NULL;

    if (pipe(g_wake_pipe) != 0) {
        report_error("HTTP server: could not create the poller wake-up pipe");
        close(server_fd);
        return 1;
    }
    fcntl(g_wake_pipe[0], F_SETFL, fcntl(g_wake_pipe[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(g_wake_pipe[1], F_SETFL, fcntl(g_wake_pipe[1], F_GETFL, 0) | O_NONBLOCK);

    pthread_t poller_tid;
    if (pthread_create(&poller_tid, NULL, poller_main, NULL) != 0) {
        report_error("HTTP server: could not start the connection poller thread");
        close(g_wake_pipe[0]);
        close(g_wake_pipe[1]);
        close(server_fd);
        return 1;
    }

    int worker_count = nac_cfg.workers;
    pthread_t *workers = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)worker_count);
    int started = 0;
    if (workers) {
        for (int i = 0; i < worker_count; i++) {
            if (pthread_create(&workers[started], NULL, worker_main, NULL) == 0) started++;
        }
    }
    if (started == 0) {
        report_error("HTTP server: could not start worker threads");
        pthread_mutex_lock(&g_ready_mutex);
        g_pool_stop = 1;
        pthread_mutex_unlock(&g_ready_mutex);
        wake_poller();
        pthread_join(poller_tid, NULL);
        free(workers);
        close(g_wake_pipe[0]);
        close(g_wake_pipe[1]);
        close(server_fd);
        return 1;
    }

    g_shutdown_requested = 0;
    struct sigaction old_int, old_term;
    install_shutdown_handlers(&old_int, &old_term);

    while (!g_shutdown_requested) {
        /* Wait for a connection, but wake up regularly to notice a shutdown
         * request. */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        struct timeval poll_tv;
        poll_tv.tv_sec = 0;
        poll_tv.tv_usec = SHUTDOWN_POLL_MS * 1000;
        int ready = select(server_fd + 1, &rfds, NULL, NULL, &poll_tv);
        if (ready <= 0) continue; /* timeout, or EINTR from a signal */

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        pthread_mutex_lock(&g_conn_count_mutex);
        while (g_active_connections >= nac_cfg.max_connections && !g_shutdown_requested) {
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_nsec += (long)SHUTDOWN_POLL_MS * 1000000L;
            if (until.tv_nsec >= 1000000000L) {
                until.tv_sec += 1;
                until.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&g_conn_count_cond, &g_conn_count_mutex, &until);
        }
        if (g_shutdown_requested) {
            pthread_mutex_unlock(&g_conn_count_mutex);
            send_http_error(client_fd, 503, "503 Service Unavailable");
            close(client_fd);
            break;
        }
        g_active_connections++;
        pthread_mutex_unlock(&g_conn_count_mutex);

        Conn *c = (Conn*)calloc(1, sizeof(Conn));
        if (!c) {
            close(client_fd);
            release_connection_slot();
            continue;
        }
        c->fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, c->ip, sizeof(c->ip));

        /* Socket-level fallback timeouts (the read loop also enforces one
         * deadline for the whole request, so slow trickling clients cannot
         * keep a worker alive forever). */
        struct timeval io_timeout;
        io_timeout.tv_sec = nac_cfg.read_timeout_ms / 1000;
        io_timeout.tv_usec = (nac_cfg.read_timeout_ms % 1000) * 1000;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &io_timeout, sizeof(io_timeout));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &io_timeout, sizeof(io_timeout));

        /* A new connection waits in the poller until its first bytes arrive. */
        idle_add(c, now_ms() + nac_cfg.read_timeout_ms);
    }

    /* Graceful shutdown: stop accepting (new connections are refused from
     * here on), then give the requests already running time to finish. */
    close(server_fd);

    pthread_mutex_lock(&g_conn_count_mutex);
    int in_flight = g_active_connections;
    pthread_mutex_unlock(&g_conn_count_mutex);
    printf("NaC: shutdown requested, no longer accepting connections; %d request(s) in flight (waiting up to %d ms)\n",
           in_flight, nac_cfg.shutdown_timeout_ms);
    fflush(stdout);

    long drain_deadline = now_ms() + nac_cfg.shutdown_timeout_ms;
    pthread_mutex_lock(&g_conn_count_mutex);
    while (g_active_connections > 0) {
        long remain = drain_deadline - now_ms();
        if (remain <= 0) break;
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += (time_t)(remain / 1000);
        until.tv_nsec += (long)(remain % 1000) * 1000000L;
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec += 1;
            until.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&g_conn_count_cond, &g_conn_count_mutex, &until);
    }
    int still_running = g_active_connections;
    pthread_mutex_unlock(&g_conn_count_mutex);

    if (still_running > 0) {
        /* Workers share interpreter state that teardown would free under
         * them, so do not return: cut them off and exit. */
        fprintf(stderr, "NaC: shutdown timed out with %d request(s) still running; exiting\n", still_running);
        fflush(stdout);
        fflush(stderr);
        _exit(1);
    }

    pthread_mutex_lock(&g_ready_mutex);
    g_pool_stop = 1;
    pthread_cond_broadcast(&g_ready_cond);
    pthread_mutex_unlock(&g_ready_mutex);
    wake_poller();
    for (int i = 0; i < started; i++) pthread_join(workers[i], NULL);
    pthread_join(poller_tid, NULL);
    free(workers);
    close(g_wake_pipe[0]);
    close(g_wake_pipe[1]);

    printf("NaC: shutdown complete\n");
    fflush(stdout);

    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    return 0;
}

#endif
