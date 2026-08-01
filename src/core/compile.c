#include "compile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../io/io.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define MAX_SRC_FILES 256
#define MAX_PATH_LEN 1024

/* Where nac itself is running from, so we can find the sibling src/
 * directory (the layout build.sh/build.bat leave behind) to recompile
 * against. Returns 1 on success, 0 if it couldn't be determined. */
static int get_exe_dir(char *buf, size_t buf_size) {
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)buf_size);
    if (len == 0 || len == buf_size) {
        return 0;
    }
#else
    ssize_t len = readlink("/proc/self/exe", buf, buf_size - 1);
    if (len <= 0) {
        return 0;
    }
    buf[len] = '\0';
#endif

    char *slash = strrchr(buf, '/');
#ifdef _WIN32
    char *backslash = strrchr(buf, '\\');
    if (!slash || (backslash && backslash > slash)) {
        slash = backslash;
    }
#endif
    if (slash) {
        *slash = '\0';
    } else {
        buf[0] = '\0';
    }
    return 1;
}

/* Recursively collects every "*.c" file under `dir` into `files`, skipping
 * "main.c" (the stub generated below provides its own main()). */
static void collect_c_files(const char *dir, char files[][MAX_PATH_LEN], int *count) {
#ifdef _WIN32
    char pattern[MAX_PATH_LEN];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }

        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            collect_c_files(path, files, count);
        } else if (*count < MAX_SRC_FILES) {
            size_t len = strlen(fd.cFileName);
            if (len > 2 && strcmp(fd.cFileName + len - 2, ".c") == 0 &&
                strcmp(fd.cFileName, "main.c") != 0) {
                strncpy(files[*count], path, MAX_PATH_LEN - 1);
                files[*count][MAX_PATH_LEN - 1] = '\0';
                (*count)++;
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            collect_c_files(path, files, count);
        } else if (*count < MAX_SRC_FILES) {
            size_t len = strlen(entry->d_name);
            if (len > 2 && strcmp(entry->d_name + len - 2, ".c") == 0 &&
                strcmp(entry->d_name, "main.c") != 0) {
                strncpy(files[*count], path, MAX_PATH_LEN - 1);
                files[*count][MAX_PATH_LEN - 1] = '\0';
                (*count)++;
            }
        }
    }

    closedir(d);
#endif
}

/* Escapes `src` for embedding inside a C string literal. Caller frees the
 * result. */
static char *escape_c_string(const char *src) {
    size_t src_len = strlen(src);
    char *out = (char *)malloc(src_len * 8 + 16);
    char *p = out;

    for (const unsigned char *s = (const unsigned char *)src; *s; s++) {
        if (*s == '\\' || *s == '"') {
            *p++ = '\\';
            *p++ = (char)*s;
        } else if (*s == '\n') {
            *p++ = '\\'; *p++ = 'n';
        } else if (*s == '\r') {
            *p++ = '\\'; *p++ = 'r';
        } else if (*s == '\t') {
            *p++ = '\\'; *p++ = 't';
        } else if (*s < 0x20) {
            p += sprintf(p, "\\x%02x\" \"", *s); /* close/reopen the literal so a
                                                      following hex digit can't be
                                                      absorbed into \x by mistake */
        } else {
            *p++ = (char)*s;
        }
    }

    *p = '\0';
    return out;
}

static void derive_output_name(const char *script_path, char *out, size_t out_size) {
    const char *base = strrchr(script_path, '/');
#ifdef _WIN32
    const char *base_bs = strrchr(script_path, '\\');
    if (!base || (base_bs && base_bs > base)) {
        base = base_bs;
    }
#endif
    base = base ? base + 1 : script_path;

    strncpy(out, base, out_size - 1);
    out[out_size - 1] = '\0';

    char *dot = strrchr(out, '.');
    if (dot) {
        *dot = '\0';
    }

#ifdef _WIN32
    strncat(out, ".exe", out_size - strlen(out) - 1);
#endif
}

int cmd_build(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s build <script.nac> [-o <output>]\n", argv[0]);
        return 1;
    }

    const char *script_path = argv[2];
    const char *output_name = NULL;
    char default_output[MAX_PATH_LEN];

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_name = argv[i + 1];
            i++;
        }
    }

    if (!output_name) {
        derive_output_name(script_path, default_output, sizeof(default_output));
        output_name = default_output;
    }

    char exe_dir[MAX_PATH_LEN];
    if (!get_exe_dir(exe_dir, sizeof(exe_dir)) || exe_dir[0] == '\0') {
        fprintf(stderr, "[ERROR] Could not determine nac's own location.\n");
        return 1;
    }

    char src_dir[MAX_PATH_LEN + 16];
    snprintf(src_dir, sizeof(src_dir), "%s%csrc", exe_dir,
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
    );

    char main_check[MAX_PATH_LEN + 16];
    snprintf(main_check, sizeof(main_check), "%s%cmain.c", src_dir,
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
    );
    FILE *probe = fopen(main_check, "rb");
    if (!probe) {
        fprintf(stderr,
                "[ERROR] 'nac build' needs the NaC project's src/ directory next to\n"
                "        the nac executable (the same layout build.sh/build.bat leave\n"
                "        behind). Looked for: %s\n"
                "        Run 'nac build' from the project you built nac in.\n",
                main_check);
        return 1;
    }
    fclose(probe);

    char *source = read_file(script_path); /* exits(1) itself if the file can't be opened */

    static char files[MAX_SRC_FILES][MAX_PATH_LEN];
    int file_count = 0;
    collect_c_files(src_dir, files, &file_count);

    if (file_count == 0) {
        fprintf(stderr, "[ERROR] No source files found under %s\n", src_dir);
        free(source);
        return 1;
    }

    char *escaped = escape_c_string(source);
    free(source);

    const char *stub_path = ".nac_build_stub.c";
    FILE *f = fopen(stub_path, "w");
    if (!f) {
        fprintf(stderr, "[ERROR] Could not create temporary build file (%s).\n", stub_path);
        free(escaped);
        return 1;
    }

    fprintf(f,
            "/* Auto-generated by `nac build` -- safe to delete. */\n"
            "#include <string.h>\n"
            "#include \"core/interpreter.h\"\n"
            "#include \"io/io.h\"\n"
            "static const char *NAC_EMBEDDED_SOURCE = \"%s\";\n"
            "int main(int argc, char *argv[]) {\n"
            "    init_interpreter();\n"
            "    set_cli_args(argc - 1 > 0 ? argc - 1 : 0, argc > 1 ? &argv[1] : NULL);\n"
            "    set_source_code(strdup(NAC_EMBEDDED_SOURCE));\n"
            "    int exit_code = run_interpreter();\n"
            "    shutdown_interpreter();\n"
            "    return exit_code;\n"
            "}\n",
            escaped);
    fclose(f);
    free(escaped);

    /* Same link flags as build.sh/build.bat. */
    char cmd[65536];
    int n = snprintf(cmd, sizeof(cmd), "gcc \"%s\" -I\"%s\"", stub_path, src_dir);
    for (int i = 0; i < file_count && n < (int)sizeof(cmd); i++) {
        n += snprintf(cmd + n, sizeof(cmd) - n, " \"%s\"", files[i]);
    }
#ifdef _WIN32
    n += snprintf(cmd + n, sizeof(cmd) - n,
                  " -o \"%s\" -lwinhttp -lws2_32 -lm -Wl,--stack,134217728", output_name);
#else
    n += snprintf(cmd + n, sizeof(cmd) - n, " -o \"%s\" -lcurl -lm -lpthread", output_name);
#endif

    printf("[INFO] Building standalone executable from %s ...\n", script_path);
    int ret = system(cmd);
    remove(stub_path);

    if (ret != 0) {
        fprintf(stderr, "[ERROR] Compilation failed.\n");
        return 1;
    }

    printf("[SUCCESS] Wrote %s -- run it directly, no 'nac' or .nac file needed:\n", output_name);
    int already_pathed = (output_name[0] == '/' || output_name[0] == '\\' ||
                           strchr(output_name, '/') != NULL || strchr(output_name, '\\') != NULL ||
                           (strlen(output_name) > 1 && output_name[1] == ':'));
#ifdef _WIN32
    printf(already_pathed ? "    %s\n" : "    .\\%s\n", output_name);
#else
    printf(already_pathed ? "    %s\n" : "    ./%s\n", output_name);
#endif
    return 0;
}
