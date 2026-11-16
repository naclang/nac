#include "router.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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

typedef struct {
    MiddlewareKind kind;
    char pattern[512];      /* "" = every path */
    char fn_name[MAX_TOKEN_LEN];
} Middleware;

static Route g_routes[MAX_ROUTES];
static int g_route_count = 0;
static Middleware g_middleware[MAX_MIDDLEWARE];
static int g_middleware_count = 0;
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

int middleware_add(MiddlewareKind kind, const char *pattern, const char *fn_name) {
    if (g_middleware_count >= MAX_MIDDLEWARE) return -1;
    Middleware *m = &g_middleware[g_middleware_count++];
    m->kind = kind;
    m->pattern[0] = '\0';
    if (pattern) {
        strncpy(m->pattern, pattern, sizeof(m->pattern) - 1);
        m->pattern[sizeof(m->pattern) - 1] = '\0';
    }
    strncpy(m->fn_name, fn_name, sizeof(m->fn_name) - 1);
    m->fn_name[sizeof(m->fn_name) - 1] = '\0';
    return 0;
}

int middleware_count(void) {
    return g_middleware_count;
}

int middleware_next(MiddlewareKind kind, int *cursor, const char *path,
                    char *fn_out, int fn_cap) {
    for (int i = *cursor; i < g_middleware_count; i++) {
        const Middleware *m = &g_middleware[i];
        if (m->kind != kind) continue;
        if (m->pattern[0]) {
            Value scratch = make_map();
            int ok = match_pattern(m->pattern, path, &scratch);
            free_value(&scratch);
            if (!ok) continue;
        }
        strncpy(fn_out, m->fn_name, fn_cap - 1);
        fn_out[fn_cap - 1] = '\0';
        *cursor = i + 1;
        return 1;
    }
    *cursor = g_middleware_count;
    return 0;
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

/* --- Static file cache -------------------------------------------------
 * Small files are kept in memory, keyed by path + mtime + size, so repeated
 * requests skip open/read entirely. Bounded to keep memory use predictable;
 * larger files are read per request as before. The caller serializes access
 * with the interpreter lock (passed to static_serve). */
#define STATIC_CACHE_ENTRIES 32
#define STATIC_CACHE_FILE_LIMIT (512 * 1024)
#define STATIC_CACHE_TOTAL_LIMIT (4 * 1024 * 1024)

typedef struct {
    char path[2200];
    time_t mtime;
    long size;
    char *data;
} StaticCacheEntry;

static StaticCacheEntry g_static_cache[STATIC_CACHE_ENTRIES];
static int g_static_cache_next = 0;
static size_t g_static_cache_bytes = 0;

static void static_cache_drop(StaticCacheEntry *e) {
    if (!e->data) return;
    free(e->data);
    e->data = NULL;
    if (g_static_cache_bytes >= (size_t)e->size) g_static_cache_bytes -= (size_t)e->size;
    else g_static_cache_bytes = 0;
    e->path[0] = '\0';
    e->size = 0;
}

static int static_cache_copy(const char *path, time_t mtime, long size, char *dest) {
    for (int i = 0; i < STATIC_CACHE_ENTRIES; i++) {
        StaticCacheEntry *e = &g_static_cache[i];
        if (e->data && e->mtime == mtime && e->size == size && strcmp(e->path, path) == 0) {
            memcpy(dest, e->data, (size_t)size);
            return 1;
        }
    }
    return 0;
}

static void static_cache_store(const char *path, time_t mtime, long size, const char *data) {
    if (size <= 0 || size > STATIC_CACHE_FILE_LIMIT) return;

    StaticCacheEntry *target = &g_static_cache[g_static_cache_next];
    static_cache_drop(target);

    while (g_static_cache_bytes + (size_t)size > STATIC_CACHE_TOTAL_LIMIT) {
        StaticCacheEntry *victim = NULL;
        for (int i = 1; i <= STATIC_CACHE_ENTRIES; i++) {
            StaticCacheEntry *e = &g_static_cache[(g_static_cache_next + i) % STATIC_CACHE_ENTRIES];
            if (e->data) { victim = e; break; }
        }
        if (!victim) break;
        static_cache_drop(victim);
    }

    char *copy = (char*)malloc((size_t)size);
    if (!copy) return;
    memcpy(copy, data, (size_t)size);
    target->data = copy;
    target->mtime = mtime;
    target->size = size;
    strncpy(target->path, path, sizeof(target->path) - 1);
    target->path[sizeof(target->path) - 1] = '\0';
    g_static_cache_bytes += (size_t)size;
    g_static_cache_next = (g_static_cache_next + 1) % STATIC_CACHE_ENTRIES;
}

static int month_index(const char *m) {
    static const char *names[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
    for (int i = 0; i < 12; i++) {
        int j = 0;
        while (j < 3 && m[j] &&
               tolower((unsigned char)m[j]) == tolower((unsigned char)names[i][j])) j++;
        if (j == 3) return i;
    }
    return -1;
}

/* Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
 * civil-from-days algorithm), so HTTP dates can be parsed without timegm(). */
static long long days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned mp = (m > 2u) ? (m - 3u) : (m + 9u);
    const unsigned doy = (153u * mp + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (long long)era * 146097 + (long long)doe - 719468;
}

static void format_http_date(time_t t, char *out, size_t cap) {
    static const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
    struct tm tm;
#if defined(_MSC_VER)
    if (gmtime_s(&tm, &t) != 0) {
#else
    if (!gmtime_r(&t, &tm)) {
#endif
        snprintf(out, cap, "Thu, 01 Jan 1970 00:00:00 GMT");
        return;
    }
    snprintf(out, cap, "%s, %02d %s %04d %02d:%02d:%02d GMT",
             days[tm.tm_wday % 7], tm.tm_mday, months[tm.tm_mon % 12],
             tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* Parses the IMF-fixdate format we emit ("Sun, 06 Nov 1994 08:49:37 GMT")
 * and the older asctime variant ("Sun Nov  6 08:49:37 1994"). Returns
 * (time_t)-1 when the value is not a valid HTTP date. */
static time_t parse_http_date(const char *s) {
    char mon[8];
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(s, "%*3s, %d %7s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) != 6 &&
        sscanf(s, "%*3s %7s %d %d:%d:%d %d", mon, &day, &hh, &mm, &ss, &year) != 6) {
        return (time_t)-1;
    }
    int mi = month_index(mon);
    if (mi < 0 || day < 1 || day > 31 || year < 1970 ||
        hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60) {
        return (time_t)-1;
    }
    long long days = days_from_civil(year, (unsigned)(mi + 1), (unsigned)day);
    return (time_t)(days * 86400 + hh * 3600 + mm * 60 + ss);
}

/* True when the If-None-Match list contains `etag` (weak comparison), or "*". */
static int etag_list_matches(const char *list, const char *etag) {
    const char *p = list;
    size_t etag_len = strlen(etag);
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        if (*p == '*') return 1;

        const char *start = p;
        if (p[0] == 'W' && p[1] == '/') p += 2;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') p++;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
        }

        const char *tag = start;
        size_t len = (size_t)(p - start);
        if (len >= 2 && tag[0] == 'W' && tag[1] == '/') { tag += 2; len -= 2; }
        if (len == etag_len && strncmp(tag, etag, len) == 0) return 1;

        while (*p && *p != ',') p++;
    }
    return 0;
}

static char *build_file_response(const char *fs_path, int head_only,
                                 Value *req_headers, int *resp_len, int *status_out,
                                 int keep_alive,
                                 void (*lock)(void), void (*unlock)(void)) {
    FILE *f = fopen(fs_path, "rb");
    if (!f) return NULL;

    struct stat st;
#if defined(_MSC_VER)
    int fd = _fileno(f);
#else
    int fd = fileno(f);
#endif
    if (fstat(fd, &st) != 0 || (st.st_mode & S_IFMT) == S_IFDIR) {
        fclose(f);
        return NULL;
    }
    long size = (long)st.st_size;
    if (size < 0 || size > MAX_STATIC_FILE) { fclose(f); return NULL; }

    char etag[64];
    snprintf(etag, sizeof(etag), "\"%llx-%lx\"",
             (unsigned long long)st.st_mtime, (unsigned long)size);
    char last_modified[64];
    format_http_date(st.st_mtime, last_modified, sizeof(last_modified));

    /* A matching If-None-Match wins over If-Modified-Since (RFC 9110). */
    int not_modified = 0;
    if (req_headers && req_headers->type == TYPE_MAP) {
        Value *inm = map_get(req_headers, "if-none-match");
        Value *ims = map_get(req_headers, "if-modified-since");
        if (inm && inm->type == TYPE_STRING) {
            if (etag_list_matches(inm->str_val, etag)) not_modified = 1;
        } else if (ims && ims->type == TYPE_STRING) {
            time_t since = parse_http_date(ims->str_val);
            if (since != (time_t)-1 && st.st_mtime <= since) not_modified = 1;
        }
    }

    char header[768];
    int hlen;
    const char *connection = keep_alive ? "keep-alive" : "close";
    if (not_modified) {
        hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 304 Not Modified\r\n"
            "ETag: %s\r\n"
            "Last-Modified: %s\r\n"
            "Cache-Control: public, max-age=0, must-revalidate\r\n"
            "Connection: %s\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "X-Frame-Options: DENY\r\n"
            "Referrer-Policy: no-referrer\r\n"
            "\r\n", etag, last_modified, connection);
    } else {
        hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "ETag: %s\r\n"
            "Last-Modified: %s\r\n"
            "Cache-Control: public, max-age=0, must-revalidate\r\n"
            "Connection: %s\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "X-Frame-Options: DENY\r\n"
            "Referrer-Policy: no-referrer\r\n"
            "\r\n", mime_for(fs_path), size, etag, last_modified, connection);
    }
    if (hlen < 0 || hlen >= (int)sizeof(header)) { fclose(f); return NULL; }

    int send_body = !head_only && !not_modified;
    char *out = (char*)malloc((size_t)hlen + (send_body ? (size_t)size : 0) + 1);
    if (!out) { fclose(f); return NULL; }
    memcpy(out, header, (size_t)hlen);

    if (send_body && size > 0) {
        int cached = 0;
        if (lock) lock();
        cached = static_cache_copy(fs_path, st.st_mtime, size, out + hlen);
        if (unlock) unlock();

        if (!cached) {
            if (fread(out + hlen, 1, (size_t)size, f) != (size_t)size) {
                free(out);
                fclose(f);
                return NULL;
            }
            if (lock) lock();
            static_cache_store(fs_path, st.st_mtime, size, out + hlen);
            if (unlock) unlock();
        }
    }
    fclose(f);

    out[hlen + (send_body ? (int)size : 0)] = '\0';
    *resp_len = hlen + (send_body ? (int)size : 0);
    if (status_out) *status_out = not_modified ? 304 : 200;
    return out;
}

char *static_serve(const char *method, const char *path, Value *req_headers,
                   int *resp_len, int *status_out, int keep_alive,
                   void (*lock)(void), void (*unlock)(void)) {
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

        char *resp = build_file_response(fs_path, head, req_headers, resp_len, status_out,
                                         keep_alive, lock, unlock);
        if (resp) return resp;
    }
    return NULL;
}
