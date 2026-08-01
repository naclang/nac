#include "extended_builtin.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../builtin/builtin.h"
#include "../core/interpreter.h"
#include "../net/http.h"
#include "../net/server.h"
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

/* Shared by httpRequest/httpJson and the httpGet/httpPost/httpPut/httpPatch/
 * httpDelete convenience wrappers below. `body_arg` may be NULL (no body,
 * e.g. for GET/DELETE); when non-NULL it may be a string (sent as-is) or
 * any other value (e.g. a map()), which is JSON-serialized automatically —
 * so `httpPost(url, myMap)` works without hand-escaping a JSON string.
 */
static Value do_http_request(const char *method, const char *url, Value *body_arg, bool parse_json) {
    const char *body = NULL;
    char *json_body = NULL;

    if (body_arg) {
        if (!body_to_json(*body_arg, &json_body)) {
            report_error("Could not serialize HTTP body");
            return make_int(0);
        }
        body = (body_arg->type == TYPE_STRING) ? body_arg->str_val : json_body;
    }

    char *response = NULL;
#ifdef _WIN32
    response = http_request_win_response(method, url, body);
#else
    response = http_request_unix_response(method, url, body);
#endif

    if (json_body) {
        free(json_body);
    }

    if (!response) {
        return make_string("");
    }

    if (!parse_json) {
        Value out = make_string(response);
        free(response);
        return out;
    }

    Value parsed;
    int ok = json_parse_value(response, &parsed);
    free(response);
    if (!ok) {
        report_error("HTTP response is not valid JSON");
        return make_int(0);
    }
    return parsed;
}

bool is_extended_builtin(const char *name) {
    const char *builtins[] = {
        "jsonParse", "jsonStringify",
        "httpRequest", "httpJson",
        "httpGet", "httpPost", "httpPut", "httpPatch", "httpDelete",
        "args", "env", "exit", "serve"
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
    if (strcmp(name, "args") == 0) {
        if (arg_count != 0) {
            report_error("args() requires 0 arguments");
            return make_array(0);
        }

        Value result = make_array(0);
        result.array_val.capacity = nac_argc;
        result.array_val.size = nac_argc;
        result.array_val.elements = nac_argc > 0 ? (Value*)malloc(sizeof(Value) * nac_argc) : NULL;
        for (int i = 0; i < nac_argc; i++) {
            result.array_val.elements[i] = make_string(nac_argv[i]);
        }
        return result;
    }

    if (strcmp(name, "env") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("env() requires 1 string argument (variable name)");
            return make_string("");
        }
        const char *value = getenv(args[0].str_val);
        return make_string(value ? value : "");
    }

    if (strcmp(name, "exit") == 0) {
        int code = 0;
        if (arg_count == 1) {
            code = to_int(args[0]);
        } else if (arg_count != 0) {
            report_error("exit() requires 0 or 1 arguments");
            return make_int(0);
        }
        fflush(stdout);
        exit(code);
    }

    if (strcmp(name, "serve") == 0) {
        if (arg_count != 2 || args[1].type != TYPE_STRING) {
            report_error("serve() requires 2 arguments (port, handlerFunctionName)");
            return make_int(1);
        }
        int port = to_int(args[0]);
        int result = server_start(port, args[1].str_val);
        return make_int(result);
    }

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

        Value *body_arg = (arg_count == 3) ? &args[2] : NULL;
        bool as_json = (strcmp(name, "httpJson") == 0);
        return do_http_request(args[0].str_val, args[1].str_val, body_arg, as_json);
    }

    /* httpGet(url) / httpDelete(url): no body.
     * httpPost(url, body?) / httpPut(url, body?) / httpPatch(url, body?):
     * body is optional and, if given, may be a string or any other value
     * (auto JSON-serialized -- e.g. a map()).
     * All five return the raw response body as a string, same as
     * httpRequest() -- pass the result to jsonParse() (or use httpJson()
     * directly) if you want it parsed. This is the recommended, more
     * readable way to make HTTP calls; the method name is the function
     * name instead of a separate string argument:
     *
     *   response = httpGet("https://api.ipify.org/?format=json");
     *
     *   body = map();
     *   body["mesaj"] = "Merhaba";
     *   response = httpPost("https://httpbin.org/post", body);
     */
    static const struct { const char *fn_name; const char *method; bool has_body; } http_verbs[] = {
        {"httpGet",    "GET",    false},
        {"httpDelete", "DELETE", false},
        {"httpPost",   "POST",   true},
        {"httpPut",    "PUT",    true},
        {"httpPatch",  "PATCH",  true},
    };

    for (size_t i = 0; i < sizeof(http_verbs) / sizeof(http_verbs[0]); i++) {
        if (strcmp(name, http_verbs[i].fn_name) != 0) {
            continue;
        }

        int min_args = 1;
        int max_args = http_verbs[i].has_body ? 2 : 1;
        if (arg_count < min_args || arg_count > max_args) {
            char msg[128];
            snprintf(msg, sizeof(msg), "%s() requires %s", http_verbs[i].fn_name,
                     http_verbs[i].has_body ? "1 argument (url) or 2 (url, body)" : "1 argument (url)");
            report_error(msg);
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            char msg[128];
            snprintf(msg, sizeof(msg), "%s() requires url as a string", http_verbs[i].fn_name);
            report_error(msg);
            return make_string("");
        }

        Value *body_arg = (arg_count == 2) ? &args[1] : NULL;
        return do_http_request(http_verbs[i].method, args[0].str_val, body_arg, false);
    }

    report_error("Unknown extended built-in function");
    return make_int(0);
}
