#ifndef NAC_INTERPRETER_H
#define NAC_INTERPRETER_H

#include <stdbool.h>

#include "../lexer/token.h"
#include "../parser/parser.h"
#include "../runtime/value.h"
#include "../runtime/vartable.h"

#define NAC_VERSION "3.2.0"
#define NAC_TAG "NaC" NAC_VERSION
#define MAX_CALL_DEPTH 100

extern char *code;
extern int pos;
extern int code_len;
extern Token current_token;

extern VarTable *global_vars;
extern VarTable *call_stack_vars[MAX_CALL_DEPTH];
extern int call_depth;

extern Function functions[MAX_FUNCS];
extern int func_count;

extern bool should_break;
extern bool should_continue;
extern bool should_return;
extern Value return_value;

extern bool error_occurred;
extern int error_count;

extern char latest[64];

void init_interpreter(void);
void set_source_code(char *source);
int run_interpreter(void);
void shutdown_interpreter(void);

void get_latest(void);
int compare_versions(const char *v1, const char *v2);

#endif
