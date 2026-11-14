#ifndef NAC_ROUTER_H
#define NAC_ROUTER_H

#include "../runtime/value.h"

#define MAX_ROUTES 128
#define MAX_MOUNTS 8

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

/* static("dir") / static("/prefix", "dir"): serve files from `dir` for
 * URLs under `prefix`. Returns 0 on success, -1 if the table is full. */
int static_mount_add(const char *prefix, const char *dir);

/* Tries to answer GET/HEAD requests from the mounted directories.
 * Returns a malloc'd, complete HTTP response (length in *resp_len and
 * status in *status_out) or NULL if no mount has the file. Files are
 * streamed as raw bytes, so they are not limited by MAX_STRING_LEN. */
char *static_serve(const char *method, const char *path, int *resp_len, int *status_out);

#endif
