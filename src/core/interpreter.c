#include "interpreter.h"
#include "../builtin/db_builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../runtime/eval.h"
#include "../util/error.h"
#include "config.h"

char *code = NULL;
int pos = 0;
int code_len = 0;
Token current_token;

VarTable *global_vars;
VarTable *call_stack_vars[MAX_CALL_DEPTH];
int call_depth = 0;

Function functions[MAX_FUNCS];
int func_count = 0;

bool should_break = false;
bool should_continue = false;
bool should_return = false;
Value return_value;

/* Source position of the AST node being evaluated, for runtime error
 * messages (0 while parsing: errors then use the lexer's current token). */
int nac_eval_line = 0;
int nac_eval_col = 0;

bool error_occurred = false;
int error_count = 0;
bool nac_test_mode = false;

int try_depth = 0;
bool error_pending = false;
char caught_error[512] = "";

int nac_argc = 0;
char **nac_argv = NULL;

void set_cli_args(int argc, char **argv) {
    nac_argc = argc;
    nac_argv = argv;
}

void init_interpreter(void) {
    global_vars = create_var_table();
    func_count = 0;
    call_depth = 0;
    should_break = false;
    should_continue = false;
    should_return = false;
    return_value = make_int(0);
    error_occurred = false;
    error_count = 0;
    nac_test_mode = false;
    try_depth = 0;
    error_pending = false;
    caught_error[0] = '\0';

    char config_error[512];
    if (nac_load_dotenv(".env", config_error, sizeof(config_error)) < 0) {
        report_error(config_error);
    }
}

void set_source_code(char *source) {
    code = source;
    code_len = strlen(code);
    pos = 0;
}

int run_interpreter(void) {
    if (error_occurred) {
        fprintf(stderr, "\nExecution completed with %d error(s).\n", error_count);
        return 1;
    }

    init_lexer();
    next_token();

    /* Parse the whole program before running any of it: a syntax error
     * anywhere stops it up front, instead of after the statements before the
     * mistake (database writes, requests, ...) have already run. */
    ASTNode **program = NULL;
    int count = 0, capacity = 0;
    while (current_token.type != TOK_EOF) {
        nac_eval_line = 0;      /* parse errors report the lexer's position */
        ASTNode *stmt = parse_statement();
        if (stmt) {
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 64;
                program = (ASTNode**)realloc(program, sizeof(ASTNode*) * (size_t)capacity);
            }
            program[count++] = stmt;
        }

        if (error_count > 10) {
            fprintf(stderr, "Too many errors, stopping execution.\n");
            break;
        }
    }

    if (error_occurred) {
        for (int i = 0; i < count; i++) free_ast(program[i]);
        free(program);
        fprintf(stderr, "\nExecution completed with %d error(s).\n", error_count);
        return 1;
    }

    int executed = 0;
    for (; executed < count; executed++) {
        eval_node(program[executed]);
        free_ast(program[executed]);
        program[executed] = NULL;

        if (nac_test_mode && error_occurred) {
            executed++;
            break;
        }

        if (error_count > 10) {
            fprintf(stderr, "Too many errors, stopping execution.\n");
            executed++;
            break;
        }
    }
    for (int i = executed; i < count; i++) free_ast(program[i]);
    free(program);

    if (error_occurred) {
        fprintf(stderr, "\nExecution completed with %d error(s).\n", error_count);
        return 1;
    }

    return 0;
}

void shutdown_interpreter(void) {
    nac_db_close_all();
    free(code);
    free_lexer();
    free_var_table(global_vars);
}
