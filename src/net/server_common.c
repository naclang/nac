#include "server_common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../builtin/builtin.h"
#include "../runtime/json.h"

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
                *content_length = atoi(val);
            }
        }

        if (!sep) break;
        start = sep + 2;
    }
}

void nac_http_parse_request(const char *raw, int header_end, NacHttpRequest *req) {
    memset(req->method, 0, sizeof(req->method));
    memset(req->path, 0, sizeof(req->path));
    memset(req->query_raw, 0, sizeof(req->query_raw));
    req->query = make_map();
    req->headers = make_map();
    req->body[0] = '\0';
    req->content_length = 0;

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
}

void nac_http_free_request(NacHttpRequest *req) {
    free_value(&req->query);
    free_value(&req->headers);
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
        case 409: return "Conflict";
        case 422: return "Unprocessable Entity";
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
                    strncpy(content_type, val_str, sizeof(content_type) - 1);
                    continue;
                }

                char line[1600];
                snprintf(line, sizeof(line), "%s: %s\r\n", key, val_str);
                strncat(extra_header_lines, line, sizeof(extra_header_lines) - strlen(extra_header_lines) - 1);
            }
        }
    } else if (result.type == TYPE_STRING) {
        strncpy(body, result.str_val, sizeof(body) - 1);
    } else {
        value_to_display_string(result, body, sizeof(body));
    }

    if (out_status) *out_status = status;

    int written = snprintf(out, out_cap,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n"
        "%s",
        status, status_text(status), content_type, (int)strlen(body), extra_header_lines, body);

    if (written < 0 || written >= out_cap) return -1;
    return written;
}
