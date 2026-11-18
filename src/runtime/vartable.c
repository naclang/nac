#include "vartable.h"

#include <stdlib.h>
#include <string.h>

#include "../core/interpreter.h"

unsigned int hash(const char *str) {
    unsigned int hash_value = 5381;
    int c;
    while ((c = *str++)) {
        hash_value = ((hash_value << 5) + hash_value) + c;
    }
    return hash_value | 0x80000000u;   /* never 0: 0 means "not computed" in the AST */
}

static VarTable *alloc_table(unsigned int size) {
    VarTable *table = (VarTable*)calloc(1, sizeof(VarTable) + sizeof(VarEntry*) * size);
    if (table) table->mask = size - 1;
    return table;
}

VarTable *create_var_table(void) { return alloc_table(HASH_TABLE_SIZE); }
VarTable *create_local_var_table(void) { return alloc_table(LOCAL_HASH_SIZE); }

void free_var_table(VarTable *table) {
    for (unsigned int i = 0; i <= table->mask; i++) {
        VarEntry *entry = table->buckets[i];
        while (entry) {
            VarEntry *next = entry->next;
            free_value(&entry->value);
            free(entry);
            entry = next;
        }
    }
    free(table);
}

static VarEntry *find_entry(VarTable *table, const char *name, unsigned int h) {
    VarEntry *entry = table->buckets[h & table->mask];
    while (entry) {
        if (entry->hash == h && strcmp(entry->name, name) == 0) return entry;
        entry = entry->next;
    }
    return NULL;
}

static void add_entry(VarTable *table, const char *name, unsigned int h, Value value) {
    size_t len = strlen(name);
    if (len > MAX_TOKEN_LEN - 1) len = MAX_TOKEN_LEN - 1;
    VarEntry *e = (VarEntry*)malloc(sizeof(VarEntry) + len + 1);
    memcpy(e->name, name, len);
    e->name[len] = '\0';
    e->hash = h;
    e->value = copy_value(value);
    e->next = table->buckets[h & table->mask];
    table->buckets[h & table->mask] = e;
}

static void assign_entry(VarEntry *entry, Value value) {
    Value copy = copy_value(value);
    free_value(&entry->value);
    entry->value = copy;
}

Value *get_var(const char *name) {
    return get_var_h(name, hash(name));
}

Value *get_var_h(const char *name, unsigned int h) {
    if (call_depth > 0) {
        VarEntry *e = find_entry(call_stack_vars[call_depth - 1], name, h);
        if (e) return &e->value;
    }
    VarEntry *e = find_entry(global_vars, name, h);
    return e ? &e->value : NULL;
}

void set_var(const char *name, Value value) {
    set_var_h(name, hash(name), value);
}

void set_var_h(const char *name, unsigned int h, Value value) {

    if (call_depth > 0) {
        VarTable *local = call_stack_vars[call_depth - 1];
        VarEntry *e = find_entry(local, name, h);
        if (e) { assign_entry(e, value); return; }

        /* Not already a local variable in this call -- if it exists in
         * the global scope, update THAT instead of silently shadowing it
         * with a new local one. This is what lets a function mutate
         * shared global state (counters, caches, etc.) as expected. */
        e = find_entry(global_vars, name, h);
        if (e) { assign_entry(e, value); return; }

        /* Genuinely new variable inside a function -> local. */
        add_entry(local, name, h, value);
        return;
    }

    VarEntry *e = find_entry(global_vars, name, h);
    if (e) { assign_entry(e, value); return; }
    add_entry(global_vars, name, h, value);
}

void declare_local_var(const char *name, Value value) {
    VarTable *table = (call_depth > 0) ? call_stack_vars[call_depth - 1] : global_vars;
    unsigned int h = hash(name);
    VarEntry *e = find_entry(table, name, h);
    if (e) { assign_entry(e, value); return; }
    add_entry(table, name, h, value);
}
