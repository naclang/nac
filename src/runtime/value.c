#include "value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int map_find_key(const Value *map, const char *key) {
    if (!map || map->type != TYPE_MAP) {
        return -1;
    }

    for (int i = 0; i < map->map_val.size; i++) {
        if (strcmp(map->map_val.keys[i], key) == 0) {
            return i;
        }
    }

    return -1;
}

Value make_int(int v) {
    Value val;
    val.type = TYPE_INT;
    val.owned = 0;
    val.int_val = v;
    return val;
}

Value make_float(double v) {
    Value val;
    val.type = TYPE_FLOAT;
    val.owned = 0;
    val.float_val = v;
    return val;
}

Value make_string_borrowed(const char *s) {
    Value val;
    val.type = TYPE_STRING;
    val.owned = 0;
    val.str_val = (char*)(s ? s : "");
    return val;
}

Value make_string_take(char *s) {
    if (!s) return make_string_borrowed("");
    Value val;
    val.type = TYPE_STRING;
    val.owned = 1;
    val.str_val = s;
    return val;
}

Value make_string_len(const char *s, size_t len) {
    if (!s) {
        s = "";
        len = 0;
    }
    char *copy = (char*)malloc(len + 1);
    if (!copy) return make_string_borrowed("");
    if (len > 0) memcpy(copy, s, len);
    copy[len] = '\0';
    return make_string_take(copy);
}

Value make_string(const char *s) {
    if (!s) s = "";
    return make_string_len(s, strlen(s));
}

Value make_func(const char *name) {
    if (!name) name = "";
    size_t len = strlen(name);
    char *copy = (char*)malloc(len + 1);
    if (!copy) {
        Value val;
        val.type = TYPE_FUNC;
        val.owned = 0;
        val.str_val = (char*)"";
        return val;
    }
    memcpy(copy, name, len + 1);
    Value val;
    val.type = TYPE_FUNC;
    val.owned = 1;
    val.str_val = copy;
    return val;
}

Value make_array(int size) {
    if (size < 0) size = 0;
    Value val;
    val.type = TYPE_ARRAY;
    val.owned = 1;
    val.array_val.size = size;
    val.array_val.capacity = size;
    val.array_val.elements = (Value*)calloc((size_t)size, sizeof(Value));
    if (!val.array_val.elements && size > 0) {
        val.array_val.size = 0;
        val.array_val.capacity = 0;
        size = 0;
    }
    for (int i = 0; i < size; i++) {
        val.array_val.elements[i] = make_int(0);
    }
    return val;
}

Value make_bool(int v) {
    Value val;
    val.type = TYPE_BOOL;
    val.owned = 0;
    val.int_val = v ? 1 : 0;
    return val;
}

Value make_null(void) {
    Value val;
    val.type = TYPE_NULL;
    val.owned = 0;
    val.int_val = 0;
    return val;
}

Value make_map(void) {
    Value val;
    val.type = TYPE_MAP;
    val.owned = 1;
    val.map_val.keys = NULL;
    val.map_val.values = NULL;
    val.map_val.size = 0;
    val.map_val.capacity = 0;
    return val;
}

double to_float(Value v) {
    switch (v.type) {
        case TYPE_INT: return (double)v.int_val;
        case TYPE_FLOAT: return v.float_val;
        case TYPE_STRING: return atof(v.str_val);
        case TYPE_BOOL: return (double)v.int_val;
        case TYPE_NULL: return 0.0;
        case TYPE_ARRAY: return 0.0;
        case TYPE_MAP: return 0.0;
        case TYPE_FUNC: return 0.0;
    }
    return 0.0;
}

int to_int(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val;
        case TYPE_FLOAT: return (int)v.float_val;
        case TYPE_STRING: return atoi(v.str_val);
        case TYPE_BOOL: return v.int_val;
        case TYPE_NULL: return 0;
        case TYPE_ARRAY: return v.array_val.size;
        case TYPE_MAP: return v.map_val.size;
        case TYPE_FUNC: return 0;
    }
    return 0;
}

int to_bool(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val != 0;
        case TYPE_FLOAT: return v.float_val != 0.0;
        case TYPE_STRING: return strlen(v.str_val) > 0;
        case TYPE_BOOL: return v.int_val != 0;
        case TYPE_NULL: return 0;
        case TYPE_ARRAY: return v.array_val.size > 0;
        case TYPE_MAP: return v.map_val.size > 0;
        case TYPE_FUNC: return 1;
    }
    return 0;
}

void nac_format_double(double v, char *buf, size_t cap) {
    if (cap == 0) return;
    if (isfinite(v) && v == floor(v) && fabs(v) < 1e15) {
        snprintf(buf, cap, "%.0f", v);
    } else {
        snprintf(buf, cap, "%g", v);
    }
}

static void print_float(double v) {
    char num[64];
    nac_format_double(v, num, sizeof(num));
    fputs(num, stdout);
}

void print_value(Value v) {
    switch (v.type) {
        case TYPE_INT: printf("%d\n", v.int_val); break;
        case TYPE_FLOAT: print_float(v.float_val); putchar('\n'); break;
        case TYPE_STRING: printf("%s\n", v.str_val); break;
        case TYPE_BOOL: printf("%s\n", v.int_val ? "true" : "false"); break;
        case TYPE_NULL: printf("null\n"); break;
        case TYPE_FUNC: printf("<fn>\n"); break;
        case TYPE_ARRAY:
            printf("[");
            for (int i = 0; i < v.array_val.size; i++) {
                if (i > 0) printf(", ");
                switch (v.array_val.elements[i].type) {
                    case TYPE_INT: printf("%d", v.array_val.elements[i].int_val); break;
                    case TYPE_FLOAT: print_float(v.array_val.elements[i].float_val); break;
                    case TYPE_STRING: printf("\"%s\"", v.array_val.elements[i].str_val); break;
                    case TYPE_BOOL: printf("%s", v.array_val.elements[i].int_val ? "true" : "false"); break;
                    case TYPE_NULL: printf("null"); break;
                    case TYPE_ARRAY: printf("[...]"); break;
                    case TYPE_MAP: printf("{...}"); break;
                    case TYPE_FUNC: printf("<fn>"); break;
                }
            }
            printf("]\n");
            break;
        case TYPE_MAP:
            printf("{");
            for (int i = 0; i < v.map_val.size; i++) {
                if (i > 0) printf(", ");
                printf("\"%s\": ", v.map_val.keys[i]);
                switch (v.map_val.values[i].type) {
                    case TYPE_INT: printf("%d", v.map_val.values[i].int_val); break;
                    case TYPE_FLOAT: print_float(v.map_val.values[i].float_val); break;
                    case TYPE_STRING: printf("\"%s\"", v.map_val.values[i].str_val); break;
                    case TYPE_BOOL: printf("%s", v.map_val.values[i].int_val ? "true" : "false"); break;
                    case TYPE_NULL: printf("null"); break;
                    case TYPE_ARRAY: printf("[...]"); break;
                    case TYPE_MAP: printf("{...}"); break;
                    case TYPE_FUNC: printf("<fn>"); break;
                }
            }
            printf("}\n");
            break;
    }
}

Value copy_value(Value v) {
    if (v.type == TYPE_STRING) {
        /* Always a real copy, even of a borrowed view: a view points into a
         * variable or container that may be changed or freed later (for
         * example `n = m.name; m.name = "x"`), which would leave the stored
         * value dangling. */
        if (v.str_val) return make_string(v.str_val);
        v.owned = 0;
        return v;
    }

    if (v.type == TYPE_FUNC) {
        if (v.owned && v.str_val) {
            Value copy = make_string(v.str_val);
            copy.type = v.type;
            return copy;
        }
        v.owned = 0;
        return v;
    }

    if (v.type == TYPE_ARRAY) {
        Value new_val;
        new_val.type = TYPE_ARRAY;
        new_val.owned = 1;
        new_val.array_val.size = v.array_val.size;
        new_val.array_val.capacity = v.array_val.size;
        new_val.array_val.elements = (Value*)malloc(sizeof(Value) * (size_t)new_val.array_val.capacity);
        if (!new_val.array_val.elements) {
            new_val.array_val.size = 0;
            new_val.array_val.capacity = 0;
            return new_val;
        }
        for (int i = 0; i < v.array_val.size; i++) {
            new_val.array_val.elements[i] = copy_value(v.array_val.elements[i]);
        }
        return new_val;
    }

    if (v.type == TYPE_MAP) {
        Value new_val = make_map();
        if (v.map_val.size > 0) {
            new_val.map_val.capacity = v.map_val.size;
            new_val.map_val.size = v.map_val.size;
            new_val.map_val.keys = (char**)malloc(sizeof(char*) * (size_t)new_val.map_val.capacity);
            new_val.map_val.values = (Value*)malloc(sizeof(Value) * (size_t)new_val.map_val.capacity);

            for (int i = 0; i < v.map_val.size; i++) {
                size_t len = strlen(v.map_val.keys[i]);
                new_val.map_val.keys[i] = (char*)malloc(len + 1);
                memcpy(new_val.map_val.keys[i], v.map_val.keys[i], len + 1);
                new_val.map_val.values[i] = copy_value(v.map_val.values[i]);
            }
        }
        return new_val;
    }

    v.owned = 0;
    return v;
}

void free_value(Value *v) {
    if (!v) return;

    if (v->type == TYPE_STRING || v->type == TYPE_FUNC) {
        if (v->owned) free(v->str_val);
        v->str_val = NULL;
        v->owned = 0;
        return;
    }

    if (v->type == TYPE_ARRAY) {
        if (!v->owned) return;
        if (v->array_val.elements) {
            for (int i = 0; i < v->array_val.size; i++) {
                free_value(&v->array_val.elements[i]);
            }
            free(v->array_val.elements);
        }
        v->array_val.elements = NULL;
        v->array_val.size = 0;
        v->array_val.capacity = 0;
        v->owned = 0;
        return;
    }

    if (v->type == TYPE_MAP) {
        if (!v->owned) return;
        for (int i = 0; i < v->map_val.size; i++) {
            free(v->map_val.keys[i]);
            free_value(&v->map_val.values[i]);
        }

        free(v->map_val.keys);
        free(v->map_val.values);

        v->map_val.keys = NULL;
        v->map_val.values = NULL;
        v->map_val.size = 0;
        v->map_val.capacity = 0;
        v->owned = 0;
    }
}

Value *map_get(Value *map, const char *key) {
    int idx = map_find_key(map, key);
    if (idx < 0) {
        return NULL;
    }
    return &map->map_val.values[idx];
}

void map_set(Value *map, const char *key, Value value) {
    if (!map || map->type != TYPE_MAP) {
        return;
    }

    int idx = map_find_key(map, key);
    if (idx >= 0) {
        /* Copy first: `value` may be a view into this very map. */
        Value copy = copy_value(value);
        free_value(&map->map_val.values[idx]);
        map->map_val.values[idx] = copy;
        return;
    }

    if (map->map_val.size >= map->map_val.capacity) {
        int new_capacity = (map->map_val.capacity == 0) ? 8 : map->map_val.capacity * 2;
        map->map_val.keys = (char**)realloc(map->map_val.keys, sizeof(char*) * new_capacity);
        map->map_val.values = (Value*)realloc(map->map_val.values, sizeof(Value) * new_capacity);
        map->map_val.capacity = new_capacity;
    }

    size_t len = strlen(key);
    map->map_val.keys[map->map_val.size] = (char*)malloc(len + 1);
    memcpy(map->map_val.keys[map->map_val.size], key, len + 1);
    map->map_val.values[map->map_val.size] = copy_value(value);
    map->map_val.size++;
}
