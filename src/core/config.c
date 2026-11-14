#include "config.h"

#include <ctype.h>
#include <errno.h>
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
