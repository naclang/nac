#include "error.h"

#include <stdio.h>
#include <stdlib.h>

#include "../core/interpreter.h"

void report_error(const char *msg) {
    int line = 1;
    int col = 1;
    for (int i = 0; i < pos && i < code_len; i++) {
        if (code[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    fprintf(stderr, "Error (Line %d, Column %d): %s\n", line, col, msg);
    error_occurred = true;
    error_count++;
}

void error_and_exit(const char *msg) {
    report_error(msg);
    exit(1);
}
