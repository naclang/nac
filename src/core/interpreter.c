#include "interpreter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../runtime/eval.h"

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

bool error_occurred = false;
int error_count = 0;

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
}

void set_source_code(char *source) {
    code = source;
    code_len = strlen(code);
    pos = 0;
}

int run_interpreter(void) {
    next_token();

    while (current_token.type != TOK_EOF) {
        ASTNode *stmt = parse_statement();
        if (stmt) {
            eval_node(stmt);
            free_ast(stmt);
        }

        if (error_count > 10) {
            fprintf(stderr, "Too many errors, stopping execution.\n");
            break;
        }
    }

    if (error_occurred) {
        fprintf(stderr, "\nExecution completed with %d error(s).\n", error_count);
        return 1;
    }

    return 0;
}

void shutdown_interpreter(void) {
    free(code);
    free_var_table(global_vars);
}
