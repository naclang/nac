#include "io.h"

#include <stdio.h>
#include <stdlib.h>

char *read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = (char*)malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';

    fclose(f);
    return content;
}
