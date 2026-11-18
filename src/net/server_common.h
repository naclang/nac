#ifndef NAC_SERVER_COMMON_H
#define NAC_SERVER_COMMON_H

#include "../core/config.h"
#include "../runtime/value.h"

/* The header limit, read timeout, rate limit, connection cap and body limit
 * are runtime settings (nac_cfg in core/config.h: flags, environment, defaults). */
#define NAC_MAX_BODY_BYTES (nac_cfg.max_body_bytes)

typedef struct {
    char method[16];
    char path[1024];
    char query_raw[2048];
    Value query;    /* map: string -> string */
    Value headers;  /* map: string -> string (lowercased keys) */
    Value cookie;   /* map: cookie name -> value */
    char *body;     /* heap, always NUL-terminated; sized to the request */
    int content_length;
    int http_minor;   /* minor version of "HTTP/1.x"; 0 for HTTP/1.0 or older */
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

/* Copies `len` bytes of request body into req->body (replacing the empty
 * default). Returns 0, or -1 when out of memory. */
int nac_http_set_body(NacHttpRequest *req, const char *data, int len);

/* True when the connection may be reused for another request: HTTP/1.1
 * defaults to persistent, HTTP/1.0 requires "Connection: keep-alive", and
 * either version can opt out with "Connection: close". */
int nac_http_request_keep_alive(const NacHttpRequest *req);

/* Invokes the handler (already looked up via call_nac_function by the
 * caller) result processing: builds a full HTTP response (status line,
 * headers, blank line, body) into `out` (capacity out_cap) and returns
 * the number of bytes written, or -1 if it didn't fit. `found_handler`
 * indicates whether the NaC function was actually found and called;
 * `result` is only read when it is non-zero. `keep_alive` selects the
 * Connection header and `is_head` suppresses the response body for HEAD. */
/* Same as nac_http_build_response, but the response is returned as a
 * malloc'd, NUL-terminated buffer of any size (caller free()s it) and
 * its length is stored in *out_len. Returns NULL on failure. */
char *nac_http_build_response_alloc(int found_handler, const char *handler_name,
                                    Value result, int keep_alive, int is_head,
                                    int *out_len, int *out_status);

int nac_http_build_response(int found_handler, const char *handler_name, Value result,
                             int keep_alive, int is_head, char *out, int out_cap,
                             int *out_status);

/* Builds a minimal status response (used for 408/413/429/431 before a
 * route runs). Returns the same value as nac_http_build_response. */
int nac_http_build_error_response(int status, const char *message,
                                  char *out, int out_cap);

/* Locks/unlocks the interpreter's global execution lock (platform specific). */
typedef void (*NacLockFn)(void);

/* Routes one parsed request. Order: built-in /health (when enabled and no
 * route claims it; exempt from the rate limit) -> rate limit -> `route`
 * table -> static() mounts -> legacy serve(port, "handler") function ->
 * 404/405.
 *
 * Returns 0 and fills *result (a Value the caller must free_value) for
 * dynamic answers, to be turned into HTTP by nac_http_build_response.
 * Returns 1 for static files: *raw_out is then a malloc'd, complete HTTP
 * response of *raw_len bytes (free() it) and *status holds its status. */
int nac_dispatch_request(NacHttpRequest *req, const char *handler_name,
                         int keep_alive, NacLockFn lock, NacLockFn unlock,
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

/* --- Extra response headers (setHeader) -------------------------------
 * setHeader() collects headers in a per-thread buffer, reset at the start of
 * each request. They are applied to the final response (dynamic or static),
 * replacing a header of the same name (case-insensitive). Returns 0, or -1
 * when the name/value is invalid (control characters) or the buffer is full. */
void nac_header_reset(void);
int nac_header_set(const char *name, const char *value);

/* --- Middleware (before / after blocks) --------------------------------
 * Both must be called with the interpreter lock held. */

/* Runs the `before` blocks that apply to req->path, in order, each with
 * (method, path, query, headers, body, params, cookie). The first block that
 * returns something other than null answers the request: returns 1 and sets
 * *answer (a Value the caller must free_value). Returns 0 when none did. */
int nac_run_before(const NacHttpRequest *req, Value params, Value *answer);

/* Finishes a response: runs the `after` blocks (same arguments as `before`
 * plus `response`, a map of status/body/headers), then applies setHeader()
 * headers. Exactly one of *result (a dynamic result) and *raw (a malloc'd,
 * complete static-file response of *raw_len bytes with status *status) holds
 * the response. A block that returns a response replaces it, in which case
 * *raw is freed and set to NULL and *result is filled. For a static file
 * `response.body` is "". Pass raw = NULL when there are no static files. */
void nac_finish_response(const NacHttpRequest *req, Value params,
                         Value *result, char **raw, int *raw_len, int *status);

/* Fixed-window per-IP rate limiter. Returns 1 if the request may run,
 * 0 when the limit is exceeded (and sets *retry_after seconds). */
int nac_rate_limit_allow(const char *ip, NacLockFn lock, NacLockFn unlock,
                         int *retry_after);

#endif
