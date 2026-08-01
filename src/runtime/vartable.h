#ifndef NAC_VARTABLE_H
#define NAC_VARTABLE_H

#include "../lexer/token.h"
#include "value.h"

#define HASH_TABLE_SIZE 256

typedef struct VarEntry {
    char name[MAX_TOKEN_LEN];
    Value value;
    struct VarEntry *next;
} VarEntry;

typedef struct {
    VarEntry *buckets[HASH_TABLE_SIZE];
} VarTable;

unsigned int hash(const char *str);
VarTable *create_var_table(void);
void free_var_table(VarTable *table);
Value *get_var(const char *name);
void set_var(const char *name, Value value);

/* Always creates/updates the variable in the CURRENT function's local
 * scope, even if a global of the same name exists. Used exclusively for
 * binding function parameters, so a parameter (e.g. a handler's `path`)
 * can never accidentally alias an unrelated global variable of the same
 * name. Plain assignment (`x = ...;`) goes through set_var() instead,
 * which updates an existing outer/global variable if one exists. */
void declare_local_var(const char *name, Value value);

#endif
