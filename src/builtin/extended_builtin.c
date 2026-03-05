#include "extended_builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../module/module.h"
#include "../net/http.h"
#include "../runtime/json.h"
#include "../util/error.h"

static int body_to_json(Value arg, char **out_json) {
    if (arg.type == TYPE_STRING) {
        *out_json = NULL;
        return 1;
    }

    *out_json = json_stringify_value(arg);
    return (*out_json != NULL);
}

bool is_extended_builtin(const char *name) {
    const char *builtins[] = {
        "jsonParse", "jsonStringify",
        "httpRequest", "httpJson",
        "moduleLoad", "moduleRegister", "moduleGet", "moduleRequire", "moduleNames"
    };

    int count = sizeof(builtins) / sizeof(builtins[0]);
    for (int i = 0; i < count; i++) {
        if (strcmp(name, builtins[i]) == 0) {
            return true;
        }
    }

    return false;
}

Value call_extended_builtin(const char *name, Value *args, int arg_count) {
    if (strcmp(name, "jsonParse") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("jsonParse() requires 1 string argument");
            return make_int(0);
        }

        Value parsed;
        if (!json_parse_value(args[0].str_val, &parsed)) {
            return make_int(0);
        }
        return parsed;
    }

    if (strcmp(name, "jsonStringify") == 0) {
        if (arg_count != 1) {
            report_error("jsonStringify() requires 1 argument");
            return make_string("");
        }

        char *json = json_stringify_value(args[0]);
        if (!json) {
            return make_string("");
        }

        Value out = make_string(json);
        free(json);
        return out;
    }

    if (strcmp(name, "httpRequest") == 0 || strcmp(name, "httpJson") == 0) {
        if (arg_count < 2 || arg_count > 3) {
            report_error("httpRequest/httpJson require 2 or 3 arguments");
            return make_int(0);
        }

        if (args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("httpRequest/httpJson require method and url as strings");
            return make_int(0);
        }

        const char *body = NULL;
        char *json_body = NULL;

        if (arg_count == 3) {
            if (!body_to_json(args[2], &json_body)) {
                report_error("Could not serialize HTTP body");
                return make_int(0);
            }
            body = (args[2].type == TYPE_STRING) ? args[2].str_val : json_body;
        }

        char *response = NULL;
#ifdef _WIN32
        response = http_request_win_response(args[0].str_val, args[1].str_val, body);
#else
        response = http_request_unix_response(args[0].str_val, args[1].str_val, body);
#endif

        if (json_body) {
            free(json_body);
        }

        if (!response) {
            return make_string("");
        }

        if (strcmp(name, "httpRequest") == 0) {
            Value out = make_string(response);
            free(response);
            return out;
        }

        Value parsed;
        int ok = json_parse_value(response, &parsed);
        free(response);
        if (!ok) {
            report_error("httpJson() response is not valid JSON");
            return make_int(0);
        }

        return parsed;
    }

    if (strcmp(name, "moduleLoad") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("moduleLoad() requires 1 string path argument");
            return make_int(0);
        }

        int ok = 0;
        return module_load_json_file(args[0].str_val, &ok);
    }

    if (strcmp(name, "moduleRegister") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_STRING) {
            report_error("moduleRegister() requires (name, module)");
            return make_int(0);
        }

        return make_int(module_register(args[0].str_val, args[1]));
    }

    if (strcmp(name, "moduleGet") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("moduleGet() requires 1 string name argument");
            return make_int(0);
        }

        int found = 0;
        Value module = module_get_copy(args[0].str_val, &found);
        if (!found) {
            report_error("moduleGet() module not found");
            return make_int(0);
        }

        return module;
    }

    if (strcmp(name, "moduleRequire") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("moduleRequire() requires 1 string name argument");
            return make_int(0);
        }

        int ok = 0;
        return module_require_local(args[0].str_val, &ok);
    }

    if (strcmp(name, "moduleNames") == 0) {
        if (arg_count != 0) {
            report_error("moduleNames() requires 0 arguments");
            return make_array(0);
        }

        return module_list_names();
    }

    report_error("Unknown extended built-in function");
    return make_int(0);
}
