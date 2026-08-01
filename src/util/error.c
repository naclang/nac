#include "error.h"

#include <stdio.h>
#include <stdlib.h>

#include "../core/interpreter.h"

void report_error(const char *msg) {
    fprintf(stderr, "Error (Line %d, Column %d): %s\n", current_token.line, current_token.col, msg);
    error_occurred = true;
    error_count++;
}

void error_and_exit(const char *msg) {
    report_error(msg);
    exit(1);
}
