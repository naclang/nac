#ifndef NAC_VARTABLE_H
#define NAC_VARTABLE_H

#include "../lexer/token.h"
#include "value.h"

#define HASH_TABLE_SIZE 256      /* global scope */
#define LOCAL_HASH_SIZE 16       /* per-call scope: small, cheap to create/free */

typedef struct VarEntry {
    Value value;
    struct VarEntry *next;
    unsigned int hash;          /* full hash, compared before strcmp */
    char name[];                /* exact-length name (flexible array) */
} VarEntry;

typedef struct {
    unsigned int mask;          /* bucket count - 1 (power of two) */
    VarEntry *buckets[];
} VarTable;

unsigned int hash(const char *str);
VarTable *create_var_table(void);        /* global-size table */
VarTable *create_local_var_table(void);  /* small per-call table */
void free_var_table(VarTable *table);
Value *get_var(const char *name);
Value *get_var_h(const char *name, unsigned int h);   /* h from hash() */
void set_var_h(const char *name, unsigned int h, Value value);
void set_var(const char *name, Value value);

/* Always creates/updates the variable in the CURRENT function's local
 * scope, even if a global of the same name exists. Used exclusively for
 * binding function parameters, so a parameter (e.g. a handler's `path`)
 * can never accidentally alias an unrelated global variable of the same
 * name. Plain assignment (`x = ...;`) goes through set_var() instead,
 * which updates an existing outer/global variable if one exists. */
void declare_local_var(const char *name, Value value);

#endif
