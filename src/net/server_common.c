#include "server_common.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../builtin/builtin.h"
#include "../core/interpreter.h"
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
    req->http_minor = 0;
    req->client_ip[0] = '\0';

    char full_path[2048] = "/";
    sscanf(raw, "%15s %2047s HTTP/1.%d", req->method, full_path, &req->http_minor);

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

    char header_section[NAC_HEADER_BYTES_CEILING];
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

static int ci_equal_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    }
    return 1;
}

/* True when a comma-separated header value contains `token` (case-insensitive). */
static int header_list_has_token(const char *list, const char *token) {
    size_t token_len = strlen(token);
    const char *p = list;
    while (*p) {
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        const char *start = p;
        while (*p && *p != ',') p++;
        const char *end = p;
        while (end > start && isspace((unsigned char)end[-1])) end--;

        if ((size_t)(end - start) == token_len && ci_equal_n(start, token, token_len)) {
            return 1;
        }
    }
    return 0;
}

int nac_http_request_keep_alive(const NacHttpRequest *req) {
    if (!req) return 0;

    Value *conn = map_get((Value*)&req->headers, "connection");
    int close_requested = 0;
    int keep_requested = 0;
    if (conn && conn->type == TYPE_STRING) {
        close_requested = header_list_has_token(conn->str_val, "close");
        keep_requested = header_list_has_token(conn->str_val, "keep-alive");
    }

    if (close_requested) return 0;
    if (req->http_minor >= 1) return 1; /* HTTP/1.1 and later default to persistent */
    return keep_requested;              /* HTTP/1.0 defaults to close */
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
                             int keep_alive, int is_head, char *out, int out_cap,
                             int *out_status) {
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

                /* Connection persistence is owned by the server. */
                if (strcmp(lower_key, "connection") == 0) continue;

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

    /* 1xx/204/205/304 never carry a message body. HEAD carries the headers
     * (including Content-Length) of the equivalent GET, but no body. */
    int status_allows_body = !(status == 204 || status == 205 || status == 304 ||
                               (status >= 100 && status < 200));
    char content_length_line[64] = "";
    if (status_allows_body) {
        snprintf(content_length_line, sizeof(content_length_line),
                 "Content-Length: %d\r\n", (int)strlen(body));
    }
    const char *body_out = (status_allows_body && !is_head) ? body : "";

    int written = snprintf(out, out_cap,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "%s"
        "Connection: %s\r\n"
        NAC_SECURITY_HEADERS
        "%s"
        "%s"
        "\r\n"
        "%s",
        status, status_text(status), content_type, content_length_line,
        keep_alive ? "keep-alive" : "close",
        extra_header_lines, set_cookie_lines, body_out);

    if (written < 0 || written >= out_cap) return -1;
    return written;
}

int nac_http_build_error_response(int status, const char *message,
                                  char *out, int out_cap) {
    Value resp = make_map();
    map_set(&resp, "status", make_int(status));
    map_set(&resp, "body", make_string(message ? message : ""));
    int ignored = 0;
    int written = nac_http_build_response(1, "", resp, 0, 0, out, out_cap, &ignored);
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

/* --- Extra response headers (setHeader) -------------------------------- */

#define MAX_EXTRA_HEADERS 24
#define MAX_EXTRA_HEADER_NAME 128
#define MAX_EXTRA_HEADER_VALUE 1024

static NAC_THREADLOCAL char g_hdr_names[MAX_EXTRA_HEADERS][MAX_EXTRA_HEADER_NAME];
static NAC_THREADLOCAL char g_hdr_values[MAX_EXTRA_HEADERS][MAX_EXTRA_HEADER_VALUE];
static NAC_THREADLOCAL int g_hdr_count = 0;

static int ci_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int valid_header_name(const char *name) {
    if (!name[0]) return 0;
    for (const char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || strchr("!#$%&'*+-.^_`|~", c))) return 0;
    }
    return 1;
}

void nac_header_reset(void) {
    g_hdr_count = 0;
}

int nac_header_set(const char *name, const char *value) {
    if (!name || !value || !valid_header_name(name) || has_header_break(value)) return -1;
    if (strlen(name) >= MAX_EXTRA_HEADER_NAME || strlen(value) >= MAX_EXTRA_HEADER_VALUE) return -1;

    for (int i = 0; i < g_hdr_count; i++) {
        if (ci_equal(g_hdr_names[i], name)) {
            strcpy(g_hdr_names[i], name);
            strcpy(g_hdr_values[i], value);
            return 0;
        }
    }
    if (g_hdr_count >= MAX_EXTRA_HEADERS) return -1;
    strcpy(g_hdr_names[g_hdr_count], name);
    strcpy(g_hdr_values[g_hdr_count], value);
    g_hdr_count++;
    return 0;
}

/* A handler result (map, string or anything else) as the full
 * { status, body, headers } map that `after` blocks receive. */
static Value response_from_result(Value result) {
    Value response = make_map();
    if (result.type == TYPE_MAP) {
        Value *status_v = map_get(&result, "status");
        Value *body_v = map_get(&result, "body");
        Value *headers_v = map_get(&result, "headers");
        map_set(&response, "status", status_v ? make_int(to_int(*status_v)) : make_int(200));
        if (body_v) {
            map_set(&response, "body", *body_v);
        } else {
            map_set(&response, "body", make_string(""));
        }
        if (headers_v && headers_v->type == TYPE_MAP) {
            map_set(&response, "headers", *headers_v);
        } else {
            Value empty = make_map();
            map_set(&response, "headers", empty);
            free_value(&empty);
        }
        return response;
    }

    map_set(&response, "status", make_int(200));
    if (result.type == TYPE_STRING) {
        map_set(&response, "body", result);
    } else {
        char text[MAX_STRING_LEN];
        value_to_display_string(result, text, sizeof(text));
        map_set(&response, "body", make_string(text));
    }
    Value empty = make_map();
    map_set(&response, "headers", empty);
    free_value(&empty);
    return response;
}

/* Adds the setHeader() headers to a dynamic result, which becomes a full
 * response map if it was a plain string. */
static void apply_headers_to_result(Value *result) {
    if (g_hdr_count == 0) return;
    if (result->type != TYPE_MAP) {
        Value full = response_from_result(*result);
        free_value(result);
        *result = full;
    }

    Value *headers = map_get(result, "headers");
    if (!headers || headers->type != TYPE_MAP) {
        Value empty = make_map();
        map_set(result, "headers", empty);
        free_value(&empty);
        headers = map_get(result, "headers");
    }

    for (int i = 0; i < g_hdr_count; i++) {
        /* Reuse the spelling of an existing header with the same name so
         * that the response never carries it twice. */
        const char *key = g_hdr_names[i];
        for (int k = 0; k < headers->map_val.size; k++) {
            if (ci_equal(headers->map_val.keys[k], g_hdr_names[i])) {
                key = headers->map_val.keys[k];
                break;
            }
        }
        char key_copy[MAX_EXTRA_HEADER_NAME];
        strncpy(key_copy, key, sizeof(key_copy) - 1);
        key_copy[sizeof(key_copy) - 1] = '\0';
        map_set(headers, key_copy, make_string(g_hdr_values[i]));
    }
}

/* Same for a complete raw HTTP response (a static file): rewrites the header
 * block with the setHeader() headers, replacing same-named ones. */
static void apply_headers_to_raw(char **raw, int *raw_len) {
    if (g_hdr_count == 0 || !*raw) return;
    char *buf = *raw;
    int len = *raw_len;

    int header_end = -1;
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            header_end = i;
            break;
        }
    }
    if (header_end < 0) return;

    size_t extra = 0;
    for (int i = 0; i < g_hdr_count; i++) {
        if (ci_equal(g_hdr_names[i], "connection")) continue;
        extra += strlen(g_hdr_names[i]) + strlen(g_hdr_values[i]) + 4;
    }
    char *out = (char*)malloc((size_t)len + extra + 1);
    if (!out) return;

    int o = 0;
    int pos = 0;
    int first = 1;
    while (pos <= header_end) {
        int eol = pos;
        while (eol < header_end && !(buf[eol] == '\r' && buf[eol + 1] == '\n')) eol++;
        int line_len = eol - pos;

        int drop = 0;
        if (!first) {
            for (int i = 0; i < g_hdr_count && !drop; i++) {
                if (ci_equal(g_hdr_names[i], "connection")) continue;
                size_t nlen = strlen(g_hdr_names[i]);
                if ((size_t)line_len > nlen && buf[pos + nlen] == ':') {
                    char head[MAX_EXTRA_HEADER_NAME];
                    memcpy(head, buf + pos, nlen);
                    head[nlen] = '\0';
                    if (ci_equal(head, g_hdr_names[i])) drop = 1;
                }
            }
        }
        if (!drop) {
            memcpy(out + o, buf + pos, (size_t)line_len);
            o += line_len;
            out[o++] = '\r';
            out[o++] = '\n';
        }
        first = 0;
        pos = eol + 2;
    }

    for (int i = 0; i < g_hdr_count; i++) {
        if (ci_equal(g_hdr_names[i], "connection")) continue;
        o += sprintf(out + o, "%s: %s\r\n", g_hdr_names[i], g_hdr_values[i]);
    }
    out[o++] = '\r';
    out[o++] = '\n';
    memcpy(out + o, buf + header_end + 4, (size_t)(len - header_end - 4));
    o += len - header_end - 4;
    out[o] = '\0';

    free(buf);
    *raw = out;
    *raw_len = o;
}

/* --- Middleware: before / after blocks ---------------------------------- */

static int call_middleware(const char *fn, const NacHttpRequest *req, Value params,
                           const Value *response, Value *out) {
    Value args[8];
    args[0] = make_string(req->method);
    args[1] = make_string(req->path);
    args[2] = req->query;
    args[3] = req->headers;
    args[4] = make_string(req->body);
    args[5] = params;
    args[6] = req->cookie;
    int argc = 7;
    if (response) {
        args[7] = *response;
        argc = 8;
    }
    /* A block that ends without `return` must read as null, not as whatever
     * the previous call returned. */
    return_value = make_null();
    return call_nac_function(fn, args, argc, out);
}

int nac_run_before(const NacHttpRequest *req, Value params, Value *answer) {
    int cursor = 0;
    char fn[MAX_TOKEN_LEN];
    while (middleware_next(MW_BEFORE, &cursor, req->path, fn, sizeof(fn))) {
        Value out = make_null();
        if (!call_middleware(fn, req, params, NULL, &out)) continue;
        if (out.type != TYPE_NULL) {
            *answer = out;
            return 1;
        }
        free_value(&out);
    }
    return 0;
}

void nac_finish_response(const NacHttpRequest *req, Value params,
                         Value *result, char **raw, int *raw_len, int *status) {
    int cursor = 0;
    char fn[MAX_TOKEN_LEN];

    if (middleware_next(MW_AFTER, &cursor, req->path, fn, sizeof(fn))) {
        int is_raw = raw && *raw;
        Value current;
        if (is_raw) {
            current = make_map();
            map_set(&current, "status", make_int(status ? *status : 200));
            map_set(&current, "body", make_string(""));
            Value empty = make_map();
            map_set(&current, "headers", empty);
            free_value(&empty);
        } else {
            current = response_from_result(*result);
        }
        apply_headers_to_result(&current);   /* response.headers shows setHeader() so far */

        cursor = 0;
        while (middleware_next(MW_AFTER, &cursor, req->path, fn, sizeof(fn))) {
            Value out = make_null();
            if (!call_middleware(fn, req, params, &current, &out)) continue;
            if (out.type == TYPE_NULL) {
                free_value(&out);
                continue;
            }
            Value replacement = response_from_result(out);
            free_value(&out);
            free_value(&current);
            current = replacement;
            if (is_raw) {
                free(*raw);
                *raw = NULL;
                if (raw_len) *raw_len = 0;
                is_raw = 0;
            }
            apply_headers_to_result(&current);
        }

        if (is_raw) {
            free_value(&current);
        } else {
            free_value(result);
            *result = current;
        }
    }

    if (raw && *raw) {
        apply_headers_to_raw(raw, raw_len);
    } else {
        apply_headers_to_result(result);
    }
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
    if (nac_cfg.rate_limit_max <= 0) return 1;      /* --rate-limit 0: disabled */

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

    if (now - entry->window_start >= nac_cfg.rate_limit_window_sec) {
        entry->window_start = now;
        entry->count = 0;
    }

    entry->count++;
    int allowed = entry->count <= nac_cfg.rate_limit_max;
    if (!allowed && retry_after) {
        long remaining = (long)(entry->window_start + nac_cfg.rate_limit_window_sec - now);
        if (remaining < 1) remaining = 1;
        if (remaining > nac_cfg.rate_limit_window_sec) remaining = nac_cfg.rate_limit_window_sec;
        *retry_after = (int)remaining;
    }

    if (unlock) unlock();
    return allowed;
}

/* Built-in health check, opt-in with --health / NAC_HEALTH. */
static int is_health_request(const NacHttpRequest *req) {
    if (!nac_cfg.health) return 0;
    if (strcmp(req->method, "GET") != 0 && strcmp(req->method, "HEAD") != 0) return 0;
    return strcmp(req->path, "/health") == 0;
}

static Value make_health_response(void) {
    Value m = make_map();
    map_set(&m, "status", make_int(200));

    Value body = make_map();
    map_set(&body, "status", make_string("ok"));
    map_set(&m, "body", body);
    free_value(&body);

    Value headers = make_map();
    map_set(&headers, "cache-control", make_string("no-store"));
    map_set(&m, "headers", headers);
    free_value(&headers);
    return m;
}

int nac_dispatch_request(NacHttpRequest *req, const char *handler_name,
                         int keep_alive, NacLockFn lock, NacLockFn unlock,
                         Value *result, char **raw_out, int *raw_len, int *status) {
    *raw_out = NULL;
    *raw_len = 0;
    nac_cookie_reset();
    nac_header_reset();

    /* A route the script defined for GET /health always wins. Otherwise the
     * built-in answers, and before the rate limiter so that a probe is never
     * turned into a 429 (which could make an orchestrator restart a healthy
     * process). */
    if (is_health_request(req)) {
        char probe_fn[MAX_TOKEN_LEN];
        Value probe_params;
        int probe_path_matched = 0;
        if (router_match(req->method, req->path, probe_fn, sizeof(probe_fn),
                         &probe_params, &probe_path_matched)) {
            free_value(&probe_params);
        } else {
            *result = make_health_response();
            return 0;
        }
    }

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

    /* Middleware: when the script has before/after blocks, the whole request
     * (before -> route -> after) runs under one acquisition of the
     * interpreter lock, so a global set in `before` is still there in the
     * route and in `after`. Without middleware each call locks on its own,
     * as it always did. The lock is released while a static file is read. */
    int mw = middleware_count() > 0;

    char fn[MAX_TOKEN_LEN];
    int path_matched = 0;
    int route_found = 0;
    Value params = make_map();
    {
        Value matched_params;
        if (router_match(req->method, req->path, fn, sizeof(fn), &matched_params, &path_matched)) {
            free_value(&params);
            params = matched_params;
            route_found = 1;
        }
    }

    int answered = 0;
    char *raw = NULL;
    *result = make_null();

    if (mw) lock();
    if (mw && nac_run_before(req, params, result)) answered = 1;

    if (!answered && route_found) {
        Value args[7];
        args[0] = make_string(req->method);
        args[1] = make_string(req->path);
        args[2] = req->query;
        args[3] = req->headers;
        args[4] = make_string(req->body);
        args[5] = params;
        args[6] = req->cookie;

        if (!mw) lock();
        int found = call_nac_function(fn, args, 7, result);
        if (!mw) unlock();

        if (!found) *result = make_status_map(500, "NaC HTTP server: route function not found");
        answered = 1;
    }

    if (!answered) {
        if (mw) unlock();
        raw = static_serve(req->method, req->path, &req->headers, raw_len, status,
                           keep_alive, lock, unlock);
        if (mw) lock();
        if (raw) answered = 1;
    }

    if (!answered && handler_name && handler_name[0]) {
        Value args[5];
        args[0] = make_string(req->method);
        args[1] = make_string(req->path);
        args[2] = req->query;
        args[3] = req->headers;
        args[4] = make_string(req->body);

        if (!mw) lock();
        int found = call_nac_function(handler_name, args, 5, result);
        if (!mw) unlock();

        if (!found) {
            char msg[512];
            snprintf(msg, sizeof(msg), "NaC HTTP server: handler function '%s' not found", handler_name);
            *result = make_status_map(500, msg);
        }
        answered = 1;
    }

    if (!answered) {
        char msg[1200];
        if (path_matched) {
            snprintf(msg, sizeof(msg), "405 Method Not Allowed: %s %s", req->method, req->path);
            *result = make_status_map(405, msg);
            *status = 405;
        } else {
            snprintf(msg, sizeof(msg), "404 Not Found: %s", req->path);
            *result = make_status_map(404, msg);
            *status = 404;
        }
    }

    /* Runs `after` blocks (needs the lock only if there are any, i.e. mw)
     * and applies setHeader() headers. */
    nac_finish_response(req, params, result, &raw, raw_len, status);
    if (mw) unlock();
    free_value(&params);

    if (raw) {
        *raw_out = raw;
        return 1;
    }
    return 0;
}
