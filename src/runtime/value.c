#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

double to_float(Value v) {
    switch (v.type) {
        case TYPE_INT: return (double)v.int_val;
        case TYPE_FLOAT: return v.float_val;
        case TYPE_STRING: return atof(v.str_val);
        case TYPE_ARRAY: return 0.0;
    }
    return 0.0;
}

int to_int(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val;
        case TYPE_FLOAT: return (int)v.float_val;
        case TYPE_STRING: return atoi(v.str_val);
        case TYPE_ARRAY: return v.array_val.size;
    }
    return 0;
}

int to_bool(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val != 0;
        case TYPE_FLOAT: return v.float_val != 0.0;
        case TYPE_STRING: return strlen(v.str_val) > 0;
        case TYPE_ARRAY: return v.array_val.size > 0;
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
                    default: printf("?");
                }
            }
            printf("]\n");
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
    return v;
}

void free_value(Value *v) {
    if (v->type == TYPE_ARRAY && v->array_val.elements) {
        for (int i = 0; i < v->array_val.size; i++) {
            free_value(&v->array_val.elements[i]);
        }
        free(v->array_val.elements);
    }
}
