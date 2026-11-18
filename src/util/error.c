#include "error.h"

#include <stdio.h>
#include <stdlib.h>

#include "../core/interpreter.h"

void report_error(const char *msg) {
    if (try_depth > 0) {
        /* inside try { }: remember the first error for `catch`, stay silent */
        if (!error_pending) {
            snprintf(caught_error, sizeof(caught_error), "%s", msg);
            error_pending = true;
        }
        return;
    }
    int line = nac_eval_line > 0 ? nac_eval_line : current_token.line;
    int col = nac_eval_line > 0 ? nac_eval_col : current_token.col;
    fprintf(stderr, "Error (Line %d, Column %d): %s\n", line, col, msg);
    error_occurred = true;
    error_count++;
}

void error_and_exit(const char *msg) {
    try_depth = 0;   /* fatal: never swallow it in a try */
    report_error(msg);
    exit(1);
}
