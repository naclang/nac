#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/error.h"

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuilder;

static void sb_init(StrBuilder *sb) {
    sb->cap = 128;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    sb->data[0] = '\0';
}

static void sb_ensure(StrBuilder *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) {
        return;
    }

    while (sb->len + extra + 1 > sb->cap) {
        sb->cap *= 2;
    }

    sb->data = (char*)realloc(sb->data, sb->cap);
}

static void sb_append_char(StrBuilder *sb, char c) {
    sb_ensure(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

static void sb_append_str(StrBuilder *sb, const char *s) {
    size_t n = strlen(s);
    sb_ensure(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static int parse_json_string(const char **p, char *out, size_t out_size) {
    if (**p != '"') {
        return 0;
    }

    (*p)++;
    size_t idx = 0;

    while (**p && **p != '"') {
        char c = **p;
        (*p)++;

        if (c == '\\') {
            if (!**p) {
                return 0;
            }

            char esc = **p;
            (*p)++;

            switch (esc) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                default:
                    return 0;
            }
        }

        if (idx + 1 < out_size) {
            out[idx++] = c;
        }
    }

    if (**p != '"') {
        return 0;
    }

    (*p)++;
    out[idx] = '\0';
    return 1;
}

static int parse_json_number(const char **p, Value *out) {
    const char *start = *p;

    if (**p == '-') {
        (*p)++;
    }

    if (!isdigit((unsigned char)**p)) {
        return 0;
    }

    while (isdigit((unsigned char)**p)) {
        (*p)++;
    }

    int is_float = 0;

    if (**p == '.') {
        is_float = 1;
        (*p)++;
        if (!isdigit((unsigned char)**p)) {
            return 0;
        }
        while (isdigit((unsigned char)**p)) {
            (*p)++;
        }
    }

    if (**p == 'e' || **p == 'E') {
        is_float = 1;
        (*p)++;
        if (**p == '+' || **p == '-') {
            (*p)++;
        }
        if (!isdigit((unsigned char)**p)) {
            return 0;
        }
        while (isdigit((unsigned char)**p)) {
            (*p)++;
        }
    }

    size_t len = (size_t)(*p - start);
    char tmp[128];
    if (len >= sizeof(tmp)) {
        return 0;
    }

    memcpy(tmp, start, len);
    tmp[len] = '\0';

    if (is_float) {
        *out = make_float(strtod(tmp, NULL));
    } else {
        *out = make_int((int)strtol(tmp, NULL, 10));
    }

    return 1;
}

static int parse_value(const char **p, Value *out, int depth);

static int parse_array(const char **p, Value *out, int depth) {
    if (**p != '[') {
        return 0;
    }

    (*p)++;
    skip_ws(p);

    Value *items = NULL;
    int count = 0;
    int cap = 0;

    if (**p == ']') {
        (*p)++;
        *out = make_array(0);
        return 1;
    }

    while (**p) {
        if (count >= cap) {
            cap = (cap == 0) ? 4 : cap * 2;
            items = (Value*)realloc(items, sizeof(Value) * cap);
        }

        if (!parse_value(p, &items[count], depth + 1)) {
            for (int i = 0; i < count; i++) {
                free_value(&items[i]);
            }
            free(items);
            return 0;
        }
        count++;

        skip_ws(p);

        if (**p == ',') {
            (*p)++;
            skip_ws(p);
            continue;
        }

        if (**p == ']') {
            (*p)++;
            Value arr = make_array(count);
            for (int i = 0; i < count; i++) {
                free_value(&arr.array_val.elements[i]);
                arr.array_val.elements[i] = items[i];
            }
            free(items);
            *out = arr;
            return 1;
        }

        break;
    }

    for (int i = 0; i < count; i++) {
        free_value(&items[i]);
    }
    free(items);
    return 0;
}

static int parse_object(const char **p, Value *out, int depth) {
    if (**p != '{') {
        return 0;
    }

    (*p)++;
    skip_ws(p);

    Value obj = make_map();

    if (**p == '}') {
        (*p)++;
        *out = obj;
        return 1;
    }

    while (**p) {
        char key[MAX_STRING_LEN];
        if (!parse_json_string(p, key, sizeof(key))) {
            free_value(&obj);
            return 0;
        }

        skip_ws(p);
        if (**p != ':') {
            free_value(&obj);
            return 0;
        }

        (*p)++;
        skip_ws(p);

        Value val;
        if (!parse_value(p, &val, depth + 1)) {
            free_value(&obj);
            return 0;
        }

        map_set(&obj, key, val);
        free_value(&val);

        skip_ws(p);

        if (**p == ',') {
            (*p)++;
            skip_ws(p);
            continue;
        }

        if (**p == '}') {
            (*p)++;
            *out = obj;
            return 1;
        }

        break;
    }

    free_value(&obj);
    return 0;
}

static int parse_value(const char **p, Value *out, int depth) {
    if (depth > 64) {
        return 0;
    }

    skip_ws(p);

    if (**p == '"') {
        char s[MAX_STRING_LEN];
        if (!parse_json_string(p, s, sizeof(s))) {
            return 0;
        }
        *out = make_string(s);
        return 1;
    }

    if (**p == '{') {
        return parse_object(p, out, depth);
    }

    if (**p == '[') {
        return parse_array(p, out, depth);
    }

    if (strncmp(*p, "true", 4) == 0) {
        *out = make_int(1);
        *p += 4;
        return 1;
    }

    if (strncmp(*p, "false", 5) == 0) {
        *out = make_int(0);
        *p += 5;
        return 1;
    }

    if (strncmp(*p, "null", 4) == 0) {
        *out = make_int(0);
        *p += 4;
        return 1;
    }

    return parse_json_number(p, out);
}

bool json_parse_value(const char *json, Value *out) {
    if (!json || !out) {
        return false;
    }

    const char *p = json;
    if (!parse_value(&p, out, 0)) {
        report_error("Invalid JSON input");
        return false;
    }

    skip_ws(&p);
    if (*p != '\0') {
        free_value(out);
        report_error("Invalid JSON: trailing characters");
        return false;
    }

    return true;
}

static void stringify_escaped_string(StrBuilder *sb, const char *s) {
    sb_append_char(sb, '"');
    for (size_t i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        switch (c) {
            case '"': sb_append_str(sb, "\\\""); break;
            case '\\': sb_append_str(sb, "\\\\"); break;
            case '\n': sb_append_str(sb, "\\n"); break;
            case '\r': sb_append_str(sb, "\\r"); break;
            case '\t': sb_append_str(sb, "\\t"); break;
            default: sb_append_char(sb, c); break;
        }
    }
    sb_append_char(sb, '"');
}

static void stringify_value(StrBuilder *sb, Value value) {
    char num[64];

    switch (value.type) {
        case TYPE_INT:
            snprintf(num, sizeof(num), "%d", value.int_val);
            sb_append_str(sb, num);
            break;

        case TYPE_FLOAT:
            snprintf(num, sizeof(num), "%g", value.float_val);
            sb_append_str(sb, num);
            break;

        case TYPE_STRING:
            stringify_escaped_string(sb, value.str_val);
            break;

        case TYPE_ARRAY:
            sb_append_char(sb, '[');
            for (int i = 0; i < value.array_val.size; i++) {
                if (i > 0) {
                    sb_append_char(sb, ',');
                }
                stringify_value(sb, value.array_val.elements[i]);
            }
            sb_append_char(sb, ']');
            break;

        case TYPE_MAP:
            sb_append_char(sb, '{');
            for (int i = 0; i < value.map_val.size; i++) {
                if (i > 0) {
                    sb_append_char(sb, ',');
                }
                stringify_escaped_string(sb, value.map_val.keys[i]);
                sb_append_char(sb, ':');
                stringify_value(sb, value.map_val.values[i]);
            }
            sb_append_char(sb, '}');
            break;
    }
}

char *json_stringify_value(Value value) {
    StrBuilder sb;
    sb_init(&sb);
    stringify_value(&sb, value);
    return sb.data;
}
