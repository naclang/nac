#include "server_common.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../builtin/builtin.h"
#include "../runtime/eval.h"
#include "../runtime/json.h"
#include "router.h"

#if defined(_MSC_VER)
#define NAC_THREADLOCAL __declspec(thread)
#else
#define NAC_THREADLOCAL __thread
#endif

#define MAX_SET_COOKIES 16
#define MAX_SET_COOKIE_LINE 1024

static NAC_THREADLOCAL char g_cookie_lines[MAX_SET_COOKIES][MAX_SET_COOKIE_LINE];
static NAC_THREADLOCAL int g_cookie_count = 0;

/* Always emitted by the dynamic and static response builders. */
#define NAC_SECURITY_HEADERS \
    "X-Content-Type-Options: nosniff\r\n" \
    "X-Frame-Options: DENY\r\n" \
    "Referrer-Policy: no-referrer\r\n"

static int has_header_break(const char *s) {
    for (; *s; s++) {
        if (*s == '\r' || *s == '\n') return 1;
    }
    return 0;
}

/* Appends one header line to `dst`, silently dropping it if it would
 * overflow. Prevents a script-supplied header from injecting extra
 * response lines. */
static void append_header_line(char *dst, size_t dst_size, const char *key,
                               const char *value) {
    if (!key || !value || has_header_break(key) || has_header_break(value)) return;

    char line[1600];
    int n = snprintf(line, sizeof(line), "%s: %s\r\n", key, value);
    if (n < 0 || n >= (int)sizeof(line)) return;

    size_t used = strlen(dst);
    if (used + (size_t)n < dst_size) {
        memcpy(dst + used, line, (size_t)n + 1);
    }
}

static void url_decode_into(const char *src, char *dst, int dst_size) {
    int out_i = 0;
    for (int i = 0; src[i] && out_i < dst_size - 1; i++) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
            char hex[3] = { src[i + 1], src[i + 2], '\0' };
            dst[out_i++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[out_i++] = ' ';
        } else {
            dst[out_i++] = src[i];
        }
    }
    dst[out_i] = '\0';
}

static void parse_query_into_map(const char *query, Value *map) {
    if (!query || !*query) return;

    const char *start = query;
    while (*start) {
        const char *sep = strchr(start, '&');
        int len = sep ? (int)(sep - start) : (int)strlen(start);

        char pair[2048];
        if (len >= (int)sizeof(pair)) len = (int)sizeof(pair) - 1;
        memcpy(pair, start, len);
        pair[len] = '\0';

        if (pair[0] != '\0') {
            char *eq = strchr(pair, '=');
            char decoded_key[512];
            char decoded_val[1536];

            if (eq) {
                *eq = '\0';
                url_decode_into(pair, decoded_key, sizeof(decoded_key));
                url_decode_into(eq + 1, decoded_val, sizeof(decoded_val));
            } else {
                url_decode_into(pair, decoded_key, sizeof(decoded_key));
                decoded_val[0] = '\0';
            }

            map_set(map, decoded_key, make_string(decoded_val));
        }

        if (!sep) break;
        start = sep + 1;
    }
}

/* Parses a "\r\n"-separated block of "Key: Value" header lines. */
static void parse_headers_into_map(const char *block, Value *map, int *content_length) {
    const char *start = block;
    while (*start) {
        const char *sep = strstr(start, "\r\n");
        int len = sep ? (int)(sep - start) : (int)strlen(start);

        char line[1024];
        if (len >= (int)sizeof(line)) len = (int)sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';

        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *val = colon + 1;
            while (*val == ' ') val++;

            char lower_key[256];
            int i = 0;
            for (; line[i] && i < 255; i++) lower_key[i] = (char)tolower((unsigned char)line[i]);
            lower_key[i] = '\0';

            map_set(map, lower_key, make_string(val));

            if (strcmp(lower_key, "content-length") == 0) {
                char *end = NULL;
                long parsed = strtol(val, &end, 10);
                if (end == val || parsed < 0) {
                    *content_length = -1;
                } else if (parsed > INT_MAX) {
                    *content_length = INT_MAX;
                } else {
                    *content_length = (int)parsed;
                }
            }
        }

        if (!sep) break;
        start = sep + 2;
    }
}

/* Parses a Cookie: header ("a=1; b=2") into a name -> value map. Values are
 * kept raw (not URL-decoded); use urlDecode() if an application needs it. */
static void parse_cookies_into_map(const char *header, Value *map) {
    const char *p = header;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';') p++;
        if (!*p) break;

        const char *name_start = p;
        while (*p && *p != '=' && *p != ';') p++;
        const char *name_end = p;
        while (name_end > name_start && (name_end[-1] == ' ' || name_end[-1] == '\t')) {
            name_end--;
        }

        const char *value_start = "";
        const char *value_end = value_start;
        if (*p == '=') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            value_start = p;
            while (*p && *p != ';') p++;
            value_end = p;
            while (value_end > value_start &&
                   (value_end[-1] == ' ' || value_end[-1] == '\t')) {
                value_end--;
            }
            if (value_end - value_start >= 2 && value_start[0] == '"' &&
                value_end[-1] == '"') {
                value_start++;
                value_end--;
            }
        }

        int name_len = (int)(name_end - name_start);
        int value_len = (int)(value_end - value_start);
        if (name_len > 0) {
            char name[256];
            char value[4096];
            if (name_len >= (int)sizeof(name)) name_len = (int)sizeof(name) - 1;
            if (value_len >= (int)sizeof(value)) value_len = (int)sizeof(value) - 1;
            memcpy(name, name_start, (size_t)name_len);
            name[name_len] = '\0';
            memcpy(value, value_start, (size_t)value_len);
            value[value_len] = '\0';
            map_set(map, name, make_string(value));
        }

        if (*p == ';') p++;
    }
}

void nac_http_parse_request(const char *raw, int header_end, NacHttpRequest *req) {
    memset(req->method, 0, sizeof(req->method));
    memset(req->path, 0, sizeof(req->path));
    memset(req->query_raw, 0, sizeof(req->query_raw));
    req->query = make_map();
    req->headers = make_map();
    req->cookie = make_map();
    req->body[0] = '\0';
    req->content_length = 0;
    req->client_ip[0] = '\0';

    char full_path[2048] = "/";
    sscanf(raw, "%15s %2047s", req->method, full_path);

    char *qmark = strchr(full_path, '?');
    if (qmark) {
        *qmark = '\0';
        strncpy(req->query_raw, qmark + 1, sizeof(req->query_raw) - 1);
    }
    strncpy(req->path, full_path, sizeof(req->path) - 1);

    parse_query_into_map(req->query_raw, &req->query);

    /* Header block: everything between the end of the request line and
     * header_end (which points just past the terminating "\r\n\r\n"). */
    const char *headers_start = strstr(raw, "\r\n");
    headers_start = headers_start ? headers_start + 2 : raw + strlen(raw);

    int hs_len = (int)((raw + header_end) - headers_start);
    if (hs_len < 0) hs_len = 0;

    char header_section[16384];
    if (hs_len >= (int)sizeof(header_section)) hs_len = (int)sizeof(header_section) - 1;
    memcpy(header_section, headers_start, hs_len);
    header_section[hs_len] = '\0';

    parse_headers_into_map(header_section, &req->headers, &req->content_length);

    Value *cookie_header = map_get(&req->headers, "cookie");
    if (cookie_header && cookie_header->type == TYPE_STRING) {
        parse_cookies_into_map(cookie_header->str_val, &req->cookie);
    }
}

void nac_http_free_request(NacHttpRequest *req) {
    free_value(&req->query);
    free_value(&req->headers);
    free_value(&req->cookie);
}

static const char *status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

int nac_http_build_response(int found_handler, const char *handler_name, Value result,
                             char *out, int out_cap, int *out_status) {
    int status = 200;
    char content_type[128] = "text/plain; charset=utf-8";
    char body[MAX_STRING_LEN] = "";
    char extra_header_lines[4096] = "";
    char set_cookie_lines[4096] = "";

    if (!found_handler) {
        status = 500;
        snprintf(body, sizeof(body), "NaC HTTP server: handler function '%s' not found", handler_name);
    } else if (result.type == TYPE_MAP) {
        Value *status_v = map_get(&result, "status");
        if (status_v) status = to_int(*status_v);

        Value *body_v = map_get(&result, "body");
        if (body_v) {
            if (body_v->type == TYPE_STRING) {
                strncpy(body, body_v->str_val, sizeof(body) - 1);
            } else if (body_v->type == TYPE_MAP || body_v->type == TYPE_ARRAY) {
                char *json = json_stringify_value(*body_v);
                if (json) {
                    strncpy(body, json, sizeof(body) - 1);
                    free(json);
                    strncpy(content_type, "application/json", sizeof(content_type) - 1);
                }
            } else {
                value_to_display_string(*body_v, body, sizeof(body));
            }
        }

        Value *headers_v = map_get(&result, "headers");
        if (headers_v && headers_v->type == TYPE_MAP) {
            for (int i = 0; i < headers_v->map_val.size; i++) {
                const char *key = headers_v->map_val.keys[i];
                char val_str[MAX_STRING_LEN];
                value_to_display_string(headers_v->map_val.values[i], val_str, sizeof(val_str));

                char lower_key[256];
                int li = 0;
                for (; key[li] && li < 255; li++) lower_key[li] = (char)tolower((unsigned char)key[li]);
                lower_key[li] = '\0';

                if (strcmp(lower_key, "content-type") == 0) {
                    if (!has_header_break(val_str)) {
                        strncpy(content_type, val_str, sizeof(content_type) - 1);
                        content_type[sizeof(content_type) - 1] = '\0';
                    }
                    continue;
                }

                append_header_line(extra_header_lines, sizeof(extra_header_lines), key, val_str);
            }
        }
    } else if (result.type == TYPE_STRING) {
        strncpy(body, result.str_val, sizeof(body) - 1);
    } else {
        value_to_display_string(result, body, sizeof(body));
    }

    /* Web-first default: a plain string that looks like markup is HTML. */
    if (strcmp(content_type, "text/plain; charset=utf-8") == 0) {
        const char *b = body;
        while (*b && isspace((unsigned char)*b)) b++;
        if (*b == '<') {
            strncpy(content_type, "text/html; charset=utf-8", sizeof(content_type) - 1);
        }
    }

    for (int i = 0; i < g_cookie_count; i++) {
        append_header_line(set_cookie_lines, sizeof(set_cookie_lines),
                           "Set-Cookie", g_cookie_lines[i]);
    }

    if (out_status) *out_status = status;

    int written = snprintf(out, out_cap,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        NAC_SECURITY_HEADERS
        "%s"
        "%s"
        "\r\n"
        "%s",
        status, status_text(status), content_type, (int)strlen(body),
        extra_header_lines, set_cookie_lines, body);

    if (written < 0 || written >= out_cap) return -1;
    return written;
}

int nac_http_build_error_response(int status, const char *message,
                                  char *out, int out_cap) {
    Value resp = make_map();
    map_set(&resp, "status", make_int(status));
    map_set(&resp, "body", make_string(message ? message : ""));
    int ignored = 0;
    int written = nac_http_build_response(1, "", resp, out, out_cap, &ignored);
    free_value(&resp);
    return written;
}

static Value make_status_map(int status, const char *body) {
    Value m = make_map();
    map_set(&m, "status", make_int(status));
    map_set(&m, "body", make_string(body));
    return m;
}

/* --- Set-Cookie accumulator ------------------------------------------- */

void nac_cookie_reset(void) {
    g_cookie_count = 0;
}

int nac_cookie_count(void) {
    return g_cookie_count;
}

const char *nac_cookie_line(int index) {
    if (index < 0 || index >= g_cookie_count) return NULL;
    return g_cookie_lines[index];
}

static int append_cookie_attr(char *line, int used, const char *fmt, const char *value) {
    int room = MAX_SET_COOKIE_LINE - used;
    if (room <= 0) return -1;
    int n = snprintf(line + used, (size_t)room, fmt, value);
    if (n < 0 || n >= room) return -1;
    return used + n;
}

int nac_cookie_add(const char *name, const char *value,
                   const char *path, int max_age, const char *expires,
                   int http_only, int secure, const char *same_site,
                   const char *domain) {
    if (!name || !*name || !value) return -1;
    if (g_cookie_count >= MAX_SET_COOKIES) return -1;

    /* Defensive validation: the builtin already checks these, but the
     * response builder must never emit a header line break. */
    if (has_header_break(name) || has_header_break(value) ||
        strchr(name, ';') || strchr(value, ';')) {
        return -1;
    }
    if ((path && has_header_break(path)) ||
        (domain && has_header_break(domain)) ||
        (expires && has_header_break(expires)) ||
        (same_site && has_header_break(same_site))) {
        return -1;
    }

    char *line = g_cookie_lines[g_cookie_count];
    int used = snprintf(line, MAX_SET_COOKIE_LINE, "%s=%s", name, value);
    if (used < 0 || used >= MAX_SET_COOKIE_LINE) return -1;

    if (path && path[0]) {
        used = append_cookie_attr(line, used, "; Path=%s", path);
        if (used < 0) return -1;
    }
    if (max_age >= 0) {
        char age[32];
        snprintf(age, sizeof(age), "%d", max_age);
        used = append_cookie_attr(line, used, "; Max-Age=%s", age);
        if (used < 0) return -1;
    }
    if (expires && expires[0]) {
        used = append_cookie_attr(line, used, "; Expires=%s", expires);
        if (used < 0) return -1;
    }
    if (domain && domain[0]) {
        used = append_cookie_attr(line, used, "; Domain=%s", domain);
        if (used < 0) return -1;
    }
    if (http_only) {
        used = append_cookie_attr(line, used, "%s", "; HttpOnly");
        if (used < 0) return -1;
    }
    if (secure) {
        used = append_cookie_attr(line, used, "%s", "; Secure");
        if (used < 0) return -1;
    }
    if (same_site && same_site[0]) {
        used = append_cookie_attr(line, used, "; SameSite=%s", same_site);
        if (used < 0) return -1;
    }

    g_cookie_count++;
    return 0;
}

/* --- Per-IP rate limiter ---------------------------------------------- */

typedef struct {
    char ip[64];
    time_t window_start;
    int count;
} RateEntry;

#define RATE_TABLE_SIZE 1024
static RateEntry g_rate_table[RATE_TABLE_SIZE];
static int g_rate_count = 0;

int nac_rate_limit_allow(const char *ip, NacLockFn lock, NacLockFn unlock,
                         int *retry_after) {
    if (!ip || !*ip) return 1;
    if (retry_after) *retry_after = 1;

    if (lock) lock();
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        if (unlock) unlock();
        return 1;
    }

    RateEntry *entry = NULL;
    for (int i = 0; i < g_rate_count; i++) {
        if (strncmp(g_rate_table[i].ip, ip, sizeof(g_rate_table[i].ip)) == 0) {
            entry = &g_rate_table[i];
            break;
        }
    }

    if (!entry) {
        if (g_rate_count < RATE_TABLE_SIZE) {
            entry = &g_rate_table[g_rate_count++];
        } else {
            /* Table full: reuse the oldest window (bounded memory, no
             * unbounded attacker-controlled allocation). */
            entry = &g_rate_table[0];
            for (int i = 1; i < g_rate_count; i++) {
                if (g_rate_table[i].window_start < entry->window_start) {
                    entry = &g_rate_table[i];
                }
            }
        }
        strncpy(entry->ip, ip, sizeof(entry->ip) - 1);
        entry->ip[sizeof(entry->ip) - 1] = '\0';
        entry->window_start = now;
        entry->count = 0;
    }

    if (now - entry->window_start >= NAC_RATE_LIMIT_WINDOW_SEC) {
        entry->window_start = now;
        entry->count = 0;
    }

    entry->count++;
    int allowed = entry->count <= NAC_RATE_LIMIT_MAX;
    if (!allowed && retry_after) {
        long remaining = (long)(entry->window_start + NAC_RATE_LIMIT_WINDOW_SEC - now);
        if (remaining < 1) remaining = 1;
        if (remaining > NAC_RATE_LIMIT_WINDOW_SEC) remaining = NAC_RATE_LIMIT_WINDOW_SEC;
        *retry_after = (int)remaining;
    }

    if (unlock) unlock();
    return allowed;
}

int nac_dispatch_request(NacHttpRequest *req, const char *handler_name,
                         NacLockFn lock, NacLockFn unlock,
                         Value *result, char **raw_out, int *raw_len, int *status) {
    *raw_out = NULL;
    *raw_len = 0;
    nac_cookie_reset();

    int retry_after = 1;
    if (!nac_rate_limit_allow(req->client_ip, lock, unlock, &retry_after)) {
        Value m = make_status_map(429, "429 Too Many Requests");
        Value headers = make_map();
        char retry[32];
        snprintf(retry, sizeof(retry), "%d", retry_after);
        map_set(&headers, "retry-after", make_string(retry));
        map_set(&m, "headers", headers);
        free_value(&headers);
        *result = m;
        return 0;
    }

    char fn[MAX_TOKEN_LEN];
    int path_matched = 0;
    Value params;

    if (router_match(req->method, req->path, fn, sizeof(fn), &params, &path_matched)) {
        Value args[7];
        args[0] = make_string(req->method);
        args[1] = make_string(req->path);
        args[2] = req->query;
        args[3] = req->headers;
        args[4] = make_string(req->body);
        args[5] = params;
        args[6] = req->cookie;

        lock();
        int found = call_nac_function(fn, args, 7, result);
        unlock();
        free_value(&params);

        if (!found) *result = make_status_map(500, "NaC HTTP server: route function not found");
        return 0;
    }

    char *raw = static_serve(req->method, req->path, raw_len, status);
    if (raw) {
        *raw_out = raw;
        return 1;
    }

    if (handler_name && handler_name[0]) {
        Value args[5];
        args[0] = make_string(req->method);
        args[1] = make_string(req->path);
        args[2] = req->query;
        args[3] = req->headers;
        args[4] = make_string(req->body);

        lock();
        int found = call_nac_function(handler_name, args, 5, result);
        unlock();

        if (!found) {
            char msg[512];
            snprintf(msg, sizeof(msg), "NaC HTTP server: handler function '%s' not found", handler_name);
            *result = make_status_map(500, msg);
        }
        return 0;
    }

    char msg[1200];
    if (path_matched) {
        snprintf(msg, sizeof(msg), "405 Method Not Allowed: %s %s", req->method, req->path);
        *result = make_status_map(405, msg);
    } else {
        snprintf(msg, sizeof(msg), "404 Not Found: %s", req->path);
        *result = make_status_map(404, msg);
    }
    return 0;
}
