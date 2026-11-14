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
    fprintf(stderr, "Error (Line %d, Column %d): %s\n", current_token.line, current_token.col, msg);
    error_occurred = true;
    error_count++;
}

void error_and_exit(const char *msg) {
    try_depth = 0;   /* fatal: never swallow it in a try */
    report_error(msg);
    exit(1);
}
