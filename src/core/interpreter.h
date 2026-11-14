#ifndef NAC_INTERPRETER_H
#define NAC_INTERPRETER_H

#include <stdbool.h>

#include "../lexer/token.h"
#include "../parser/parser.h"
#include "../runtime/value.h"
#include "../runtime/vartable.h"

#define NAC_VERSION "1.1.0"
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
extern bool nac_test_mode;

/* try/catch support. While try_depth > 0, report_error() does not print or
 * count: it stores the first message in caught_error and sets error_pending,
 * which makes every running block stop at the next statement until the
 * enclosing `try` takes the error and runs its `catch`. */
extern int try_depth;
extern bool error_pending;
extern char caught_error[512];

extern char latest[64];

/* Extra command-line arguments passed after the script name, exposed to
 * NaC scripts via args() for CLI-tool style usage. */
extern int nac_argc;
extern char **nac_argv;
void set_cli_args(int argc, char **argv);

void init_interpreter(void);
void set_source_code(char *source);
int run_interpreter(void);
void shutdown_interpreter(void);

void get_latest(void);
int compare_versions(const char *v1, const char *v2);

#endif

