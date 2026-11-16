#ifndef NAC_SERVER_H
#define NAC_SERVER_H

/* Starts a blocking HTTP server on `host`:`port`. `host` is an IPv4 address
 * or host name; NULL or "" listens on every interface (0.0.0.0). Limits come
 * from nac_cfg (see core/config.h), which the caller must have resolved with
 * nac_config_apply_env() first. For every request received,
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
 * The accept loop runs until the process gets SIGINT or SIGTERM (Ctrl+C,
 * Ctrl+Break or console close on Windows). It then shuts down gracefully:
 * the listening socket is closed, new connections are refused, and the
 * requests already being handled get up to nac_cfg.shutdown_timeout_ms to
 * finish. Returns 0 when they all finished in time. If any are still
 * running when the timeout ends, the process exits with status 1 (cutting
 * them off) instead of returning. A second signal while draining exits
 * immediately. server_start returns a non-zero value only if the server
 * could not be started (e.g. the port is already in use).
 */
int server_start(int port, const char *host, const char *handler_name);

#endif
