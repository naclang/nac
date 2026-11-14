#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "core/compile.h"
#include "core/interpreter.h"
#include "io/io.h"

static int file_signature(const char *path, uint64_t *signature) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < n; i++) {
            hash ^= buffer[i];
            hash *= UINT64_C(1099511628211);
        }
    }
    int failed = ferror(file);
    fclose(file);
    if (failed) return 0;
    *signature = hash;
    return 1;
}

static int dev_signature(const char *script_path, uint64_t *signature) {
    uint64_t script_signature;
    uint64_t env_signature = 0;
    int has_env = file_signature(".env", &env_signature);
    if (!file_signature(script_path, &script_signature)) return 0;
    script_signature ^= env_signature + UINT64_C(0x9e3779b97f4a7c15) +
                        (script_signature << 6) + (script_signature >> 2);
    if (has_env) script_signature ^= UINT64_C(0xd6e8feb86659fd93);
    *signature = script_signature;
    return 1;
}

static void dev_poll_pause(void) {
#ifdef _WIN32
    Sleep(250);
#else
    struct timespec delay = { 0, 250000000L };
    nanosleep(&delay, NULL);
#endif
}

#ifdef _WIN32
static int append_windows_arg(char *out, size_t cap, size_t *used, const char *arg) {
    if (*used + 1 >= cap) return 0;
    out[(*used)++] = '"';
    size_t slashes = 0;
    for (const char *p = arg; ; p++) {
        if (*p == '\\') {
            slashes++;
            continue;
        }
        if (*p == '"') {
            while (slashes-- > 0) {
                if (*used + 2 >= cap) return 0;
                out[(*used)++] = '\\';
                out[(*used)++] = '\\';
            }
            if (*used + 2 >= cap) return 0;
            out[(*used)++] = '\\';
            out[(*used)++] = '"';
            slashes = 0;
            continue;
        }
        if (*p == '\0') {
            while (slashes-- > 0) {
                if (*used + 2 >= cap) return 0;
                out[(*used)++] = '\\';
                out[(*used)++] = '\\';
            }
            break;
        }
        while (slashes-- > 0) {
            if (*used + 1 >= cap) return 0;
            out[(*used)++] = '\\';
        }
        slashes = 0;
        if (*used + 1 >= cap) return 0;
        out[(*used)++] = *p;
    }
    if (*used + 2 >= cap) return 0;
    out[(*used)++] = '"';
    out[*used] = '\0';
    return 1;
}

static int start_dev_child(const char *exe, int argc, char **argv, PROCESS_INFORMATION *process) {
    char command[8192] = "";
    size_t used = 0;
    if (!append_windows_arg(command, sizeof(command), &used, exe)) return 0;
    for (int i = 2; i < argc; i++) {
        if (used + 1 >= sizeof(command)) return 0;
        command[used++] = ' ';
        command[used] = '\0';
        if (!append_windows_arg(command, sizeof(command), &used, argv[i])) return 0;
    }
    STARTUPINFOA startup;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    memset(process, 0, sizeof(*process));
    return CreateProcessA(exe, command, NULL, NULL, TRUE, 0, NULL, NULL,
                          &startup, process) != 0;
}
#endif

static int cmd_dev(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s dev <file.nac> [script args...]\n", argv[0]);
        return 1;
    }
    uint64_t previous;
    if (!dev_signature(argv[2], &previous)) {
        fprintf(stderr, "[ERROR] Cannot watch script: %s\n", argv[2]);
        return 1;
    }

#ifdef _WIN32
    char exe[MAX_PATH];
    DWORD exe_len = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (exe_len == 0 || exe_len >= sizeof(exe)) {
        fprintf(stderr, "[ERROR] Could not locate nac.exe.\n");
        return 1;
    }
    for (;;) {
        PROCESS_INFORMATION process;
        if (!start_dev_child(exe, argc, argv, &process)) {
            fprintf(stderr, "[ERROR] Could not start development process.\n");
            return 1;
        }
        printf("[dev] running %s (watching for changes)\n", argv[2]);
        fflush(stdout);
        int changed = 0;
        for (;;) {
            DWORD wait = WaitForSingleObject(process.hProcess, 250);
            uint64_t current;
            if (dev_signature(argv[2], &current) && current != previous) {
                previous = current;
                changed = 1;
                TerminateProcess(process.hProcess, 0);
                WaitForSingleObject(process.hProcess, INFINITE);
                break;
            }
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_FAILED) {
                fprintf(stderr, "[ERROR] Failed while watching development process.\n");
                TerminateProcess(process.hProcess, 1);
                WaitForSingleObject(process.hProcess, INFINITE);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                return 1;
            }
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (changed) {
            printf("[dev] script changed; restarting\n");
            fflush(stdout);
        } else {
            printf("[dev] script exited; waiting for a change\n");
            fflush(stdout);
            for (;;) {
                dev_poll_pause();
                uint64_t current;
                if (dev_signature(argv[2], &current) && current != previous) {
                    previous = current;
                    break;
                }
            }
        }
    }
#else
    for (;;) {
        pid_t child = fork();
        if (child < 0) {
            perror("[ERROR] fork");
            return 1;
        }
        if (child == 0) {
            char **child_argv = (char**)calloc((size_t)argc, sizeof(char*));
            if (!child_argv) _exit(127);
            child_argv[0] = argv[0];
            for (int i = 2; i < argc; i++) child_argv[i - 1] = argv[i];
            execvp(argv[0], child_argv);
            perror("[ERROR] exec");
            _exit(127);
        }
        printf("[dev] running %s (watching for changes)\n", argv[2]);
        fflush(stdout);
        int changed = 0;
        for (;;) {
            int status;
            pid_t done = waitpid(child, &status, WNOHANG);
            if (done == child) break;
            if (done < 0) {
                if (errno == EINTR) continue;
                perror("[ERROR] waitpid");
                kill(child, SIGTERM);
                waitpid(child, NULL, 0);
                return 1;
            }
            dev_poll_pause();
            uint64_t current;
            if (dev_signature(argv[2], &current) && current != previous) {
                previous = current;
                changed = 1;
                kill(child, SIGTERM);
                waitpid(child, NULL, 0);
                break;
            }
        }
        if (changed) {
            printf("[dev] script changed; restarting\n");
            fflush(stdout);
        } else {
            printf("[dev] script exited; waiting for a change\n");
            fflush(stdout);
            for (;;) {
                dev_poll_pause();
                uint64_t current;
                if (dev_signature(argv[2], &current) && current != previous) {
                    previous = current;
                    break;
                }
            }
        }
    }
#endif
}

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
    if (argc >= 2 && strcmp(argv[1], "dev") == 0) {
        return cmd_dev(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "test") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s test <file.nac> [script args...]\n", argv[0]);
            return 1;
        }
        init_interpreter();
        nac_test_mode = true;
        set_cli_args(argc - 3 > 0 ? argc - 3 : 0, argc > 3 ? &argv[3] : NULL);
        set_source_code(read_file(argv[2]));
        int exit_code = run_interpreter();
        shutdown_interpreter();
        if (exit_code == 0) {
            puts("Tests passed.");
        }
        return exit_code;
    }

    if (argc < 2) {
        printf("NaC Language Interpreter (%s)\n", NAC_VERSION);
        printf("Usage: %s <file.nac> [script args...]\n", argv[0]);
        printf("       %s build <file.nac> [-o <output>]   (package as a standalone executable)\n\n", argv[0]);
        printf("       %s dev <file.nac> [script args...]   (restart on save)\n", argv[0]);
        printf("       %s test <file.nac> [script args...]  (run assertions)\n\n", argv[0]);

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
