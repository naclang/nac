#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/resource.h>
#endif

#include "core/compile.h"
#include "core/interpreter.h"
#include "io/io.h"

/* NaC's Value struct embeds a fixed 8192-byte string buffer (sizeof(Value)
 * is ~8.2KB), and several Values are stack-allocated per level of the C
 * call chain used to evaluate one level of NaC function recursion
 * (eval_node <-> call_nac_function). That costs roughly 150-200KB of real
 * C stack per level of NaC recursion -- so MAX_CALL_DEPTH's documented
 * cap of 100 levels needs on the order of 15-20MB of stack just to be
 * reachable at all, well above the ~8MB a typical Linux process gets by
 * default (and far above Windows' ~1MB default, which is why deeply
 * recursive scripts could silently crash there even at depth ~10 instead
 * of hitting the interpreter's own graceful "Stack overflow" error).
 *
 * The real fix -- shrinking Value so it doesn't carry a giant inline
 * buffer -- is a larger refactor (tracked in Roadmap.md). Until then, ask
 * for a generous stack up front so the documented call-depth limit is
 * actually the thing that fires, rather than the OS.
 */
static void raise_stack_limit(void) {
#ifndef _WIN32
    struct rlimit rl;
    const rlim_t desired = 128 * 1024 * 1024; /* 128MB: comfortable multiple of the ~20MB MAX_CALL_DEPTH needs */

    if (getrlimit(RLIMIT_STACK, &rl) != 0) {
        return;
    }

    rlim_t target = desired;
    if (rl.rlim_max != RLIM_INFINITY && target > rl.rlim_max) {
        target = rl.rlim_max;
    }

    if (rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur >= target) {
        return; /* already generous enough */
    }

    rl.rlim_cur = target;
    setrlimit(RLIMIT_STACK, &rl); /* best-effort; ignore failure */
#endif
    /* On Windows, the main thread's stack size is fixed at link time
     * (see build.bat's -Wl,--stack flag) and cannot be resized here. */
}

int main(int argc, char *argv[]) {
    raise_stack_limit();

    if (argc >= 2 && strcmp(argv[1], "build") == 0) {
        return cmd_build(argc, argv);
    }

    if (argc < 2) {
        printf("NaC Language Interpreter (%s)\n", NAC_VERSION);
        printf("Usage: %s <file.nac> [script args...]\n", argv[0]);
        printf("       %s build <file.nac> [-o <output>]   (package as a standalone executable)\n\n", argv[0]);

        get_latest();

        if (strcmp(latest, "UNKNOWN") == 0) {
            printf("Could not check for updates.\n");
        } else if (compare_versions(latest, NAC_TAG) > 0) {
            printf("There's a new version: %s (local: %s)\n", latest, NAC_TAG);
        }

        return 1;
    }

    init_interpreter();

    /* Everything after the script filename is made available to the
     * script itself via args(), e.g. `nac tool.nac build --release`. */
    set_cli_args(argc - 2 > 0 ? argc - 2 : 0, argc > 2 ? &argv[2] : NULL);

    set_source_code(read_file(argv[1]));

    int exit_code = run_interpreter();
    shutdown_interpreter();

    return exit_code;
}
