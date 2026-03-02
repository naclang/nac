#include <stdio.h>
#include <string.h>

#include "core/interpreter.h"
#include "io/io.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("NaC Language Interpreter (%s)\n", NAC_VERSION);
        printf("Usage: %s <file.nac>\n\n", argv[0]);

        get_latest();

        if (strcmp(latest, "UNKNOWN") == 0) {
            printf("Could not check for updates.\n");
        } else if (compare_versions(latest, NAC_TAG) > 0) {
            printf("There's a new version: %s (local: %s)\n", latest, NAC_TAG);
        }

        return 1;
    }

    init_interpreter();

    set_source_code(read_file(argv[1]));

    int exit_code = run_interpreter();
    shutdown_interpreter();

    return exit_code;
}
