#ifndef NAC_CONFIG_H
#define NAC_CONFIG_H

#include <stddef.h>

/* Loads .env values only when the process environment doesn't already define
 * the key. Returns 0 when the file is absent, 1 when loaded, and -1 on error. */
int nac_load_dotenv(const char *path, char *error, size_t error_cap);

/* ---- Runtime configuration -------------------------------------------
 *
 * Server limits and the listen address can be set, from highest to lowest
 * priority, by a command-line flag, by an environment variable (which
 * includes .env), or by the built-in default below:
 *
 *   --port N                  (serve(port) argument, then PORT, then 8080)
 *   --host ADDR               NAC_HOST                      0.0.0.0
 *   --max-connections N       NAC_MAX_CONNECTIONS           64
 *   --read-timeout-ms N       NAC_READ_TIMEOUT_MS           10000
 *   --max-header-bytes N      NAC_MAX_HEADER_BYTES          16384
 *   --rate-limit N            NAC_RATE_LIMIT_MAX            120   (0 = off)
 *   --rate-window-sec N       NAC_RATE_LIMIT_WINDOW_SEC     60
 *   --health                  NAC_HEALTH                    off
 *   --shutdown-timeout-ms N   NAC_SHUTDOWN_TIMEOUT_MS       10000
 *   --workers N               NAC_WORKERS                   8
 *   --max-body-bytes N        NAC_MAX_BODY_BYTES            1048576
 *
 * Flags work in any position: `nac --port 80 app.nac` and
 * `nac app.nac --port 80` are the same. They are removed from args(). A "--"
 * ends flag parsing: `nac app.nac -- --port x` gives the script ["--port", "x"]. */

#define NAC_DEFAULT_HOST "0.0.0.0"
#define NAC_DEFAULT_MAX_CONNECTIONS 64
#define NAC_DEFAULT_READ_TIMEOUT_MS 10000
#define NAC_DEFAULT_MAX_HEADER_BYTES 16384
#define NAC_DEFAULT_RATE_LIMIT_MAX 120
#define NAC_DEFAULT_RATE_LIMIT_WINDOW_SEC 60
#define NAC_DEFAULT_SHUTDOWN_TIMEOUT_MS 10000
#define NAC_DEFAULT_WORKERS 8
#define NAC_WORKERS_CEILING 256
#define NAC_DEFAULT_MAX_BODY_BYTES (1024 * 1024)
#define NAC_MAX_BODY_BYTES_CEILING (64 * 1024 * 1024)

#define NAC_HEADER_BYTES_CEILING 32768
#define NAC_MAX_CONNECTIONS_CEILING 4096

/* Bit flags recording which options were given on the command line. */
#define NAC_OPT_PORT        (1u << 0)
#define NAC_OPT_HOST        (1u << 1)
#define NAC_OPT_CONNECTIONS (1u << 2)
#define NAC_OPT_TIMEOUT     (1u << 3)
#define NAC_OPT_HEADER      (1u << 4)
#define NAC_OPT_RATE_MAX    (1u << 5)
#define NAC_OPT_RATE_WINDOW (1u << 6)
#define NAC_OPT_HEALTH      (1u << 7)
#define NAC_OPT_SHUTDOWN    (1u << 8)
#define NAC_OPT_WORKERS     (1u << 9)
#define NAC_OPT_MAX_BODY    (1u << 10)

typedef struct {
    int port;                 /* only meaningful when NAC_OPT_PORT is set */
    char host[256];
    int max_connections;
    int read_timeout_ms;
    int max_header_bytes;
    int rate_limit_max;       /* requests per window per IP; 0 disables */
    int rate_limit_window_sec;
    int health;               /* serve a built-in GET /health */
    int shutdown_timeout_ms;  /* how long a graceful shutdown waits for requests in flight */
    int workers;              /* worker threads that run requests (Unix; Windows keeps one thread per connection) */
    int max_body_bytes;       /* maximum request body size accepted by the HTTP server */
    unsigned flags_set;       /* NAC_OPT_* bits set by command-line flags */
} NacConfig;

/* Read-only once the server is accepting connections. */
extern NacConfig nac_cfg;

/* Tries to consume one runtime flag at argv[*i] ("--port 80" or
 * "--port=80"). Returns 1 and advances *i past it when consumed, 0 when
 * argv[*i] is not a NaC runtime flag (*i unchanged), and -1 on a missing or
 * invalid value (message in `error`). */
int nac_config_consume_flag(int argc, char **argv, int *i,
                            char *error, size_t error_cap);

/* For standalone executables built with `nac build`, where there is no
 * script name to separate NaC's flags from the program's own arguments:
 * removes every runtime flag from argv[1..] in place and updates *argc.
 * "--" stops the scan and is itself removed, so everything after it reaches
 * the program untouched. Returns 0, or -1 on an invalid flag value. */
int nac_config_extract_args(int *argc, char **argv, char *error, size_t error_cap);

/* Fills every option not set by a flag from its environment variable and
 * checks the final values. Called by serve(). Returns 0, or -1 on an
 * invalid value (message in `error`). */
int nac_config_apply_env(char *error, size_t error_cap);

/* One line per flag, for the usage text. */
const char *nac_config_usage(void);

#endif
