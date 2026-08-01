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
    return hash_value % HASH_TABLE_SIZE;
}

VarTable *create_var_table(void) {
    VarTable *table = (VarTable*)calloc(1, sizeof(VarTable));
    return table;
}

void free_var_table(VarTable *table) {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
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

Value *get_var(const char *name) {
    if (call_depth > 0) {
        VarTable *local = call_stack_vars[call_depth - 1];
        unsigned int idx = hash(name);
        VarEntry *entry = local->buckets[idx];
        while (entry) {
            if (strcmp(entry->name, name) == 0) {
                return &entry->value;
            }
            entry = entry->next;
        }
    }

    unsigned int idx = hash(name);
    VarEntry *entry = global_vars->buckets[idx];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return &entry->value;
        }
        entry = entry->next;
    }

    return NULL;
}

void set_var(const char *name, Value value) {
    unsigned int idx = hash(name);

    if (call_depth > 0) {
        VarTable *local = call_stack_vars[call_depth - 1];
        VarEntry *entry = local->buckets[idx];
        while (entry) {
            if (strcmp(entry->name, name) == 0) {
                free_value(&entry->value);
                entry->value = copy_value(value);
                return;
            }
            entry = entry->next;
        }

        /* Not already a local variable in this call -- if it exists in
         * the global scope, update THAT instead of silently shadowing it
         * with a new local one. This is what lets a function mutate
         * shared global state (counters, caches, etc.) as expected. */
        VarEntry *global_entry = global_vars->buckets[idx];
        while (global_entry) {
            if (strcmp(global_entry->name, name) == 0) {
                free_value(&global_entry->value);
                global_entry->value = copy_value(value);
                return;
            }
            global_entry = global_entry->next;
        }

        /* Genuinely new variable inside a function -> local. */
        VarEntry *new_entry = (VarEntry*)malloc(sizeof(VarEntry));
        strncpy(new_entry->name, name, MAX_TOKEN_LEN - 1);
        new_entry->name[MAX_TOKEN_LEN - 1] = '\0';
        new_entry->value = copy_value(value);
        new_entry->next = local->buckets[idx];
        local->buckets[idx] = new_entry;
        return;
    }

    VarEntry *entry = global_vars->buckets[idx];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            free_value(&entry->value);
            entry->value = copy_value(value);
            return;
        }
        entry = entry->next;
    }

    VarEntry *new_entry = (VarEntry*)malloc(sizeof(VarEntry));
    strncpy(new_entry->name, name, MAX_TOKEN_LEN - 1);
    new_entry->name[MAX_TOKEN_LEN - 1] = '\0';
    new_entry->value = copy_value(value);
    new_entry->next = global_vars->buckets[idx];
    global_vars->buckets[idx] = new_entry;
}

void declare_local_var(const char *name, Value value) {
    VarTable *table = (call_depth > 0) ? call_stack_vars[call_depth - 1] : global_vars;
    unsigned int idx = hash(name);

    VarEntry *entry = table->buckets[idx];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            free_value(&entry->value);
            entry->value = copy_value(value);
            return;
        }
        entry = entry->next;
    }

    VarEntry *new_entry = (VarEntry*)malloc(sizeof(VarEntry));
    strncpy(new_entry->name, name, MAX_TOKEN_LEN - 1);
    new_entry->name[MAX_TOKEN_LEN - 1] = '\0';
    new_entry->value = copy_value(value);
    new_entry->next = table->buckets[idx];
    table->buckets[idx] = new_entry;
}
