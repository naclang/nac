#ifndef NAC_VALUE_H
#define NAC_VALUE_H

#include "../lexer/token.h"

#define MAX_ARRAY_SIZE 10000

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_ARRAY
} ValueType;

typedef struct Value {
    ValueType type;
    union {
        int int_val;
        double float_val;
        char str_val[MAX_STRING_LEN];
        struct {
            struct Value *elements;
            int size;
            int capacity;
        } array_val;
    };
} Value;

Value make_int(int v);
Value make_float(double v);
Value make_string(const char *s);
Value make_array(int size);

double to_float(Value v);
int to_int(Value v);
int to_bool(Value v);

void print_value(Value v);
Value copy_value(Value v);
void free_value(Value *v);

#endif
