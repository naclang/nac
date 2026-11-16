#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) text++;
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }
    return text;
}

static int valid_key(const char *key) {
    if (!key[0] || !(isalpha((unsigned char)key[0]) || key[0] == '_')) return 0;
    for (const char *p = key + 1; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
    }
    return 1;
}

static int decode_value(char *raw, char *out, size_t out_cap) {
    raw = trim(raw);
    size_t used = 0;
    if (*raw == '"' || *raw == '\'') {
        char quote = *raw++;
        char *end = raw;
        while (*end) {
            if (quote == '"' && *end == '\\' && end[1]) {
                end += 2;
                continue;
            }
            if (*end == quote) break;
            end++;
        }
        if (!*end) return 0;
        *end = '\0';
        char *trailing = trim(end + 1);
        if (*trailing && *trailing != '#') return 0;
        for (char *p = raw; *p; p++) {
            char c = *p;
            if (quote == '"' && c == '\\' && p[1]) {
                p++;
                switch (*p) {
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case '\\': c = '\\'; break;
                    case '"': c = '"'; break;
                    default:
                        if (used + 2 >= out_cap) return 0;
                        out[used++] = '\\';
                        c = *p;
                        break;
                }
            }
            if (used + 1 >= out_cap) return 0;
            out[used++] = c;
        }
    } else {
        for (char *p = raw; *p; p++) {
            if (*p == '#' && (p == raw || isspace((unsigned char)p[-1]))) {
                *p = '\0';
                break;
            }
        }
        raw = trim(raw);
        used = strlen(raw);
        if (used >= out_cap) return 0;
        memcpy(out, raw, used);
    }
    out[used] = '\0';
    return 1;
}

static int set_if_missing(const char *key, const char *value) {
    if (getenv(key) != NULL) return 1;
#ifdef _WIN32
    return _putenv_s(key, value) == 0;
#else
    return setenv(key, value, 0) == 0;
#endif
}

int nac_load_dotenv(const char *path, char *error, size_t error_cap) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) return 0;
        if (error && error_cap) {
            snprintf(error, error_cap, "Could not open %s", path);
        }
        return -1;
    }

    char line[8192];
    int line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] != '\n' && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) {}
            if (error && error_cap) {
                snprintf(error, error_cap, "%s:%d: line exceeds 8191 bytes", path, line_number);
            }
            fclose(file);
            return -1;
        }
        if (line_number == 1 && len >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            memmove(line, line + 3, len - 2);
        }
        char *entry = trim(line);
        if (!*entry || *entry == '#') continue;
        if (strncmp(entry, "export", 6) == 0 &&
            isspace((unsigned char)entry[6])) {
            entry = trim(entry + 6);
        }

        char *equals = strchr(entry, '=');
        if (!equals) {
            if (error && error_cap) {
                snprintf(error, error_cap, "%s:%d: expected KEY=VALUE", path, line_number);
            }
            fclose(file);
            return -1;
        }
        *equals = '\0';
        char *key = trim(entry);
        if (!valid_key(key)) {
            if (error && error_cap) {
                snprintf(error, error_cap, "%s:%d: invalid environment variable name", path, line_number);
            }
            fclose(file);
            return -1;
        }

        char value[8192];
        if (!decode_value(equals + 1, value, sizeof(value))) {
            if (error && error_cap) {
                snprintf(error, error_cap, "%s:%d: invalid or oversized value", path, line_number);
            }
            fclose(file);
            return -1;
        }
        if (!set_if_missing(key, value)) {
            if (error && error_cap) {
                snprintf(error, error_cap, "%s:%d: could not set environment variable", path, line_number);
            }
            fclose(file);
            return -1;
        }
    }

    if (ferror(file)) {
        if (error && error_cap) {
            snprintf(error, error_cap, "Could not read %s", path);
        }
        fclose(file);
        return -1;
    }
    fclose(file);
    return 1;
}

/* ---- Runtime configuration -------------------------------------------- */

NacConfig nac_cfg = {
    0,                                   /* port: unset */
    NAC_DEFAULT_HOST,
    NAC_DEFAULT_MAX_CONNECTIONS,
    NAC_DEFAULT_READ_TIMEOUT_MS,
    NAC_DEFAULT_MAX_HEADER_BYTES,
    NAC_DEFAULT_RATE_LIMIT_MAX,
    NAC_DEFAULT_RATE_LIMIT_WINDOW_SEC,
    0,                                   /* health */
    NAC_DEFAULT_SHUTDOWN_TIMEOUT_MS,
    NAC_DEFAULT_WORKERS,
    NAC_DEFAULT_MAX_BODY_BYTES,
    0u                                   /* flags_set */
};

typedef enum { OPT_INT, OPT_STR, OPT_BOOL } OptKind;

typedef struct {
    const char *flag;     /* "--port" */
    const char *env;      /* NULL: no environment variable */
    unsigned bit;         /* NAC_OPT_* */
    OptKind kind;
    int *ival;            /* OPT_INT / OPT_BOOL target */
    char *sval;           /* OPT_STR target */
    size_t scap;
    int min, max;         /* OPT_INT range */
} OptSpec;

static const OptSpec g_opts[] = {
    { "--port", NULL, NAC_OPT_PORT, OPT_INT, &nac_cfg.port, NULL, 0, 1, 65535 },
    { "--host", "NAC_HOST", NAC_OPT_HOST, OPT_STR, NULL, nac_cfg.host, sizeof(nac_cfg.host), 0, 0 },
    { "--max-connections", "NAC_MAX_CONNECTIONS", NAC_OPT_CONNECTIONS, OPT_INT,
      &nac_cfg.max_connections, NULL, 0, 1, NAC_MAX_CONNECTIONS_CEILING },
    { "--read-timeout-ms", "NAC_READ_TIMEOUT_MS", NAC_OPT_TIMEOUT, OPT_INT,
      &nac_cfg.read_timeout_ms, NULL, 0, 100, 600000 },
    { "--max-header-bytes", "NAC_MAX_HEADER_BYTES", NAC_OPT_HEADER, OPT_INT,
      &nac_cfg.max_header_bytes, NULL, 0, 1024, NAC_HEADER_BYTES_CEILING },
    { "--rate-limit", "NAC_RATE_LIMIT_MAX", NAC_OPT_RATE_MAX, OPT_INT,
      &nac_cfg.rate_limit_max, NULL, 0, 0, 1000000 },
    { "--rate-window-sec", "NAC_RATE_LIMIT_WINDOW_SEC", NAC_OPT_RATE_WINDOW, OPT_INT,
      &nac_cfg.rate_limit_window_sec, NULL, 0, 1, 86400 },
    { "--health", "NAC_HEALTH", NAC_OPT_HEALTH, OPT_BOOL, &nac_cfg.health, NULL, 0, 0, 1 },
    { "--shutdown-timeout-ms", "NAC_SHUTDOWN_TIMEOUT_MS", NAC_OPT_SHUTDOWN, OPT_INT,
      &nac_cfg.shutdown_timeout_ms, NULL, 0, 0, 600000 },
    { "--workers", "NAC_WORKERS", NAC_OPT_WORKERS, OPT_INT,
      &nac_cfg.workers, NULL, 0, 1, NAC_WORKERS_CEILING },
    { "--max-body-bytes", "NAC_MAX_BODY_BYTES", NAC_OPT_MAX_BODY, OPT_INT,
      &nac_cfg.max_body_bytes, NULL, 0, 0, NAC_MAX_BODY_BYTES_CEILING },
};

#define OPT_COUNT ((int)(sizeof(g_opts) / sizeof(g_opts[0])))

static void set_error(char *error, size_t cap, const char *fmt, const char *a, const char *b) {
    if (error && cap) snprintf(error, cap, fmt, a, b);
}

static int ci_equals(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int parse_bool_text(const char *text, int *out) {
    if (ci_equals(text, "1") || ci_equals(text, "true") || ci_equals(text, "yes") ||
        ci_equals(text, "on")) {
        *out = 1;
        return 1;
    }
    if (ci_equals(text, "0") || ci_equals(text, "false") || ci_equals(text, "no") ||
        ci_equals(text, "off") || text[0] == '\0') {
        *out = 0;
        return 1;
    }
    return 0;
}

/* Validates `text` for option `o` and stores it. `origin` names the source
 * ("--port" or "NAC_HOST") for error messages. */
static int store_option(const OptSpec *o, const char *text, const char *origin,
                        char *error, size_t error_cap) {
    if (o->kind == OPT_INT) {
        char *end = NULL;
        errno = 0;
        long v = strtol(text, &end, 10);
        if (errno || end == text || *end != '\0' || v < o->min || v > o->max) {
            char range[64];
            snprintf(range, sizeof(range), "%d to %d", o->min, o->max);
            if (error && error_cap) {
                snprintf(error, error_cap, "%s: expected an integer from %s, got '%s'",
                         origin, range, text);
            }
            return 0;
        }
        *o->ival = (int)v;
        return 1;
    }

    if (o->kind == OPT_BOOL) {
        int v = 0;
        if (!parse_bool_text(text, &v)) {
            set_error(error, error_cap, "%s: expected true/false, got '%s'", origin, text);
            return 0;
        }
        *o->ival = v;
        return 1;
    }

    /* OPT_STR */
    size_t len = strlen(text);
    if (len == 0 || len >= o->scap) {
        set_error(error, error_cap, "%s: expected a host name or IP address, got '%s'", origin, text);
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char)text[i])) {
            set_error(error, error_cap, "%s: expected a host name or IP address, got '%s'", origin, text);
            return 0;
        }
    }
    memcpy(o->sval, text, len + 1);
    return 1;
}

int nac_config_consume_flag(int argc, char **argv, int *i,
                            char *error, size_t error_cap) {
    const char *arg = argv[*i];
    if (!arg || arg[0] != '-' || arg[1] != '-') return 0;

    for (int k = 0; k < OPT_COUNT; k++) {
        const OptSpec *o = &g_opts[k];
        size_t flen = strlen(o->flag);
        if (strncmp(arg, o->flag, flen) != 0) continue;

        const char *value = NULL;
        int consumed = 1;
        if (arg[flen] == '=') {
            value = arg + flen + 1;
        } else if (arg[flen] == '\0') {
            if (o->kind == OPT_BOOL) {
                value = "1";          /* bare --health */
            } else if (*i + 1 < argc) {
                value = argv[*i + 1];
                consumed = 2;
            } else {
                set_error(error, error_cap, "%s requires a value%s", o->flag, "");
                return -1;
            }
        } else {
            continue;                  /* e.g. "--port-x": a different name */
        }

        if (!store_option(o, value, o->flag, error, error_cap)) return -1;
        nac_cfg.flags_set |= o->bit;
        *i += consumed;
        return 1;
    }
    return 0;
}

int nac_config_extract_args(int *argc, char **argv, char *error, size_t error_cap) {
    int w = 1;
    int i = 1;
    while (i < *argc) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            while (i < *argc) argv[w++] = argv[i++];
            break;
        }
        int rc = nac_config_consume_flag(*argc, argv, &i, error, error_cap);
        if (rc < 0) return -1;
        if (rc == 0) argv[w++] = argv[i++];
    }
    argv[w] = NULL;
    *argc = w;
    return 0;
}

int nac_config_apply_env(char *error, size_t error_cap) {
    for (int k = 0; k < OPT_COUNT; k++) {
        const OptSpec *o = &g_opts[k];
        if (!o->env || (nac_cfg.flags_set & o->bit)) continue;

        const char *value = getenv(o->env);
        if (!value || !value[0]) continue;
        if (!store_option(o, value, o->env, error, error_cap)) return -1;
    }
    return 0;
}

const char *nac_config_usage(void) {
    return
        "  --port N                 listen port (overrides serve(port) and PORT)\n"
        "  --host ADDR              listen address, default 0.0.0.0 (NAC_HOST)\n"
        "  --max-connections N      concurrent connections, default 64 (NAC_MAX_CONNECTIONS)\n"
        "  --read-timeout-ms N      whole-request deadline, default 10000 (NAC_READ_TIMEOUT_MS)\n"
        "  --max-header-bytes N     header size limit, default 16384, max 32768 (NAC_MAX_HEADER_BYTES)\n"
        "  --rate-limit N           requests per IP per window, default 120, 0 = off (NAC_RATE_LIMIT_MAX)\n"
        "  --rate-window-sec N      rate-limit window, default 60 (NAC_RATE_LIMIT_WINDOW_SEC)\n"
        "  --health                 answer GET /health with {\"status\":\"ok\"} (NAC_HEALTH)\n"
        "  --shutdown-timeout-ms N  wait for requests in flight on SIGINT/SIGTERM, default 10000, 0 = don't wait (NAC_SHUTDOWN_TIMEOUT_MS)\n"
        "  --workers N              worker threads running requests, default 8, max 256 (NAC_WORKERS)\n"
        "  --max-body-bytes N       maximum request body size, default 1048576, max 67108864 (NAC_MAX_BODY_BYTES)\n";
}
