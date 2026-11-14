#include "router.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../lexer/token.h"

typedef struct {
    char method[16];
    char pattern[512];
    char fn_name[MAX_TOKEN_LEN];
} Route;

typedef struct {
    char prefix[256];
    char dir[1024];
} Mount;

static Route g_routes[MAX_ROUTES];
static int g_route_count = 0;
static Mount g_mounts[MAX_MOUNTS];
static int g_mount_count = 0;

#define MAX_STATIC_FILE (32 * 1024 * 1024)

int router_add(const char *method, const char *pattern, const char *fn_name) {
    if (g_route_count >= MAX_ROUTES) return -1;
    Route *r = &g_routes[g_route_count++];
    int i = 0;
    for (; method[i] && i < (int)sizeof(r->method) - 1; i++) r->method[i] = (char)toupper((unsigned char)method[i]);
    r->method[i] = '\0';
    strncpy(r->pattern, pattern, sizeof(r->pattern) - 1);
    r->pattern[sizeof(r->pattern) - 1] = '\0';
    strncpy(r->fn_name, fn_name, sizeof(r->fn_name) - 1);
    r->fn_name[sizeof(r->fn_name) - 1] = '\0';
    return 0;
}

static void decode_segment(const char *src, int len, char *dst, int cap) {
    int o = 0;
    for (int i = 0; i < len && o < cap - 1; i++) {
        if (src[i] == '%' && i + 2 < len && isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
            char hex[3] = { src[i + 1], src[i + 2], '\0' };
            dst[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
}

static int match_pattern(const char *pat, const char *path, Value *params) {
    const char *p = pat;
    const char *s = path;
    while (*p == '/') p++;
    while (*s == '/') s++;

    for (;;) {
        if (*p == '\0' && *s == '\0') return 1;
        if (*p == '\0') return 0;

        const char *pe = strchr(p, '/');
        int plen = pe ? (int)(pe - p) : (int)strlen(p);

        if (plen == 1 && p[0] == '*') {
            char rest[1024];
            decode_segment(s, (int)strlen(s), rest, sizeof(rest));
            map_set(params, "*", make_string(rest));
            return 1;
        }

        if (*s == '\0') return 0;
        const char *se = strchr(s, '/');
        int slen = se ? (int)(se - s) : (int)strlen(s);

        if (p[0] == ':' && plen > 1) {
            char name[256];
            char val[1024];
            int nlen = plen - 1;
            if (nlen > (int)sizeof(name) - 1) nlen = (int)sizeof(name) - 1;
            memcpy(name, p + 1, nlen);
            name[nlen] = '\0';
            decode_segment(s, slen, val, sizeof(val));
            map_set(params, name, make_string(val));
        } else if (plen != slen || strncmp(p, s, plen) != 0) {
            return 0;
        }

        p += plen;
        s += slen;
        while (*p == '/') p++;
        while (*s == '/') s++;
    }
}

int router_match(const char *method, const char *path, char *fn_out, int fn_cap,
                 Value *params, int *path_matched) {
    if (path_matched) *path_matched = 0;

    for (int i = 0; i < g_route_count; i++) {
        Value tmp = make_map();
        if (!match_pattern(g_routes[i].pattern, path, &tmp)) {
            free_value(&tmp);
            continue;
        }

        int method_ok = strcmp(g_routes[i].method, "*") == 0 ||
                        strcmp(g_routes[i].method, "ANY") == 0 ||
                        strcmp(g_routes[i].method, method) == 0;
        if (!method_ok) {
            free_value(&tmp);
            if (path_matched) *path_matched = 1;
            continue;
        }

        strncpy(fn_out, g_routes[i].fn_name, fn_cap - 1);
        fn_out[fn_cap - 1] = '\0';
        *params = tmp;
        return 1;
    }
    return 0;
}

int static_mount_add(const char *prefix, const char *dir) {
    if (g_mount_count >= MAX_MOUNTS) return -1;
    Mount *m = &g_mounts[g_mount_count++];

    char norm[256];
    snprintf(norm, sizeof(norm), "%s%s", (prefix[0] == '/') ? "" : "/", prefix);
    int len = (int)strlen(norm);
    while (len > 1 && norm[len - 1] == '/') norm[--len] = '\0';
    strncpy(m->prefix, norm, sizeof(m->prefix) - 1);
    m->prefix[sizeof(m->prefix) - 1] = '\0';

    strncpy(m->dir, dir, sizeof(m->dir) - 1);
    m->dir[sizeof(m->dir) - 1] = '\0';
    len = (int)strlen(m->dir);
    while (len > 1 && (m->dir[len - 1] == '/' || m->dir[len - 1] == '\\')) m->dir[--len] = '\0';
    return 0;
}

static const char *mime_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    static const struct { const char *ext; const char *mime; } table[] = {
        {".html", "text/html; charset=utf-8"}, {".htm", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"},
        {".mjs", "text/javascript; charset=utf-8"}, {".json", "application/json"},
        {".txt", "text/plain; charset=utf-8"}, {".xml", "application/xml"},
        {".svg", "image/svg+xml"}, {".png", "image/png"}, {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".webp", "image/webp"},
        {".ico", "image/x-icon"}, {".woff", "font/woff"}, {".woff2", "font/woff2"},
        {".ttf", "font/ttf"}, {".pdf", "application/pdf"}, {".mp4", "video/mp4"},
        {".webm", "video/webm"}, {".wasm", "application/wasm"}, {".map", "application/json"}
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        const char *a = dot;
        const char *b = table[i].ext;
        while (*a && *b && tolower((unsigned char)*a) == *b) { a++; b++; }
        if (!*a && !*b) return table[i].mime;
    }
    return "application/octet-stream";
}

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static char *build_file_response(const char *fs_path, int head_only, int *resp_len) {
    FILE *f = fopen(fs_path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > MAX_STATIC_FILE) { fclose(f); return NULL; }

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "\r\n", mime_for(fs_path), size);

    char *out = (char*)malloc((size_t)hlen + (size_t)size + 1);
    if (!out) { fclose(f); return NULL; }
    memcpy(out, header, hlen);

    size_t got = 0;
    if (!head_only) got = fread(out + hlen, 1, (size_t)size, f);
    fclose(f);
    if (!head_only && got != (size_t)size) { free(out); return NULL; }

    *resp_len = hlen + (head_only ? 0 : (int)size);
    return out;
}

char *static_serve(const char *method, const char *path, int *resp_len, int *status_out) {
    int head = strcmp(method, "HEAD") == 0;
    if (!head && strcmp(method, "GET") != 0) return NULL;

    /* Reject anything that could escape the mounted directory. */
    if (strstr(path, "..") || strchr(path, '\\')) return NULL;

    for (int i = 0; i < g_mount_count; i++) {
        const char *prefix = g_mounts[i].prefix;
        size_t plen = strlen(prefix);
        const char *rel;

        if (strcmp(prefix, "/") == 0) {
            rel = path;
        } else if (strncmp(path, prefix, plen) == 0 && (path[plen] == '/' || path[plen] == '\0')) {
            rel = path + plen;
        } else {
            continue;
        }
        while (*rel == '/') rel++;

        char decoded[1024];
        decode_segment(rel, (int)strlen(rel), decoded, sizeof(decoded));
        if (strstr(decoded, "..") || strchr(decoded, '\\')) continue;

        char fs_path[2200];
        snprintf(fs_path, sizeof(fs_path), "%s/%s", g_mounts[i].dir, decoded);

        size_t flen = strlen(fs_path);
        if (decoded[0] == '\0' || fs_path[flen - 1] == '/' || is_dir(fs_path)) {
            if (fs_path[flen - 1] != '/') strncat(fs_path, "/", sizeof(fs_path) - strlen(fs_path) - 1);
            strncat(fs_path, "index.html", sizeof(fs_path) - strlen(fs_path) - 1);
        }

        char *resp = build_file_response(fs_path, head, resp_len);
        if (resp) {
            if (status_out) *status_out = 200;
            return resp;
        }
    }
    return NULL;
}
