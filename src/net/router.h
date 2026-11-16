#ifndef NAC_ROUTER_H
#define NAC_ROUTER_H

#include "../runtime/value.h"

#define MAX_ROUTES 128
#define MAX_MOUNTS 8
#define MAX_MIDDLEWARE 32

/* Registers `route METHOD "pattern" { ... }`. `method` is "GET", "POST",
 * ... or "*" / "ANY" for every method. Patterns are slash-separated and
 * may contain ":name" segments (captured into `params`) and a trailing
 * "*" segment (captures the rest of the path as params["*"]).
 * Returns 0 on success, -1 if the route table is full. */
int router_add(const char *method, const char *pattern, const char *fn_name);

/* Finds the route for (method, path). On a match returns 1, copies the
 * route function's name into fn_out and sets *params to a new map of
 * captured path parameters (caller frees it). Otherwise returns 0 and
 * sets *path_matched to 1 if the path matched a route for a different
 * method (so the caller can answer 405 instead of 404). */
int router_match(const char *method, const char *path, char *fn_out, int fn_cap,
                 Value *params, int *path_matched);

/* `before { ... }` and `after { ... }` blocks (middleware). Each is a NaC
 * function registered under `fn_name`. `pattern` limits it to matching paths
 * (same syntax as route patterns: ":name" segments, trailing "*"); NULL or
 * "" means every request. Returns 0 on success, -1 if the table is full. */
typedef enum { MW_BEFORE = 0, MW_AFTER = 1 } MiddlewareKind;
int middleware_add(MiddlewareKind kind, const char *pattern, const char *fn_name);

/* Number of registered blocks of both kinds (0 for most programs). */
int middleware_count(void);

/* Walks the blocks of `kind` that apply to `path`, in the order they were
 * written. Start with *cursor = 0. Returns 1 and the function name each time
 * (advancing *cursor), 0 when there are no more. */
int middleware_next(MiddlewareKind kind, int *cursor, const char *path,
                    char *fn_out, int fn_cap);

/* static("dir") / static("/prefix", "dir"): serve files from `dir` for
 * URLs under `prefix`. Returns 0 on success, -1 if the table is full. */
int static_mount_add(const char *prefix, const char *dir);

/* Tries to answer GET/HEAD requests from the mounted directories.
 * `req_headers` is the request's header map (lowercased names) and may be
 * NULL; it enables conditional GET (If-None-Match / If-Modified-Since).
 * `lock`/`unlock` guard the shared in-memory file cache and may be NULL.
 * `keep_alive` selects the Connection header of the response.
 * Returns a malloc'd, complete HTTP response (length in *resp_len and
 * status in *status_out, 200 or 304) or NULL if no mount has the file.
 * Files are streamed as raw bytes, so they are not limited by
 * MAX_STRING_LEN. Responses carry ETag/Last-Modified and small files are
 * cached in memory. */
char *static_serve(const char *method, const char *path, Value *req_headers,
                   int *resp_len, int *status_out, int keep_alive,
                   void (*lock)(void), void (*unlock)(void));

#endif
