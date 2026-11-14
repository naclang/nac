#include "extended_builtin.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../builtin/builtin.h"
#include "../core/interpreter.h"
#include "../net/http.h"
#include "../net/server.h"
#include "../net/router.h"
#include "../runtime/eval.h"
#include "../runtime/json.h"
#include "../util/error.h"

#define HTTP_DEFAULT_TIMEOUT_SEC 30

/* Builds the response map handed back to the script (and to the callback). */
static Value make_http_response(const HttpResponse *r, int transport_ok) {
    Value res = make_map();
    int status = r->status;

    map_set(&res, "status", make_int(status));
    map_set(&res, "ok", make_bool(transport_ok && status >= 200 && status < 300));
    map_set(&res, "body", make_string(r->body ? r->body : ""));

    /* `data`: the body parsed as JSON when it is JSON, else null. Parsed from
     * the full buffer (before make_string's length cap) and silently. */
    Value data = make_null();
    if (r->body) {
        const char *p = r->body;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '{' || *p == '[') {
            Value parsed;
            if (json_try_parse_value(p, &parsed)) {
                map_set(&res, "data", parsed);
                free_value(&parsed);
                data.type = TYPE_BOOL;   /* marker: data already stored */
            }
        }
    }
    if (data.type == TYPE_NULL) map_set(&res, "data", make_null());

    if (r->error[0]) {
        map_set(&res, "error", make_string(r->error));
    } else {
        map_set(&res, "error", make_null());
    }
    return res;
}

Value http_call_with_options(const char *method, Value opts) {
    char fn_name[24];
    snprintf(fn_name, sizeof(fn_name), "http.%s", method);
    for (char *c = fn_name + 5; *c; c++) {
        if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    }

    if (opts.type != TYPE_MAP) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s expects options in braces: %s { url: \"...\" }", fn_name, fn_name);
        report_error(msg);
        return make_null();
    }

    Value *url = map_get(&opts, "url");
    if (!url || url->type != TYPE_STRING || url->str_val[0] == '\0') {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s requires a \"url\" string", fn_name);
        report_error(msg);
        return make_null();
    }

    Value *body_opt = map_get(&opts, "body");
    if (body_opt && body_opt->type == TYPE_NULL) body_opt = NULL;
    if (body_opt && strcmp(method, "GET") == 0) {
        report_error("http.get does not send a body (put parameters in the url)");
        return make_null();
    }

    Value *callback = map_get(&opts, "callback");
    if (callback && callback->type != TYPE_FUNC) {
        if (callback->type != TYPE_NULL) {
            report_error("http callback must be a function: callback: fn(res) { ... }");
        }
        callback = NULL;
    }

    int timeout = HTTP_DEFAULT_TIMEOUT_SEC;
    Value *timeout_opt = map_get(&opts, "timeout");
    if (timeout_opt && (timeout_opt->type == TYPE_INT || timeout_opt->type == TYPE_FLOAT)) {
        timeout = to_int(*timeout_opt);
    }

    /* headers: { "X-Key": "abc" } -> ["X-Key: abc"] */
    const char **header_lines = NULL;
    char **header_storage = NULL;
    int header_count = 0;
    Value *headers_opt = map_get(&opts, "headers");
    if (headers_opt && headers_opt->type == TYPE_MAP && headers_opt->map_val.size > 0) {
        int n = headers_opt->map_val.size;
        header_lines = (const char**)malloc(sizeof(char*) * n);
        header_storage = (char**)malloc(sizeof(char*) * n);
        for (int i = 0; i < n; i++) {
            char val[2048];
            value_to_display_string(headers_opt->map_val.values[i], val, sizeof(val));
            size_t need = strlen(headers_opt->map_val.keys[i]) + strlen(val) + 3;
            header_storage[header_count] = (char*)malloc(need);
            snprintf(header_storage[header_count], need, "%s: %s", headers_opt->map_val.keys[i], val);
            header_lines[header_count] = header_storage[header_count];
            header_count++;
        }
    } else if (headers_opt && headers_opt->type != TYPE_NULL && headers_opt->type != TYPE_MAP) {
        report_error("http headers must be a map: headers: { \"X-Key\": \"abc\" }");
    }

    /* body: strings go as-is, maps/arrays are sent as JSON */
    char *json_body = NULL;
    const char *body = NULL;
    if (body_opt) {
        if (body_opt->type == TYPE_STRING) {
            body = body_opt->str_val;
        } else {
            json_body = json_stringify_value(*body_opt);
            body = json_body;
        }
    }

    HttpResponse r;
    memset(&r, 0, sizeof(r));
    int ok = 0;
    if (strncmp(url->str_val, "http://", 7) != 0 && strncmp(url->str_val, "https://", 8) != 0) {
        snprintf(r.error, sizeof(r.error), "URL must start with http:// or https://");
        r.body = (char*)calloc(1, 1);
    } else {
#ifdef _WIN32
        ok = http_perform_win(method, url->str_val, body, header_lines, header_count, timeout, &r);
#else
        ok = http_perform_unix(method, url->str_val, body, header_lines, header_count, timeout, &r);
#endif
    }

    Value res = make_http_response(&r, ok);

    http_response_free(&r);
    free(json_body);
    for (int i = 0; i < header_count; i++) free(header_storage[i]);
    free(header_storage);
    free(header_lines);

    /* Callback runs once the response is in (synchronously), then the
     * response is also returned so `r = http.get { ... }` works too. */
    if (callback) {
        Value args[1] = { res };
        Value ignored;
        call_func_value(*callback, args, 1, &ignored);
    }

    return res;
}

bool is_extended_builtin(const char *name) {
    const char *builtins[] = {
        "jsonParse", "jsonStringify",
        "args", "env", "exit", "serve", "static", "html", "json", "redirect"
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
        /* serve(port) -- runs the routes and static() mounts declared above it */
        if (arg_count != 1) {
            report_error("serve() requires 1 argument (port)");
            return make_int(1);
        }
        return make_int(server_start(to_int(args[0]), ""));
    }

    if (strcmp(name, "static") == 0) {
        /* static("public")  or  static("/assets", "public") */
        if ((arg_count != 1 && arg_count != 2) || args[0].type != TYPE_STRING ||
            (arg_count == 2 && args[1].type != TYPE_STRING)) {
            report_error("static() requires (dir) or (urlPrefix, dir) string arguments");
            return make_int(1);
        }
        int rc = (arg_count == 1) ? static_mount_add("/", args[0].str_val)
                                  : static_mount_add(args[0].str_val, args[1].str_val);
        if (rc != 0) {
            report_error("static(): too many mounted directories");
            return make_int(1);
        }
        return make_int(0);
    }

    if (strcmp(name, "html") == 0 || strcmp(name, "json") == 0) {
        /* html(body [, status])  /  json(value [, status]) -> response map */
        int is_json = strcmp(name, "json") == 0;
        if (arg_count < 1 || arg_count > 2) {
            report_error(is_json ? "json() requires 1 or 2 arguments (value, [status])"
                                 : "html() requires 1 or 2 arguments (body, [status])");
            return make_int(0);
        }

        Value resp = make_map();
        map_set(&resp, "status", make_int(arg_count == 2 ? to_int(args[1]) : 200));

        Value hdrs = make_map();
        if (is_json) {
            char *js = json_stringify_value(args[0]);
            map_set(&resp, "body", make_string(js ? js : "null"));
            if (js) free(js);
            map_set(&hdrs, "content-type", make_string("application/json"));
        } else {
            char text[MAX_STRING_LEN];
            value_to_display_string(args[0], text, sizeof(text));
            map_set(&resp, "body", make_string(text));
            map_set(&hdrs, "content-type", make_string("text/html; charset=utf-8"));
        }
        map_set(&resp, "headers", hdrs);
        free_value(&hdrs);
        return resp;
    }

    if (strcmp(name, "redirect") == 0) {
        if (arg_count < 1 || arg_count > 2 || args[0].type != TYPE_STRING) {
            report_error("redirect() requires (url, [status]) with a string url");
            return make_int(0);
        }
        Value resp = make_map();
        map_set(&resp, "status", make_int(arg_count == 2 ? to_int(args[1]) : 302));
        map_set(&resp, "body", make_string(""));
        Value hdrs = make_map();
        map_set(&hdrs, "location", make_string(args[0].str_val));
        map_set(&resp, "headers", hdrs);
        free_value(&hdrs);
        return resp;
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

    report_error("Unknown extended built-in function");
    return make_int(0);
}
