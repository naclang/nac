#ifndef NAC_VALUE_H
#define NAC_VALUE_H

#include <stddef.h>

#include "../lexer/token.h"

#define MAX_ARRAY_SIZE 10000

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_ARRAY,
    TYPE_MAP,
    TYPE_BOOL,
    TYPE_NULL,
    TYPE_FUNC      /* first-class function; str_val holds the function's internal name */
} ValueType;

typedef struct Value {
    ValueType type;
    /* 1 when the heap storage behind str_val/array_val/map_val belongs to
     * this Value and must be released by free_value(). Reads that point
     * into a variable or container (a[0], m.key) are borrowed: owned = 0. */
    unsigned char owned;
    union {
        int int_val;
        double float_val;
        char *str_val;
        struct {
            struct Value *elements;
            int size;
            int capacity;
        } array_val;
        struct {
            char **keys;
            struct Value *values;
            int size;
            int capacity;
        } map_val;
    };
} Value;

Value make_int(int v);
Value make_float(double v);

/* Allocating constructors: the resulting Value owns its storage. */
Value make_string(const char *s);
Value make_string_len(const char *s, size_t len);
Value make_string_take(char *s);      /* takes ownership of a malloc'd string */

/* Non-owning view of an existing string. The caller must keep it alive. */
Value make_string_borrowed(const char *s);

Value make_array(int size);
Value make_map(void);
Value make_bool(int v);
Value make_null(void);
Value make_func(const char *name);

double to_float(Value v);
int to_int(Value v);
int to_bool(Value v);

void print_value(Value v);

/* Formats a float the way NaC displays it: like "%g", except that whole
 * numbers below 1e15 are written out in full ("1731600000123", not
 * "1.7316e+12"), so large timestamps stay readable. */
void nac_format_double(double v, char *buf, size_t cap);
Value copy_value(Value v);
void free_value(Value *v);

Value *map_get(Value *map, const char *key);
void map_set(Value *map, const char *key, Value value);

#endif
