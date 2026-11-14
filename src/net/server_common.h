#ifndef NAC_SERVER_COMMON_H
#define NAC_SERVER_COMMON_H

#include "../runtime/value.h"

/* Request limits / hardening defaults. */
#define NAC_MAX_HEADER_BYTES 16384
#define NAC_MAX_BODY_BYTES (MAX_STRING_LEN - 1)
#define NAC_READ_TIMEOUT_MS 10000
#define NAC_RATE_LIMIT_MAX 120
#define NAC_RATE_LIMIT_WINDOW_SEC 60

typedef struct {
    char method[16];
    char path[1024];
    char query_raw[2048];
    Value query;    /* map: string -> string */
    Value headers;  /* map: string -> string (lowercased keys) */
    Value cookie;   /* map: cookie name -> value */
    char body[MAX_STRING_LEN];
    int content_length;
    char client_ip[64];
} NacHttpRequest;

/* Parses everything up to and including the blank line ("\r\n\r\n") that
 * terminates the HTTP header block. `raw` must be NUL-terminated and
 * contain at least the full header block (header_end is its length,
 * i.e. the offset of the first body byte). Fills every field of `req`
 * except `body`, which the caller fills in once enough bytes have been
 * read from the socket. */
void nac_http_parse_request(const char *raw, int header_end, NacHttpRequest *req);

/* Frees the `query`, `headers` and `cookie` maps allocated by
 * nac_http_parse_request. */
void nac_http_free_request(NacHttpRequest *req);

/* Invokes the handler (already looked up via call_nac_function by the
 * caller) result processing: builds a full HTTP response (status line,
 * headers, blank line, body) into `out` (capacity out_cap) and returns
 * the number of bytes written, or -1 if it didn't fit. `found_handler`
 * indicates whether the NaC function was actually found and called;
 * `result` is only read when it is non-zero. */
int nac_http_build_response(int found_handler, const char *handler_name, Value result,
                             char *out, int out_cap, int *out_status);

/* Builds a minimal status response (used for 408/413/429/431 before a
 * route runs). Returns the same value as nac_http_build_response. */
int nac_http_build_error_response(int status, const char *message,
                                  char *out, int out_cap);

/* Locks/unlocks the interpreter's global execution lock (platform specific). */
typedef void (*NacLockFn)(void);

/* Routes one parsed request. Order: rate limit -> `route` table -> static()
 * mounts -> legacy serve(port, "handler") function -> 404/405.
 *
 * Returns 0 and fills *result (a Value the caller must free_value) for
 * dynamic answers, to be turned into HTTP by nac_http_build_response.
 * Returns 1 for static files: *raw_out is then a malloc'd, complete HTTP
 * response of *raw_len bytes (free() it) and *status holds its status. */
int nac_dispatch_request(NacHttpRequest *req, const char *handler_name,
                         NacLockFn lock, NacLockFn unlock,
                         Value *result, char **raw_out, int *raw_len, int *status);

/* --- Set-Cookie response accumulator ---------------------------------
 * setCookie()/clearCookie() append lines to a per-thread buffer; the
 * request's response builder emits one Set-Cookie header per line. The
 * buffer is reset at the start of each request. */
void nac_cookie_reset(void);
int nac_cookie_add(const char *name, const char *value,
                   const char *path, int max_age, const char *expires,
                   int http_only, int secure, const char *same_site,
                   const char *domain);
int nac_cookie_count(void);
const char *nac_cookie_line(int index);

/* Fixed-window per-IP rate limiter. Returns 1 if the request may run,
 * 0 when the limit is exceeded (and sets *retry_after seconds). */
int nac_rate_limit_allow(const char *ip, NacLockFn lock, NacLockFn unlock,
                         int *retry_after);

#endif
