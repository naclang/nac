#include "io.h"

#include <stdio.h>

void console_print(const char *text) {
    printf("%s", text);
}

int console_read_line(char *buffer, int size) {
    if (fgets(buffer, size, stdin)) {
        return 1;
    }
    return 0;
}
