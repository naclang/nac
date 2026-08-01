#ifndef NAC_SERVER_H
#define NAC_SERVER_H

/* Starts a blocking HTTP server on `port`. For every request received,
 * the NaC function named `handler_name` is invoked with 5 arguments:
 *   handler(method, path, query, headers, body)
 * where `method`/`path`/`body` are strings and `query`/`headers` are maps
 * of string -> string.
 *
 * The handler is expected to return either:
 *   - a map with optional keys "status" (int), "body" (string/array/map),
 *     and "headers" (map of string -> string), or
 *   - a plain string, used directly as the response body with status 200.
 *
 * This call never returns under normal operation (the accept loop runs
 * forever); it returns a non-zero value only if the server could not be
 * started (e.g. the port is already in use).
 */
int server_start(int port, const char *handler_name);

#endif
