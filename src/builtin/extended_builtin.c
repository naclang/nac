#include "extended_builtin.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "../builtin/builtin.h"
#include "../core/interpreter.h"
#include "../net/http.h"
#include "../net/server.h"
#include "../net/router.h"
#include "../net/server_common.h"
#include "../runtime/eval.h"
#include "../runtime/json.h"
#include "../util/error.h"
#include "../util/strutil.h"

#define HTTP_DEFAULT_TIMEOUT_SEC 30

static int parse_port(const char *text, int *port) {
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || end == text || *end != '\0' || value < 1 || value > 65535) {
        return 0;
    }
    *port = (int)value;
    return 1;
}

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

static void decode_url_component(const char *in, char *out, size_t out_cap) {
    size_t oi = 0;
    for (size_t i = 0; in && in[i] && oi + 1 < out_cap; i++) {
        if (in[i] == '+') {
            out[oi++] = ' ';
        } else if (in[i] == '%' && isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2])) {
            char hex[3] = { in[i + 1], in[i + 2], '\0' };
            out[oi++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            out[oi++] = in[i];
        }
    }
    out[oi] = '\0';
}

/* A value rendered the way print() shows it, as an owned string Value of any
 * length. Strings are copied as they are; other values go through a 4 KiB
 * scratch buffer and, if that may have truncated, an 8 MiB heap buffer. */
static Value display_value_alloc(Value v) {
    if (v.type == TYPE_STRING) return make_string(v.str_val);
    char small[4096];
    value_to_display_string(v, small, sizeof(small));
    if (strlen(small) < sizeof(small) - 1) return make_string(small);
    size_t cap = 8 * 1024 * 1024;
    char *big = (char*)malloc(cap);
    if (!big) return make_string(small);
    value_to_display_string(v, big, cap);
    return make_string_take(big);
}

static Value parse_form_encoded(const char *raw) {
    Value form = make_map();
    if (!raw || raw[0] == '\0') {
        return form;
    }

    char *copy = nac_strdup(raw);
    if (!copy) {
        free_value(&form);
        report_error("form(): out of memory");
        return make_null();
    }

    for (char *pair = strtok(copy, "&"); pair; pair = strtok(NULL, "&")) {
        char *eq = strchr(pair, '=');
        if (!eq) {
            char *key = (char*)malloc(strlen(pair) + 1);
            if (!key) continue;
            decode_url_component(pair, key, strlen(pair) + 1);
            map_set(&form, key, make_string(""));
            free(key);
            continue;
        }
        *eq = '\0';
        char *key = (char*)malloc(strlen(pair) + 1);
        char *value = (char*)malloc(strlen(eq + 1) + 1);
        if (key && value) {
            decode_url_component(pair, key, strlen(pair) + 1);
            decode_url_component(eq + 1, value, strlen(eq + 1) + 1);
            map_set(&form, key, make_string(value));
        }
        free(key);
        free(value);
    }

    free(copy);
    return form;
}

typedef struct {
    char *data;
    size_t used;
    size_t cap;
} TplOut;

static int append_template_text(TplOut *o, const char *text, size_t len) {
    if (o->used + len + 1 > o->cap) {
        size_t nc = o->cap ? o->cap : 256;
        while (nc < o->used + len + 1) nc *= 2;
        char *grown = (char*)realloc(o->data, nc);
        if (!grown) return 0;
        o->data = grown;
        o->cap = nc;
    }
    if (len) memcpy(o->data + o->used, text, len);
    o->used += len;
    o->data[o->used] = '\0';
    return 1;
}

static int append_html_escaped(TplOut *o, const char *text) {
    for (const char *p = text; *p; p++) {
        const char *escaped = NULL;
        switch (*p) {
            case '&': escaped = "&amp;"; break;
            case '<': escaped = "&lt;"; break;
            case '>': escaped = "&gt;"; break;
            case '"': escaped = "&quot;"; break;
            case '\'': escaped = "&#39;"; break;
            default:
                if (!append_template_text(o, p, 1)) return 0;
                continue;
        }
        if (!append_template_text(o, escaped, strlen(escaped))) return 0;
    }
    return 1;
}

static Value render_template(const char *path, Value data) {
    if (data.type != TYPE_MAP) {
        report_error("render() requires a template path and a data map");
        return make_null();
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        char msg[512];
        snprintf(msg, sizeof(msg), "render(): cannot open template '%s'", path);
        report_error(msg);
        return make_null();
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        report_error("render(): could not read template");
        return make_null();
    }
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        report_error("render(): could not read template");
        return make_null();
    }
    rewind(file);

    char *source = (char*)malloc((size_t)file_size + 1);
    if (!source) {
        fclose(file);
        report_error("render(): out of memory");
        return make_null();
    }
    size_t read_size = fread(source, 1, (size_t)file_size, file);
    int read_failed = ferror(file);
    fclose(file);
    if (read_failed || read_size != (size_t)file_size) {
        free(source);
        report_error("render(): failed to read template");
        return make_null();
    }
    source[read_size] = '\0';

    TplOut output = {0};
    const char *cursor = source;
    while (*cursor) {
        const char *open = strstr(cursor, "{{");
        if (!open) {
            if (!append_template_text(&output, cursor, strlen(cursor))) goto oom;
            break;
        }
        if (!append_template_text(&output, cursor, (size_t)(open - cursor))) goto oom;
        const char *close = strstr(open + 2, "}}");
        if (!close) {
            free(source);
            free(output.data);
            report_error("render(): unclosed {{ placeholder");
            return make_null();
        }
        const char *key_start = open + 2;
        const char *key_end = close;
        while (key_start < key_end && isspace((unsigned char)*key_start)) key_start++;
        while (key_end > key_start && isspace((unsigned char)key_end[-1])) key_end--;
        size_t key_len = (size_t)(key_end - key_start);
        if (key_len == 0 || key_len >= MAX_TOKEN_LEN) {
            free(source);
            free(output.data);
            report_error("render(): invalid template placeholder");
            return make_null();
        }
        char key[MAX_TOKEN_LEN];
        memcpy(key, key_start, key_len);
        key[key_len] = '\0';
        Value *value = map_get(&data, key);
        if (!value) {
            char msg[256];
            snprintf(msg, sizeof(msg), "render(): missing template value '%s'", key);
            free(source);
            free(output.data);
            report_error(msg);
            return make_null();
        }
        Value text = display_value_alloc(*value);
        int ok = append_html_escaped(&output, text.str_val);
        free_value(&text);
        if (!ok) goto oom;
        cursor = close + 2;
    }
    free(source);

    Value response = make_map();
    map_set(&response, "status", make_int(200));
    map_set(&response, "body", output.data ? make_string_take(output.data) : make_string(""));
    Value headers = make_map();
    map_set(&headers, "content-type", make_string("text/html; charset=utf-8"));
    map_set(&response, "headers", headers);
    free_value(&headers);
    return response;

oom:
    free(source);
    free(output.data);
    report_error("render(): out of memory");
    return make_null();
}

static Value normalize_test_response(Value result) {
    if (result.type == TYPE_MAP) {
        Value status = map_get(&result, "status") ? copy_value(*map_get(&result, "status")) : make_int(200);
        Value body = map_get(&result, "body") ? copy_value(*map_get(&result, "body")) : make_string("");
        Value headers = map_get(&result, "headers") ? copy_value(*map_get(&result, "headers")) : make_map();
        Value response = make_map();
        map_set(&response, "status", status);
        map_set(&response, "body", body);
        map_set(&response, "headers", headers);
        free_value(&status);
        free_value(&body);
        free_value(&headers);
        return response;
    }
    Value response = make_map();
    map_set(&response, "status", make_int(200));
    map_set(&response, "body", display_value_alloc(result));
    map_set(&response, "headers", make_map());
    return response;
}

static Value test_request(Value *args, int arg_count) {
    if (arg_count < 2 || arg_count > 4 ||
        args[0].type != TYPE_STRING || args[1].type != TYPE_STRING ||
        (arg_count >= 3 && args[2].type != TYPE_STRING) ||
        (arg_count >= 4 && args[3].type != TYPE_MAP)) {
        report_error("request() requires (method, path, [body], [headers])");
        return make_null();
    }

    NacHttpRequest req;
    memset(&req, 0, sizeof(req));
    for (int i = 0; args[0].str_val[i] && i < (int)sizeof(req.method) - 1; i++) {
        req.method[i] = (char)toupper((unsigned char)args[0].str_val[i]);
    }
    req.query = make_map();
    req.cookie = make_map();
    req.headers = make_map();
    if (arg_count == 4) {
        for (int i = 0; i < args[3].map_val.size; i++) {
            char lower_key[MAX_STRING_LEN];
            size_t key_len = strlen(args[3].map_val.keys[i]);
            if (key_len >= sizeof(lower_key)) key_len = sizeof(lower_key) - 1;
            for (size_t j = 0; j < key_len; j++) {
                lower_key[j] = (char)tolower((unsigned char)args[3].map_val.keys[i][j]);
            }
            lower_key[key_len] = '\0';
            map_set(&req.headers, lower_key, args[3].map_val.values[i]);
        }
    }
    req.body = (char*)calloc(1, 1);
    if (arg_count >= 3) {
        nac_http_set_body(&req, args[2].str_val, (int)strlen(args[2].str_val));
    }
    strcpy(req.client_ip, "127.0.0.1");

    char path[MAX_STRING_LEN];
    strncpy(path, args[1].str_val, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    char *query = strchr(path, '?');
    if (query) {
        *query++ = '\0';
        Value parsed_query = parse_form_encoded(query);
        free_value(&req.query);
        req.query = parsed_query;
    }
    strncpy(req.path, path, sizeof(req.path) - 1);

    nac_header_reset();
    char fn[MAX_TOKEN_LEN];
    int path_matched = 0;
    int route_found = 0;
    Value params = make_map();
    Value result = make_null();
    {
        Value matched_params;
        if (router_match(req.method, req.path, fn, sizeof(fn), &matched_params, &path_matched)) {
            free_value(&params);
            params = matched_params;
            route_found = 1;
        }
    }

    if (nac_run_before(&req, params, &result)) {
        /* a before block answered */
    } else if (route_found) {
        Value route_args[7] = {
            make_string(req.method), make_string(req.path), req.query, req.headers,
            make_string(req.body), params, req.cookie
        };
        if (!call_nac_function(fn, route_args, 7, &result)) {
            result = make_map();
            map_set(&result, "status", make_int(500));
            map_set(&result, "body", make_string("route handler not found"));
        }
    } else {
        result = make_map();
        map_set(&result, "status", make_int(path_matched ? 405 : 404));
        map_set(&result, "body", make_string(path_matched ? "Method Not Allowed" : "Not Found"));
    }
    nac_finish_response(&req, params, &result, NULL, NULL, NULL);
    free_value(&params);

    Value response = normalize_test_response(result);
    free_value(&result);
    free_value(&req.query);
    free_value(&req.headers);
    free_value(&req.cookie);
    free(req.body);
    return response;
}

bool is_extended_builtin(const char *name) {
    const char *builtins[] = {
        "jsonParse", "jsonStringify",
        "args", "env", "exit", "serve", "static", "html", "json", "redirect",
        "form", "header", "setHeader", "cors", "render", "request"
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
        if ((arg_count != 1 && arg_count != 2) || args[0].type != TYPE_STRING) {
            report_error("env() requires (name, [default]) with a string name");
            return make_string("");
        }
        const char *value = getenv(args[0].str_val);
        if (value) return make_string(value);
        return arg_count == 2 ? copy_value(args[1]) : make_string("");
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
        /* serve([port [, host]]). Port: --port flag, else the argument, else
         * PORT from the environment, else 8080. Host: --host flag, else the
         * argument, else NAC_HOST, else every interface. */
        if (arg_count > 2) {
            report_error("serve() requires 0 to 2 arguments (port, host)");
            return make_int(1);
        }
        if (arg_count == 2 && args[1].type != TYPE_STRING) {
            report_error("serve(): host must be a string");
            return make_int(1);
        }

        char config_error[512];
        if (nac_config_apply_env(config_error, sizeof(config_error)) != 0) {
            report_error(config_error);
            return make_int(1);
        }

        int port = 8080;
        if (arg_count >= 1) {
            port = to_int(args[0]);
        } else {
            const char *configured_port = getenv("PORT");
            if (configured_port && configured_port[0] && !parse_port(configured_port, &port)) {
                report_error("serve(): PORT must be an integer from 1 to 65535");
                return make_int(1);
            }
        }
        if (nac_cfg.flags_set & NAC_OPT_PORT) port = nac_cfg.port;
        if (port < 1 || port > 65535) {
            report_error("serve(): port must be from 1 to 65535");
            return make_int(1);
        }

        const char *host = nac_cfg.host;
        if (arg_count == 2 && !(nac_cfg.flags_set & NAC_OPT_HOST)) host = args[1].str_val;
        return make_int(server_start(port, host, ""));
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
            map_set(&resp, "body", display_value_alloc(args[0]));
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

    if (strcmp(name, "form") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("form() requires 1 string argument (urlencoded body)");
            return make_map();
        }
        return parse_form_encoded(args[0].str_val);
    }

    if (strcmp(name, "header") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("header() requires (name, value) with string arguments");
            return make_map();
        }
        Value hdr = make_map();
        map_set(&hdr, args[0].str_val, make_string(args[1].str_val));
        return hdr;
    }

    if (strcmp(name, "setHeader") == 0) {
        /* setHeader(name, value) or setHeader(map): adds response headers for
         * the current request (usable in before, route and after). */
        if (arg_count == 2 && args[0].type == TYPE_STRING && args[1].type == TYPE_STRING) {
            if (nac_header_set(args[0].str_val, args[1].str_val) != 0) {
                report_error("setHeader(): invalid header name or value, or too many headers");
                return make_int(1);
            }
            return make_int(0);
        }
        if (arg_count == 1 && args[0].type == TYPE_MAP) {
            for (int i = 0; i < args[0].map_val.size; i++) {
                char value[MAX_STRING_LEN];
                value_to_display_string(args[0].map_val.values[i], value, sizeof(value));
                if (nac_header_set(args[0].map_val.keys[i], value) != 0) {
                    report_error("setHeader(): invalid header name or value, or too many headers");
                    return make_int(1);
                }
            }
            return make_int(0);
        }
        report_error("setHeader() requires (name, value) strings or one map of headers");
        return make_int(1);
    }

    if (strcmp(name, "cors") == 0) {
        if (arg_count < 1 || arg_count > 3 || args[0].type != TYPE_STRING) {
            report_error("cors() requires (origin, [methods], [headers]) with string arguments");
            return make_map();
        }
        const char *origin = args[0].str_val;
        const char *methods = (arg_count >= 2 && args[1].type == TYPE_STRING) ? args[1].str_val : "GET, POST, PUT, PATCH, DELETE, OPTIONS";
        const char *headers = (arg_count >= 3 && args[2].type == TYPE_STRING) ? args[2].str_val : "Content-Type, Authorization";

        Value hdr = make_map();
        map_set(&hdr, "Access-Control-Allow-Origin", make_string(origin));
        map_set(&hdr, "Access-Control-Allow-Methods", make_string(methods));
        map_set(&hdr, "Access-Control-Allow-Headers", make_string(headers));
        map_set(&hdr, "Access-Control-Allow-Credentials", make_string("true"));
        return hdr;
    }

    if (strcmp(name, "render") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_STRING || args[1].type != TYPE_MAP) {
            report_error("render() requires (templatePath, dataMap)");
            return make_null();
        }
        return render_template(args[0].str_val, args[1]);
    }

    if (strcmp(name, "request") == 0) {
        return test_request(args, arg_count);
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
