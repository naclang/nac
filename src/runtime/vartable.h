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

#endif
