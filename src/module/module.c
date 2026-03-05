#include "module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/json.h"
#include "../util/error.h"

#define MAX_MODULES 128

typedef struct {
    int used;
    char name[MAX_STRING_LEN];
    Value value;
} ModuleEntry;

static ModuleEntry registry[MAX_MODULES];

static int find_module(const char *name) {
    for (int i = 0; i < MAX_MODULES; i++) {
        if (registry[i].used && strcmp(registry[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int first_free_slot(void) {
    for (int i = 0; i < MAX_MODULES; i++) {
        if (!registry[i].used) {
            return i;
        }
    }
    return -1;
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char*)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

int module_register(const char *name, Value module_value) {
    if (!name || !name[0]) {
        report_error("module name cannot be empty");
        return 0;
    }

    if (module_value.type != TYPE_MAP && module_value.type != TYPE_ARRAY) {
        report_error("module must be a map or array");
        return 0;
    }

    int idx = find_module(name);
    if (idx < 0) {
        idx = first_free_slot();
    }

    if (idx < 0) {
        report_error("module registry is full");
        return 0;
    }

    if (registry[idx].used) {
        free_value(&registry[idx].value);
    }

    registry[idx].used = 1;
    strncpy(registry[idx].name, name, MAX_STRING_LEN - 1);
    registry[idx].name[MAX_STRING_LEN - 1] = '\0';
    registry[idx].value = copy_value(module_value);
    return 1;
}

Value module_get_copy(const char *name, int *found) {
    int idx = find_module(name);
    if (idx < 0) {
        if (found) {
            *found = 0;
        }
        return make_int(0);
    }

    if (found) {
        *found = 1;
    }
    return copy_value(registry[idx].value);
}

Value module_load_json_file(const char *path, int *ok) {
    if (ok) {
        *ok = 0;
    }

    if (!path || !path[0]) {
        report_error("module path cannot be empty");
        return make_int(0);
    }

    char *text = read_text_file(path);
    if (!text) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot read module file: %s", path);
        report_error(msg);
        return make_int(0);
    }

    Value parsed;
    int parsed_ok = json_parse_value(text, &parsed);
    free(text);

    if (!parsed_ok) {
        return make_int(0);
    }

    if (parsed.type != TYPE_MAP && parsed.type != TYPE_ARRAY) {
        report_error("module file must contain JSON object or array");
        free_value(&parsed);
        return make_int(0);
    }

    if (ok) {
        *ok = 1;
    }
    return parsed;
}

Value module_require_local(const char *name, int *ok) {
    if (ok) {
        *ok = 0;
    }

    int found = 0;
    Value cached = module_get_copy(name, &found);
    if (found) {
        if (ok) {
            *ok = 1;
        }
        return cached;
    }

    char path[MAX_STRING_LEN];
    snprintf(path, sizeof(path), "modules/%s.json", name);

    int load_ok = 0;
    Value loaded = module_load_json_file(path, &load_ok);
    if (!load_ok) {
        return make_int(0);
    }

    module_register(name, loaded);

    if (ok) {
        *ok = 1;
    }
    return loaded;
}

Value module_list_names(void) {
    int count = 0;
    for (int i = 0; i < MAX_MODULES; i++) {
        if (registry[i].used) {
            count++;
        }
    }

    Value arr = make_array(count);
    int out_idx = 0;
    for (int i = 0; i < MAX_MODULES; i++) {
        if (registry[i].used) {
            free_value(&arr.array_val.elements[out_idx]);
            arr.array_val.elements[out_idx] = make_string(registry[i].name);
            out_idx++;
        }
    }

    return arr;
}
