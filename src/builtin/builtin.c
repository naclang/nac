#include "builtin.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "../util/error.h"

void value_to_display_string(Value v, char *out, size_t out_size) {
    switch (v.type) {
        case TYPE_INT:
            snprintf(out, out_size, "%d", v.int_val);
            break;
        case TYPE_FLOAT:
            nac_format_double(v.float_val, out, out_size);
            break;
        case TYPE_STRING:
            snprintf(out, out_size, "%s", v.str_val);
            break;
        case TYPE_BOOL:
            snprintf(out, out_size, "%s", v.int_val ? "true" : "false");
            break;
        case TYPE_NULL:
            snprintf(out, out_size, "null");
            break;
        case TYPE_ARRAY:
            snprintf(out, out_size, "[array:%d]", v.array_val.size);
            break;
        case TYPE_MAP:
            snprintf(out, out_size, "[map:%d]", v.map_val.size);
            break;
        case TYPE_FUNC:
            snprintf(out, out_size, "<fn>");
            break;
        default:
            if (out_size > 0) out[0] = '\0';
            break;
    }
}

bool is_builtin_function(const char *name) {
    const char *builtins[] = {
        "sqrt", "pow", "sin", "cos", "tan", "abs", "floor", "ceil", "round", "log", "exp",
        "length", "upper", "lower", "push", "pop", "removeAt", "delete",
        "trim", "replace", "substr", "indexOf",
        "first", "last", "reverse", "slice", "join", "split", "contains", "has",
        "read", "write", "append", "map",
        "typeOf", "toInt", "toFloat", "toString", "isNull",
        "urlEncode", "urlDecode",
        "now", "array", "range", "keys", "values", "sleep",
        "input", "len", "str", "int", "float", "type", "assert",
        "startsWith", "endsWith"
    };
    int builtin_count = sizeof(builtins) / sizeof(builtins[0]);
    for (int i = 0; i < builtin_count; i++) {
        if (strcmp(name, builtins[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Python-style short names for the camelCase builtins. */
static const char *canonical_builtin(const char *name) {
    if (strcmp(name, "len") == 0) return "length";
    if (strcmp(name, "str") == 0) return "toString";
    if (strcmp(name, "int") == 0) return "toInt";
    if (strcmp(name, "float") == 0) return "toFloat";
    if (strcmp(name, "type") == 0) return "typeOf";
    return name;
}

/* Growable string buffer: string builtins build results of any length. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} NacSbuf;

static int sbuf_add(NacSbuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 64;
        while (nc < b->len + n + 1) nc *= 2;
        char *d = (char*)realloc(b->data, nc);
        if (!d) return 0;
        b->data = d;
        b->cap = nc;
    }
    if (n) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

static Value sbuf_finish(NacSbuf *b) {
    if (!b->data) return make_string("");
    return make_string_take(b->data);
}

#define SBUF_ADD(buf, s, n) \
    do { \
        if (!sbuf_add((buf), (s), (n))) { \
            free((buf)->data); \
            report_error("Out of memory"); \
            return make_string(""); \
        } \
    } while (0)

Value call_builtin_function(const char *name, Value *args, int arg_count) {
    name = canonical_builtin(name);

    if (strcmp(name, "assert") == 0) {
        if (arg_count < 1 || arg_count > 2) {
            report_error("assert() requires (condition, [message])");
            return make_bool(0);
        }
        if (!to_bool(args[0])) {
            char msg[512] = "Assertion failed";
            if (arg_count == 2) {
                value_to_display_string(args[1], msg, sizeof(msg));
            }
            report_error(msg);
            return make_bool(0);
        }
        return make_bool(1);
    }

    if (strcmp(name, "input") == 0) {
        /* input() / input("prompt") -> next line from stdin as a string, null at EOF */
        if (arg_count > 1) {
            report_error("input() takes at most 1 argument (prompt)");
            return make_null();
        }
        if (arg_count == 1) {
            char prompt[1024];
            value_to_display_string(args[0], prompt, sizeof(prompt));
            fputs(prompt, stdout);
            fflush(stdout);
        }
        size_t line_cap = 256, line_len = 0;
        char *line = (char*)malloc(line_cap);
        if (!line) return make_null();
        int ch;
        while ((ch = fgetc(stdin)) != EOF && ch != '\n') {
            if (line_len + 1 >= line_cap) {
                line_cap *= 2;
                char *grown = (char*)realloc(line, line_cap);
                if (!grown) {
                    free(line);
                    return make_null();
                }
                line = grown;
            }
            line[line_len++] = (char)ch;
        }
        if (ch == EOF && line_len == 0) {
            free(line);
            return make_null();
        }
        line[line_len] = '\0';
        line[strcspn(line, "\r\n")] = '\0';
        Value result = make_string(line);
        free(line);
        return result;
    }
    if (strcmp(name, "sqrt") == 0) {
        if (arg_count != 1) {
            report_error("sqrt() requires 1 argument");
            return make_float(0.0);
        }
        double val = to_float(args[0]);
        if (val < 0) {
            report_error("sqrt() of negative number");
            return make_float(0.0);
        }
        return make_float(sqrt(val));
    }

    if (strcmp(name, "pow") == 0) {
        if (arg_count != 2) {
            report_error("pow() requires 2 arguments");
            return make_float(0.0);
        }
        return make_float(pow(to_float(args[0]), to_float(args[1])));
    }

    if (strcmp(name, "sin") == 0) {
        if (arg_count != 1) {
            report_error("sin() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(sin(to_float(args[0])));
    }

    if (strcmp(name, "cos") == 0) {
        if (arg_count != 1) {
            report_error("cos() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(cos(to_float(args[0])));
    }

    if (strcmp(name, "tan") == 0) {
        if (arg_count != 1) {
            report_error("tan() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(tan(to_float(args[0])));
    }

    if (strcmp(name, "abs") == 0) {
        if (arg_count != 1) {
            report_error("abs() requires 1 argument");
            return make_float(0.0);
        }
        double val = to_float(args[0]);
        return (args[0].type == TYPE_INT) ? make_int(abs(to_int(args[0]))) : make_float(fabs(val));
    }

    if (strcmp(name, "floor") == 0) {
        if (arg_count != 1) {
            report_error("floor() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(floor(to_float(args[0])));
    }

    if (strcmp(name, "ceil") == 0) {
        if (arg_count != 1) {
            report_error("ceil() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(ceil(to_float(args[0])));
    }

    if (strcmp(name, "round") == 0) {
        if (arg_count != 1) {
            report_error("round() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(round(to_float(args[0])));
    }

    if (strcmp(name, "log") == 0) {
        if (arg_count != 1) {
            report_error("log() requires 1 argument");
            return make_float(0.0);
        }
        double val = to_float(args[0]);
        if (val <= 0) {
            report_error("log() of non-positive number");
            return make_float(0.0);
        }
        return make_float(log(val));
    }

    if (strcmp(name, "exp") == 0) {
        if (arg_count != 1) {
            report_error("exp() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(exp(to_float(args[0])));
    }

    if (strcmp(name, "length") == 0) {
        if (arg_count != 1) {
            report_error("length() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type == TYPE_STRING) {
            return make_int(strlen(args[0].str_val));
        } else if (args[0].type == TYPE_ARRAY) {
            return make_int(args[0].array_val.size);
        } else if (args[0].type == TYPE_MAP) {
            return make_int(args[0].map_val.size);
        }
        return make_int(0);
    }

    if (strcmp(name, "upper") == 0) {
        if (arg_count != 1) {
            report_error("upper() requires 1 argument");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("upper() requires a string");
            return make_string("");
        }
        Value result = make_string(args[0].str_val);
        for (char *c = result.str_val; *c; c++) {
            *c = (char)toupper((unsigned char)*c);
        }
        return result;
    }

    if (strcmp(name, "lower") == 0) {
        if (arg_count != 1) {
            report_error("lower() requires 1 argument");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("lower() requires a string");
            return make_string("");
        }
        Value result = make_string(args[0].str_val);
        for (char *c = result.str_val; *c; c++) {
            *c = (char)tolower((unsigned char)*c);
        }
        return result;
    }

    if (strcmp(name, "trim") == 0) {
        if (arg_count != 1) {
            report_error("trim() requires 1 argument");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("trim() requires a string");
            return make_string("");
        }
        const char *str = args[0].str_val;
        int start = 0;
        while (str[start] && isspace(str[start])) start++;
        int end = strlen(str) - 1;
        while (end >= start && isspace(str[end])) end--;

        if (end < start) return make_string("");
        return make_string_len(str + start, (size_t)(end - start + 1));
    }

    if (strcmp(name, "replace") == 0) {
        if (arg_count != 3) {
            report_error("replace() requires 3 arguments (string, old, new)");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING || args[1].type != TYPE_STRING || args[2].type != TYPE_STRING) {
            report_error("replace() requires string arguments");
            return make_string("");
        }

        const char *str = args[0].str_val;
        const char *old_substr = args[1].str_val;
        const char *new_substr = args[2].str_val;

        size_t old_len = strlen(old_substr);
        size_t new_len = strlen(new_substr);
        if (old_len == 0) return make_string(str);

        NacSbuf out = {0};
        const char *p = str;
        const char *hit;
        while ((hit = strstr(p, old_substr)) != NULL) {
            SBUF_ADD(&out, p, (size_t)(hit - p));
            SBUF_ADD(&out, new_substr, new_len);
            p = hit + old_len;
        }
        SBUF_ADD(&out, p, strlen(p));
        return sbuf_finish(&out);
    }

    if (strcmp(name, "substr") == 0) {
        if (arg_count != 3) {
            report_error("substr() requires 3 arguments (string, start, length)");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("substr() requires a string as first argument");
            return make_string("");
        }

        const char *str = args[0].str_val;
        int start = to_int(args[1]);
        int len = to_int(args[2]);
        int str_len = strlen(str);

        if (start < 0 || start >= str_len || len < 0) {
            return make_string("");
        }

        if (start + len > str_len) {
            len = str_len - start;
        }

        return make_string_len(str + start, (size_t)len);
    }

    if (strcmp(name, "startsWith") == 0 || strcmp(name, "endsWith") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("startsWith()/endsWith() require 2 string arguments (string, text)");
            return make_bool(0);
        }
        size_t str_len = strlen(args[0].str_val);
        size_t affix_len = strlen(args[1].str_val);
        if (affix_len > str_len) return make_bool(0);
        if (name[0] == 's') {
            return make_bool(strncmp(args[0].str_val, args[1].str_val, affix_len) == 0);
        }
        return make_bool(strcmp(args[0].str_val + (str_len - affix_len), args[1].str_val) == 0);
    }

    if (strcmp(name, "indexOf") == 0) {
        if (arg_count != 2) {
            report_error("indexOf() requires 2 arguments (string, substring)");
            return make_int(-1);
        }
        if (args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("indexOf() requires string arguments");
            return make_int(-1);
        }

        const char *str = args[0].str_val;
        const char *substr = args[1].str_val;
        const char *p = strstr(str, substr);

        if (p) {
            return make_int(p - str);
        }
        return make_int(-1);
    }

    if (strcmp(name, "first") == 0) {
        if (arg_count != 1) {
            report_error("first() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type != TYPE_ARRAY || args[0].array_val.size == 0) {
            report_error("first() on non-array or empty array");
            return make_int(0);
        }
        return args[0].array_val.elements[0];
    }

    if (strcmp(name, "last") == 0) {
        if (arg_count != 1) {
            report_error("last() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type != TYPE_ARRAY || args[0].array_val.size == 0) {
            report_error("last() on non-array or empty array");
            return make_int(0);
        }
        return args[0].array_val.elements[args[0].array_val.size - 1];
    }

    if (strcmp(name, "reverse") == 0) {
        if (arg_count != 1) {
            report_error("reverse() requires 1 argument");
            return make_array(0);
        }
        if (args[0].type != TYPE_ARRAY) {
            report_error("reverse() requires an array");
            return make_array(0);
        }

        Value arr = copy_value(args[0]);
        for (int i = 0; i < arr.array_val.size / 2; i++) {
            int j = arr.array_val.size - 1 - i;
            Value temp = arr.array_val.elements[i];
            arr.array_val.elements[i] = arr.array_val.elements[j];
            arr.array_val.elements[j] = temp;
        }
        return arr;
    }

    if (strcmp(name, "slice") == 0) {
        if (arg_count != 3) {
            report_error("slice() requires 3 arguments (array, start, end)");
            return make_array(0);
        }
        if (args[0].type != TYPE_ARRAY) {
            report_error("slice() requires an array");
            return make_array(0);
        }

        int start = to_int(args[1]);
        int end = to_int(args[2]);
        int size = args[0].array_val.size;

        if (start < 0) start = 0;
        if (end > size) end = size;
        if (start > end) start = end;

        int new_size = end - start;
        Value result = make_array(new_size);
        for (int i = 0; i < new_size; i++) {
            result.array_val.elements[i] = copy_value(args[0].array_val.elements[start + i]);
        }
        return result;
    }

    if (strcmp(name, "join") == 0) {
        if (arg_count != 2) {
            report_error("join() requires 2 arguments (array, separator)");
            return make_string("");
        }
        if (args[0].type != TYPE_ARRAY || args[1].type != TYPE_STRING) {
            report_error("join() requires an array and string separator");
            return make_string("");
        }

        NacSbuf out = {0};
        const char *sep = args[1].str_val;
        size_t sep_len = strlen(sep);

        for (int i = 0; i < args[0].array_val.size; i++) {
            if (i > 0) SBUF_ADD(&out, sep, sep_len);

            Value elem = args[0].array_val.elements[i];
            char num[64];
            const char *piece = "";
            if (elem.type == TYPE_INT) {
                snprintf(num, sizeof(num), "%d", elem.int_val);
                piece = num;
            } else if (elem.type == TYPE_FLOAT) {
                nac_format_double(elem.float_val, num, sizeof(num));
                piece = num;
            } else if (elem.type == TYPE_STRING) {
                piece = elem.str_val;
            }
            SBUF_ADD(&out, piece, strlen(piece));
        }

        return sbuf_finish(&out);
    }

    if (strcmp(name, "split") == 0) {
        if (arg_count != 2) {
            report_error("split() requires 2 arguments (string, separator)");
            return make_array(0);
        }
        if (args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("split() requires string arguments");
            return make_array(0);
        }

        const char *str = args[0].str_val;
        const char *sep = args[1].str_val;
        int sep_len = strlen(sep);

        Value result = make_array(0);
        result.array_val.capacity = 0;
        result.array_val.elements = NULL;

        if (sep_len == 0) {
            free_value(&result);
            report_error("split() separator must not be empty");
            return make_array(0);
        }

        const char *start = str;
        const char *p;
        while ((p = strstr(start, sep)) != NULL) {
            int len = p - start;

            if (result.array_val.size >= result.array_val.capacity) {
                int new_cap = (result.array_val.capacity == 0) ? 4 : result.array_val.capacity * 2;
                result.array_val.elements = (Value*)realloc(result.array_val.elements, sizeof(Value) * new_cap);
                result.array_val.capacity = new_cap;
            }
            result.array_val.elements[result.array_val.size++] = make_string_len(start, (size_t)len);

            start = p + sep_len;
        }

        if (result.array_val.size >= result.array_val.capacity) {
            int new_cap = (result.array_val.capacity == 0) ? 4 : result.array_val.capacity * 2;
            result.array_val.elements = (Value*)realloc(result.array_val.elements, sizeof(Value) * new_cap);
            result.array_val.capacity = new_cap;
        }
        result.array_val.elements[result.array_val.size++] = make_string(start);

        return result;
    }

    if (strcmp(name, "contains") == 0) {
        if (arg_count != 2) {
            report_error("contains() requires 2 arguments (string, substring)");
            return make_int(0);
        }
        if (args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("contains() requires string arguments");
            return make_int(0);
        }
        return make_int(strstr(args[0].str_val, args[1].str_val) != NULL ? 1 : 0);
    }

    if (strcmp(name, "has") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_MAP || args[1].type != TYPE_STRING) {
            report_error("has() requires a map and a string key");
            return make_int(0);
        }
        return make_int(map_get(&args[0], args[1].str_val) != NULL ? 1 : 0);
    }

    if (strcmp(name, "isNull") == 0) {
        if (arg_count != 1) {
            report_error("isNull() requires 1 argument");
            return make_bool(0);
        }
        return make_bool(args[0].type == TYPE_NULL);
    }

    if (strcmp(name, "typeOf") == 0) {
        if (arg_count != 1) {
            report_error("typeOf() requires 1 argument");
            return make_string("");
        }
        switch (args[0].type) {
            case TYPE_INT: return make_string("int");
            case TYPE_FLOAT: return make_string("float");
            case TYPE_STRING: return make_string("string");
            case TYPE_ARRAY: return make_string("array");
            case TYPE_MAP: return make_string("map");
            case TYPE_BOOL: return make_string("bool");
            case TYPE_NULL: return make_string("null");
            case TYPE_FUNC: return make_string("function");
        }
        return make_string("unknown");
    }

    if (strcmp(name, "toInt") == 0) {
        if (arg_count != 1) {
            report_error("toInt() requires 1 argument");
            return make_int(0);
        }
        return make_int(to_int(args[0]));
    }

    if (strcmp(name, "toFloat") == 0) {
        if (arg_count != 1) {
            report_error("toFloat() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(to_float(args[0]));
    }

    if (strcmp(name, "toString") == 0) {
        if (arg_count != 1) {
            report_error("toString() requires 1 argument");
            return make_string("");
        }
        if (args[0].type == TYPE_STRING) return make_string(args[0].str_val);
        char buf[4096];
        value_to_display_string(args[0], buf, sizeof(buf));
        if (strlen(buf) < sizeof(buf) - 1) return make_string(buf);
        /* Possibly truncated: render again into a large heap buffer. */
        size_t big_cap = 8 * 1024 * 1024;
        char *big = (char*)malloc(big_cap);
        if (!big) return make_string(buf);
        value_to_display_string(args[0], big, big_cap);
        return make_string_take(big);
    }

    if (strcmp(name, "urlEncode") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("urlEncode() requires 1 string argument");
            return make_string("");
        }
        const char *str = args[0].str_val;
        char *result = (char*)malloc(strlen(str) * 3 + 1);
        if (!result) return make_string("");
        int out_i = 0;
        for (int i = 0; str[i]; i++) {
            unsigned char c = (unsigned char)str[i];
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                result[out_i++] = c;
            } else if (c == ' ') {
                result[out_i++] = '+';
            } else {
                snprintf(result + out_i, 4, "%%%02X", c);
                out_i += 3;
            }
        }
        result[out_i] = '\0';
        return make_string_take(result);
    }

    if (strcmp(name, "urlDecode") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("urlDecode() requires 1 string argument");
            return make_string("");
        }
        const char *str = args[0].str_val;
        char *result = (char*)malloc(strlen(str) + 1);
        if (!result) return make_string("");
        int out_i = 0;
        for (int i = 0; str[i]; i++) {
            if (str[i] == '%' && isxdigit((unsigned char)str[i + 1]) && isxdigit((unsigned char)str[i + 2])) {
                char hex[3] = { str[i + 1], str[i + 2], '\0' };
                result[out_i++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else if (str[i] == '+') {
                result[out_i++] = ' ';
            } else {
                result[out_i++] = str[i];
            }
        }
        result[out_i] = '\0';
        return make_string_take(result);
    }

    if (strcmp(name, "now") == 0) {
        if (arg_count != 0) {
            report_error("now() requires 0 arguments");
            return make_int(0);
        }
        return make_int((int)time(NULL));
    }

    if (strcmp(name, "range") == 0) {
        /* range(n) -> [0..n-1]   range(a, b) -> [a..b-1] */
        if (arg_count != 1 && arg_count != 2) {
            report_error("range() requires 1 or 2 arguments");
            return make_array(0);
        }
        int from = (arg_count == 2) ? to_int(args[0]) : 0;
        int to = (arg_count == 2) ? to_int(args[1]) : to_int(args[0]);
        int n = to > from ? to - from : 0;
        if (n > MAX_ARRAY_SIZE) {
            report_error("range() too large");
            return make_array(0);
        }
        Value arr = make_array(n);
        for (int i = 0; i < n; i++) arr.array_val.elements[i] = make_int(from + i);
        return arr;
    }

    if (strcmp(name, "keys") == 0 || strcmp(name, "values") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_MAP) {
            report_error("keys()/values() require 1 map argument");
            return make_array(0);
        }
        int n = args[0].map_val.size;
        Value arr = make_array(n);
        for (int i = 0; i < n; i++) {
            arr.array_val.elements[i] = (name[0] == 'k') ? make_string(args[0].map_val.keys[i])
                                                          : copy_value(args[0].map_val.values[i]);
        }
        return arr;
    }

    if (strcmp(name, "array") == 0) {
        if (arg_count != 1) {
            report_error("array() requires 1 argument (size)");
            return make_array(0);
        }
        int n = to_int(args[0]);
        if (n < 0 || n > MAX_ARRAY_SIZE) {
            report_error("array() size out of range");
            return make_array(0);
        }
        return make_array(n);
    }

    if (strcmp(name, "sleep") == 0) {
        if (arg_count != 1) {
            report_error("sleep() requires 1 argument (milliseconds)");
            return make_int(0);
        }
        int ms = to_int(args[0]);
        if (ms < 0) ms = 0;
#ifdef _WIN32
        Sleep((DWORD)ms);
#else
        usleep((useconds_t)ms * 1000);
#endif
        return make_int(0);
    }

    if (strcmp(name, "read") == 0) {
        if (arg_count != 1) {
            report_error("read() requires 1 argument (filename)");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("read() requires a string filename");
            return make_string("");
        }

        const char *filename = args[0].str_val;
        FILE *f = fopen(filename, "rb");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot open file for reading: %s", filename);
            report_error(msg);
            return make_string("");
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size < 0) size = 0;
        char *buffer = (char*)malloc((size_t)size + 1);
        if (!buffer) {
            fclose(f);
            report_error("Out of memory reading file");
            return make_string("");
        }
        size_t got = fread(buffer, 1, (size_t)size, f);
        buffer[got] = '\0';
        fclose(f);

        return make_string_take(buffer);
    }

    if (strcmp(name, "write") == 0) {
        if (arg_count != 2) {
            report_error("write() requires 2 arguments (filename, content)");
            return make_int(0);
        }
        if (args[0].type != TYPE_STRING) {
            report_error("write() requires a string filename");
            return make_int(0);
        }

        const char *filename = args[0].str_val;
        const char *content = "";

        if (args[1].type == TYPE_STRING) {
            content = args[1].str_val;
        } else {
            static char temp_str[64];
            if (args[1].type == TYPE_INT) {
                snprintf(temp_str, sizeof(temp_str), "%d", args[1].int_val);
            } else if (args[1].type == TYPE_FLOAT) {
                nac_format_double(args[1].float_val, temp_str, sizeof(temp_str));
            }
            content = temp_str;
        }

        FILE *f = fopen(filename, "wb");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot open file for writing: %s", filename);
            report_error(msg);
            return make_int(0);
        }

        fwrite(content, 1, strlen(content), f);
        fclose(f);

        return make_int(strlen(content));
    }

    if (strcmp(name, "append") == 0) {
        if (arg_count != 2) {
            report_error("append() requires 2 arguments (filename, content)");
            return make_int(0);
        }
        if (args[0].type != TYPE_STRING) {
            report_error("append() requires a string filename");
            return make_int(0);
        }

        const char *filename = args[0].str_val;
        const char *content = "";

        if (args[1].type == TYPE_STRING) {
            content = args[1].str_val;
        } else {
            static char temp_str[64];
            if (args[1].type == TYPE_INT) {
                snprintf(temp_str, sizeof(temp_str), "%d", args[1].int_val);
            } else if (args[1].type == TYPE_FLOAT) {
                nac_format_double(args[1].float_val, temp_str, sizeof(temp_str));
            }
            content = temp_str;
        }

        FILE *f = fopen(filename, "ab");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot open file for appending: %s", filename);
            report_error(msg);
            return make_int(0);
        }

        fwrite(content, 1, strlen(content), f);
        fclose(f);

        return make_int(strlen(content));
    }

    if (strcmp(name, "map") == 0) {
        if (arg_count != 0) {
            report_error("map() requires 0 arguments");
            return make_map();
        }
        return make_map();
    }

    if (strcmp(name, "push") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_ARRAY) {
            report_error("push() requires 2 arguments (array, value)");
            return make_int(0);
        }
        if (args[0].array_val.size >= args[0].array_val.capacity) {
            int new_cap = (args[0].array_val.capacity == 0) ? 4 : args[0].array_val.capacity * 2;
            args[0].array_val.elements = (Value*)realloc(args[0].array_val.elements, sizeof(Value) * new_cap);
            args[0].array_val.capacity = new_cap;
        }
        args[0].array_val.elements[args[0].array_val.size++] = copy_value(args[1]);
        return make_int(args[0].array_val.size);
    }

    if (strcmp(name, "pop") == 0) {
        if (arg_count != 1) {
            report_error("pop() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type != TYPE_ARRAY || args[0].array_val.size == 0) {
            report_error("pop() on empty array");
            return make_int(0);
        }
        int last_idx = args[0].array_val.size - 1;
        Value last = copy_value(args[0].array_val.elements[last_idx]);
        free_value(&args[0].array_val.elements[last_idx]);
        args[0].array_val.size--;
        return last;
    }

    if (strcmp(name, "removeAt") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_ARRAY || args[1].type != TYPE_INT) {
            report_error("removeAt() requires an array and an integer index");
            return make_null();
        }
        int size = args[0].array_val.size;
        int idx = args[1].int_val;
        if (idx < 0 || idx >= size) {
            char msg[128];
            snprintf(msg, sizeof(msg), "removeAt() index %d out of range (array length %d)", idx, size);
            report_error(msg);
            return make_null();
        }
        Value removed = copy_value(args[0].array_val.elements[idx]);
        free_value(&args[0].array_val.elements[idx]);
        memmove(&args[0].array_val.elements[idx], &args[0].array_val.elements[idx + 1],
                sizeof(Value) * (size_t)(size - idx - 1));
        args[0].array_val.size--;
        return removed;
    }

    if (strcmp(name, "delete") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_MAP || args[1].type != TYPE_STRING) {
            report_error("delete() requires a map and a string key");
            return make_bool(0);
        }
        Value *m = &args[0];
        for (int i = 0; i < m->map_val.size; i++) {
            if (strcmp(m->map_val.keys[i], args[1].str_val) != 0) continue;
            free(m->map_val.keys[i]);
            free_value(&m->map_val.values[i]);
            int tail = m->map_val.size - i - 1;
            if (tail > 0) {
                memmove(&m->map_val.keys[i], &m->map_val.keys[i + 1], sizeof(char*) * (size_t)tail);
                memmove(&m->map_val.values[i], &m->map_val.values[i + 1], sizeof(Value) * (size_t)tail);
            }
            m->map_val.size--;
            return make_bool(1);
        }
        return make_bool(0);
    }

    report_error("Unknown built-in function");
    return make_int(0);
}


