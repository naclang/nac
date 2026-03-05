#include "value.h"

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
    val.int_val = v;
    return val;
}

Value make_float(double v) {
    Value val;
    val.type = TYPE_FLOAT;
    val.float_val = v;
    return val;
}

Value make_string(const char *s) {
    Value val;
    val.type = TYPE_STRING;
    strncpy(val.str_val, s, MAX_STRING_LEN - 1);
    val.str_val[MAX_STRING_LEN - 1] = '\0';
    return val;
}

Value make_array(int size) {
    Value val;
    val.type = TYPE_ARRAY;
    val.array_val.size = size;
    val.array_val.capacity = size;
    val.array_val.elements = (Value*)calloc(size, sizeof(Value));
    for (int i = 0; i < size; i++) {
        val.array_val.elements[i] = make_int(0);
    }
    return val;
}

Value make_map(void) {
    Value val;
    val.type = TYPE_MAP;
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
        case TYPE_ARRAY: return 0.0;
        case TYPE_MAP: return 0.0;
    }
    return 0.0;
}

int to_int(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val;
        case TYPE_FLOAT: return (int)v.float_val;
        case TYPE_STRING: return atoi(v.str_val);
        case TYPE_ARRAY: return v.array_val.size;
        case TYPE_MAP: return v.map_val.size;
    }
    return 0;
}

int to_bool(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val != 0;
        case TYPE_FLOAT: return v.float_val != 0.0;
        case TYPE_STRING: return strlen(v.str_val) > 0;
        case TYPE_ARRAY: return v.array_val.size > 0;
        case TYPE_MAP: return v.map_val.size > 0;
    }
    return 0;
}

void print_value(Value v) {
    switch (v.type) {
        case TYPE_INT: printf("%d\n", v.int_val); break;
        case TYPE_FLOAT: printf("%g\n", v.float_val); break;
        case TYPE_STRING: printf("%s\n", v.str_val); break;
        case TYPE_ARRAY:
            printf("[");
            for (int i = 0; i < v.array_val.size; i++) {
                if (i > 0) printf(", ");
                switch (v.array_val.elements[i].type) {
                    case TYPE_INT: printf("%d", v.array_val.elements[i].int_val); break;
                    case TYPE_FLOAT: printf("%g", v.array_val.elements[i].float_val); break;
                    case TYPE_STRING: printf("\"%s\"", v.array_val.elements[i].str_val); break;
                    case TYPE_ARRAY: printf("[...]"); break;
                    case TYPE_MAP: printf("{...}"); break;
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
                    case TYPE_FLOAT: printf("%g", v.map_val.values[i].float_val); break;
                    case TYPE_STRING: printf("\"%s\"", v.map_val.values[i].str_val); break;
                    case TYPE_ARRAY: printf("[...]"); break;
                    case TYPE_MAP: printf("{...}"); break;
                }
            }
            printf("}\n");
            break;
    }
}

Value copy_value(Value v) {
    if (v.type == TYPE_ARRAY) {
        Value new_val;
        new_val.type = TYPE_ARRAY;
        new_val.array_val.size = v.array_val.size;
        new_val.array_val.capacity = v.array_val.size;
        new_val.array_val.elements = (Value*)malloc(sizeof(Value) * new_val.array_val.capacity);
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
            new_val.map_val.keys = (char**)malloc(sizeof(char*) * new_val.map_val.capacity);
            new_val.map_val.values = (Value*)malloc(sizeof(Value) * new_val.map_val.capacity);

            for (int i = 0; i < v.map_val.size; i++) {
                size_t len = strlen(v.map_val.keys[i]);
                new_val.map_val.keys[i] = (char*)malloc(len + 1);
                memcpy(new_val.map_val.keys[i], v.map_val.keys[i], len + 1);
                new_val.map_val.values[i] = copy_value(v.map_val.values[i]);
            }
        }
        return new_val;
    }

    return v;
}

void free_value(Value *v) {
    if (v->type == TYPE_ARRAY && v->array_val.elements) {
        for (int i = 0; i < v->array_val.size; i++) {
            free_value(&v->array_val.elements[i]);
        }
        free(v->array_val.elements);
        v->array_val.elements = NULL;
        v->array_val.size = 0;
        v->array_val.capacity = 0;
        return;
    }

    if (v->type == TYPE_MAP) {
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
        free_value(&map->map_val.values[idx]);
        map->map_val.values[idx] = copy_value(value);
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
