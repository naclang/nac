#include "security_builtin.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../crypto/crypto.h"
#include "../net/server_common.h"
#include "../runtime/json.h"
#include "../util/error.h"
#include "../util/strutil.h"

#define PASSWORD_SALT_LEN 16
#define PASSWORD_HASH_LEN 32
#define PASSWORD_ITERATIONS 210000
#define PASSWORD_MIN_ITERATIONS 1000
#define PASSWORD_MAX_ITERATIONS 1000000
#define MAX_COOKIE_NAME_LEN 128
#define MAX_COOKIE_VALUE_LEN 4096
#define MAX_COOKIE_LINE_LEN 1024

/* Hex is used instead of raw bytes because NaC strings are NUL-terminated
 * text, so binary output would silently truncate. */
static void hex_encode(const unsigned char *data, size_t len, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[(data[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static int has_header_break(const char *s) {
    for (; *s; s++) {
        if (*s == '\r' || *s == '\n') return 1;
    }
    return 0;
}

static int valid_cookie_name(const char *name) {
    size_t len = strlen(name);
    if (len == 0 || len > MAX_COOKIE_NAME_LEN) return 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c <= 0x20 || c >= 0x7f) return 0;
        if (strchr("()<>@,;:\\\"/[]?={}", c)) return 0;
    }
    return 1;
}

static int valid_cookie_value(const char *value) {
    size_t len = strlen(value);
    if (len > MAX_COOKIE_VALUE_LEN) return 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x20 || c == 0x7f || c == ';') return 0;
    }
    return 1;
}

static int valid_cookie_attr(const char *value) {
    if (!value) return 1;
    for (; *value; value++) {
        if (*value == '\r' || *value == '\n' || *value == ';') return 0;
    }
    return 1;
}

static const char *same_site_value(const char *s, int *valid) {
    *valid = 1;
    if (nac_ci_equal(s, "lax")) return "Lax";
    if (nac_ci_equal(s, "strict")) return "Strict";
    if (nac_ci_equal(s, "none")) return "None";
    *valid = 0;
    return NULL;
}

typedef struct {
    const char *path;
    const char *domain;
    const char *expires;
    int max_age;          /* NAC_COOKIE_NO_MAX_AGE means omit */
    int http_only;
    int secure;
    const char *same_site; /* NULL omits SameSite */
} CookieOptions;

#define NAC_COOKIE_NO_MAX_AGE (-1)

static int parse_cookie_options(Value opts, CookieOptions *o, int clearing) {
    o->path = "/";
    o->domain = "";
    o->expires = "";
    o->max_age = clearing ? 0 : NAC_COOKIE_NO_MAX_AGE;
    o->http_only = 1;
    o->secure = 0;
    o->same_site = "Lax";

    if (opts.type == TYPE_NULL) {
        return 1;
    }
    if (opts.type != TYPE_MAP) {
        report_error("setCookie/clearCookie options must be a map: { path: \"/\", ... }");
        return 0;
    }

    Value *path = map_get(&opts, "path");
    if (path) {
        if (path->type != TYPE_STRING) {
            report_error("cookie path must be a string");
            return 0;
        }
        o->path = path->str_val;
    }

    Value *domain = map_get(&opts, "domain");
    if (domain) {
        if (domain->type != TYPE_STRING) {
            report_error("cookie domain must be a string");
            return 0;
        }
        o->domain = domain->str_val;
    }

    Value *expires = map_get(&opts, "expires");
    if (expires) {
        if (expires->type != TYPE_STRING) {
            report_error("cookie expires must be an HTTP date string");
            return 0;
        }
        o->expires = expires->str_val;
    }

    Value *max_age = map_get(&opts, "maxAge");
    if (max_age && max_age->type != TYPE_NULL) {
        if (max_age->type != TYPE_INT && max_age->type != TYPE_FLOAT) {
            report_error("cookie maxAge must be an integer (seconds)");
            return 0;
        }
        o->max_age = to_int(*max_age);
        if (o->max_age < 0) {
            report_error("cookie maxAge cannot be negative");
            return 0;
        }
    }

    Value *http_only = map_get(&opts, "httpOnly");
    if (http_only && http_only->type != TYPE_NULL) {
        o->http_only = to_bool(*http_only);
    }

    Value *secure = map_get(&opts, "secure");
    if (secure && secure->type != TYPE_NULL) {
        o->secure = to_bool(*secure);
    }

    Value *same_site = map_get(&opts, "sameSite");
    if (same_site && same_site->type != TYPE_NULL) {
        if (same_site->type == TYPE_BOOL && !same_site->int_val) {
            o->same_site = NULL;
        } else if (same_site->type == TYPE_STRING) {
            int valid = 0;
            const char *canonical = same_site_value(same_site->str_val, &valid);
            if (!valid) {
                report_error("cookie sameSite must be \"Lax\", \"Strict\", \"None\", or false");
                return 0;
            }
            o->same_site = canonical;
        } else {
            report_error("cookie sameSite must be a string or false");
            return 0;
        }
    }

    /* Browsers reject SameSite=None without Secure; make the safe choice
     * instead of emitting a cookie the browser will ignore. */
    if (o->same_site && strcmp(o->same_site, "None") == 0) {
        o->secure = 1;
    }

    if (!valid_cookie_attr(o->path) || !valid_cookie_attr(o->domain) ||
        !valid_cookie_attr(o->expires)) {
        report_error("cookie path/domain/expires must not contain ';' or line breaks");
        return 0;
    }

    if (clearing) {
        o->max_age = 0;
        o->expires = "Thu, 01 Jan 1970 00:00:00 GMT";
    }

    return 1;
}

static Value cookie_builtin(const char *name, Value *args, int arg_count, int clearing) {
    if (arg_count < 1 || arg_count > 3 || (arg_count < 2 && !clearing)) {
        report_error(clearing
            ? "clearCookie() requires (name [, options])"
            : "setCookie() requires (name, value [, options])");
        return make_bool(0);
    }
    if ((!clearing && arg_count > 3) || (clearing && arg_count > 2)) {
        report_error(clearing
            ? "clearCookie() requires (name [, options])"
            : "setCookie() requires (name, value [, options])");
        return make_bool(0);
    }
    if (args[0].type != TYPE_STRING || !valid_cookie_name(args[0].str_val)) {
        report_error("cookie name must be a non-empty token (letters, digits, _ - .)");
        return make_bool(0);
    }

    const char *value = "";
    int options_index = 1;

    if (!clearing) {
        if (args[1].type != TYPE_STRING || !valid_cookie_value(args[1].str_val)) {
            report_error("cookie value must be a string without ';' or control characters");
            return make_bool(0);
        }
        value = args[1].str_val;
        options_index = 2;
    }

    CookieOptions o;
    Value opts = (arg_count > options_index) ? args[options_index] : make_null();
    if (!parse_cookie_options(opts, &o, clearing)) {
        return make_bool(0);
    }

    if (nac_cookie_add(args[0].str_val, value, o.path, o.max_age, o.expires,
                       o.http_only, o.secure, o.same_site, o.domain) != 0) {
        report_error("setCookie: too many cookies or cookie line too long");
        return make_bool(0);
    }

    return make_bool(1);
}

static Value password_hash(Value pw) {
    if (pw.type != TYPE_STRING) {
        report_error("passwordHash() requires 1 string argument");
        return make_string("");
    }

    unsigned char salt[PASSWORD_SALT_LEN];
    unsigned char derived[PASSWORD_HASH_LEN];
    if (!nac_random_bytes(salt, sizeof(salt)) ||
        !nac_pbkdf2_sha256(pw.str_val, salt, sizeof(salt),
                           PASSWORD_ITERATIONS, derived, sizeof(derived))) {
        report_error("passwordHash(): crypto failure");
        return make_string("");
    }

    char salt_b64[64];
    char hash_b64[64];
    if (!nac_base64_encode(salt, sizeof(salt), salt_b64, sizeof(salt_b64), 0, 1) ||
        !nac_base64_encode(derived, sizeof(derived), hash_b64, sizeof(hash_b64), 0, 1)) {
        report_error("passwordHash(): crypto failure");
        return make_string("");
    }

    char out[256];
    snprintf(out, sizeof(out), "pbkdf2-sha256$%d$%s$%s",
             PASSWORD_ITERATIONS, salt_b64, hash_b64);
    return make_string(out);
}

static int password_verify(Value pw, Value hash) {
    if (pw.type != TYPE_STRING || hash.type != TYPE_STRING) return 0;

    char buf[512];
    strncpy(buf, hash.str_val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *algo = buf;
    char *sep = strchr(algo, '$');
    if (!sep) return 0;
    *sep = '\0';
    if (strcmp(algo, "pbkdf2-sha256") != 0) return 0;

    char *iter_s = sep + 1;
    sep = strchr(iter_s, '$');
    if (!sep) return 0;
    *sep = '\0';

    char *salt_s = sep + 1;
    sep = strchr(salt_s, '$');
    if (!sep) return 0;
    *sep = '\0';

    char *hash_s = sep + 1;
    if (strchr(hash_s, '$')) return 0;

    char *end = NULL;
    long iterations = strtol(iter_s, &end, 10);
    if (end == iter_s || *end != '\0') return 0;
    if (iterations < PASSWORD_MIN_ITERATIONS || iterations > PASSWORD_MAX_ITERATIONS) {
        return 0;
    }

    unsigned char salt[64];
    unsigned char expected[64];
    size_t salt_len = 0;
    size_t expected_len = 0;

    if (!nac_base64_decode(salt_s, salt, sizeof(salt), &salt_len)) return 0;
    if (!nac_base64_decode(hash_s, expected, sizeof(expected), &expected_len)) return 0;
    if (salt_len == 0 || expected_len != PASSWORD_HASH_LEN) return 0;

    unsigned char actual[PASSWORD_HASH_LEN];
    if (!nac_pbkdf2_sha256(pw.str_val, salt, salt_len,
                           (int)iterations, actual, sizeof(actual))) {
        return 0;
    }

    return nac_constant_time_equal(actual, sizeof(actual),
                                   expected, expected_len);
}

/* ---- JWT (HS256) ------------------------------------------------------- */

#define JWT_HEADER_JSON "{\"alg\":\"HS256\",\"typ\":\"JWT\"}"
#define JWT_MAX_HEADER_B64 256

static Value jwt_sign(Value payload, Value secret) {
    if (payload.type != TYPE_MAP || secret.type != TYPE_STRING) {
        report_error("jwtSign() requires (payload map, secret string)");
        return make_string("");
    }
    if (secret.str_val[0] == '\0') {
        report_error("jwtSign(): secret must not be empty");
        return make_string("");
    }

    char *json = json_stringify_value(payload);
    if (!json) {
        report_error("jwtSign(): could not serialize payload");
        return make_string("");
    }

    /* header.payload.signature must fit in one NaC string. */
    size_t json_len = strlen(json);
    char token[MAX_STRING_LEN];
    size_t used = 0;
    int ok = 1;

    if (!nac_base64_encode((const unsigned char *)JWT_HEADER_JSON, strlen(JWT_HEADER_JSON),
                           token, sizeof(token), 1, 0)) {
        ok = 0;
    } else {
        used = strlen(token);
        token[used++] = '.';
        token[used] = '\0';
        if (json_len > MAX_STRING_LEN ||
            !nac_base64_encode((const unsigned char *)json, json_len,
                               token + used, sizeof(token) - used, 1, 0)) {
            ok = 0;
        }
    }
    free(json);
    if (!ok) {
        report_error("jwtSign(): token would be longer than 8191 characters");
        return make_string("");
    }

    used = strlen(token);
    unsigned char mac[NAC_SHA256_LEN];
    if (!nac_hmac_sha256((const unsigned char *)secret.str_val, strlen(secret.str_val),
                         (const unsigned char *)token, used, mac)) {
        report_error("jwtSign(): crypto failure");
        return make_string("");
    }

    char sig[64];
    if (!nac_base64_encode(mac, sizeof(mac), sig, sizeof(sig), 1, 0) ||
        used + 1 + strlen(sig) >= sizeof(token)) {
        report_error("jwtSign(): token would be longer than 8191 characters");
        return make_string("");
    }
    token[used++] = '.';
    strcpy(token + used, sig);
    return make_string(token);
}

/* Returns the claims map, or null for any token that must not be trusted:
 * malformed, wrong algorithm, bad signature, expired (exp), or not yet valid
 * (nbf). Callers treat null as "unauthenticated". */
static Value jwt_verify(Value token_v, Value secret) {
    if (token_v.type != TYPE_STRING || secret.type != TYPE_STRING) {
        report_error("jwtVerify() requires (token string, secret string)");
        return make_null();
    }
    if (secret.str_val[0] == '\0') {
        report_error("jwtVerify(): secret must not be empty");
        return make_null();
    }

    char token[MAX_STRING_LEN];
    strcpy(token, token_v.str_val);

    char *dot1 = strchr(token, '.');
    char *dot2 = dot1 ? strchr(dot1 + 1, '.') : NULL;
    if (!dot1 || !dot2 || strchr(dot2 + 1, '.')) return make_null();
    if (dot1 == token || dot2 == dot1 + 1 || dot2[1] == '\0') return make_null();

    size_t signing_len = (size_t)(dot2 - token);       /* "header.payload" */
    char *header_b64 = token;
    char *payload_b64 = dot1 + 1;
    char *sig_b64 = dot2 + 1;
    *dot1 = '\0';
    *dot2 = '\0';

    /* 1. The algorithm must be exactly HS256 ("none" and every other value
     *    is refused), decided from the header before anything is trusted. */
    if (strlen(header_b64) > JWT_MAX_HEADER_B64) return make_null();
    unsigned char header_raw[JWT_MAX_HEADER_B64 + 1];
    size_t header_len = 0;
    if (!nac_base64_decode(header_b64, header_raw, sizeof(header_raw) - 1, &header_len)) {
        return make_null();
    }
    header_raw[header_len] = '\0';

    Value header;
    if (!json_try_parse_value((const char *)header_raw, &header)) return make_null();
    Value *alg = (header.type == TYPE_MAP) ? map_get(&header, "alg") : NULL;
    int alg_ok = alg && alg->type == TYPE_STRING && strcmp(alg->str_val, "HS256") == 0;
    free_value(&header);
    if (!alg_ok) return make_null();

    /* 2. Signature over the exact bytes received, compared in constant time. */
    unsigned char expected[NAC_SHA256_LEN];
    unsigned char given[NAC_SHA256_LEN + 8];
    size_t given_len = 0;
    *dot1 = '.';                                        /* restore for the MAC */
    if (!nac_hmac_sha256((const unsigned char *)secret.str_val, strlen(secret.str_val),
                         (const unsigned char *)token, signing_len, expected)) {
        report_error("jwtVerify(): crypto failure");
        return make_null();
    }
    *dot1 = '\0';
    if (!nac_base64_decode(sig_b64, given, sizeof(given), &given_len) ||
        given_len != NAC_SHA256_LEN ||
        !nac_constant_time_equal(expected, sizeof(expected), given, given_len)) {
        return make_null();
    }

    /* 3. Only now is the payload parsed. */
    unsigned char payload_raw[MAX_STRING_LEN];
    size_t payload_len = 0;
    if (!nac_base64_decode(payload_b64, payload_raw, sizeof(payload_raw) - 1, &payload_len)) {
        return make_null();
    }
    payload_raw[payload_len] = '\0';

    Value claims;
    if (!json_try_parse_value((const char *)payload_raw, &claims)) return make_null();
    if (claims.type != TYPE_MAP) {
        free_value(&claims);
        return make_null();
    }

    /* 4. Time claims, in seconds, no leeway. */
    double now = (double)time(NULL);
    Value *exp_claim = map_get(&claims, "exp");
    if (exp_claim) {
        if ((exp_claim->type != TYPE_INT && exp_claim->type != TYPE_FLOAT) ||
            now >= to_float(*exp_claim)) {
            free_value(&claims);
            return make_null();
        }
    }
    Value *nbf_claim = map_get(&claims, "nbf");
    if (nbf_claim) {
        if ((nbf_claim->type != TYPE_INT && nbf_claim->type != TYPE_FLOAT) ||
            now < to_float(*nbf_claim)) {
            free_value(&claims);
            return make_null();
        }
    }

    return claims;
}

bool is_security_builtin(const char *name) {
    static const char *builtins[] = {
        "randomBytes", "uuid", "sessionId",
        "sha256", "hmac", "base64Encode", "base64Decode",
        "safeEqual", "passwordHash", "passwordVerify",
        "setCookie", "clearCookie",
        "jwtSign", "jwtVerify"
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        if (strcmp(name, builtins[i]) == 0) return true;
    }
    return false;
}

Value call_security_builtin(const char *name, Value *args, int arg_count) {
    if (strcmp(name, "randomBytes") == 0) {
        if (arg_count != 1 || (args[0].type != TYPE_INT && args[0].type != TYPE_FLOAT)) {
            report_error("randomBytes() requires 1 integer argument (byte count)");
            return make_string("");
        }

        int n = to_int(args[0]);
        if (n < 1 || n > (MAX_STRING_LEN - 1) / 2) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "randomBytes() count must be between 1 and %d",
                     (MAX_STRING_LEN - 1) / 2);
            report_error(msg);
            return make_string("");
        }

        unsigned char *raw = (unsigned char *)malloc((size_t)n);
        if (!raw) {
            report_error("randomBytes(): out of memory");
            return make_string("");
        }
        if (!nac_random_bytes(raw, (size_t)n)) {
            free(raw);
            report_error("randomBytes(): crypto failure");
            return make_string("");
        }

        char hex[MAX_STRING_LEN];
        hex_encode(raw, (size_t)n, hex);
        free(raw);
        return make_string(hex);
    }

    if (strcmp(name, "uuid") == 0) {
        if (arg_count != 0) {
            report_error("uuid() requires 0 arguments");
            return make_string("");
        }

        unsigned char b[16];
        if (!nac_random_bytes(b, sizeof(b))) {
            report_error("uuid(): crypto failure");
            return make_string("");
        }

        b[6] = (unsigned char)((b[6] & 0x0f) | 0x40); /* version 4 */
        b[8] = (unsigned char)((b[8] & 0x3f) | 0x80); /* RFC 4122 variant */

        char out[40];
        snprintf(out, sizeof(out),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                 b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        return make_string(out);
    }

    if (strcmp(name, "sessionId") == 0) {
        if (arg_count != 0) {
            report_error("sessionId() requires 0 arguments");
            return make_string("");
        }

        unsigned char raw[32];
        if (!nac_random_bytes(raw, sizeof(raw))) {
            report_error("sessionId(): crypto failure");
            return make_string("");
        }

        char out[64];
        if (!nac_base64_encode(raw, sizeof(raw), out, sizeof(out), 1, 0)) {
            report_error("sessionId(): encoding failure");
            return make_string("");
        }
        return make_string(out);
    }

    if (strcmp(name, "sha256") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("sha256() requires 1 string argument");
            return make_string("");
        }

        unsigned char digest[NAC_SHA256_LEN];
        if (!nac_sha256((const unsigned char *)args[0].str_val,
                        strlen(args[0].str_val), digest)) {
            report_error("sha256(): crypto failure");
            return make_string("");
        }

        char hex[NAC_SHA256_LEN * 2 + 1];
        hex_encode(digest, sizeof(digest), hex);
        return make_string(hex);
    }

    if (strcmp(name, "hmac") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("hmac() requires 2 string arguments (key, message)");
            return make_string("");
        }

        unsigned char digest[NAC_SHA256_LEN];
        if (!nac_hmac_sha256((const unsigned char *)args[0].str_val,
                             strlen(args[0].str_val),
                             (const unsigned char *)args[1].str_val,
                             strlen(args[1].str_val), digest)) {
            report_error("hmac(): crypto failure");
            return make_string("");
        }

        char hex[NAC_SHA256_LEN * 2 + 1];
        hex_encode(digest, sizeof(digest), hex);
        return make_string(hex);
    }

    if (strcmp(name, "base64Encode") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("base64Encode() requires 1 string argument");
            return make_string("");
        }

        char out[MAX_STRING_LEN];
        if (!nac_base64_encode((const unsigned char *)args[0].str_val,
                               strlen(args[0].str_val),
                               out, sizeof(out), 0, 1)) {
            report_error("base64Encode(): result is too long");
            return make_string("");
        }
        return make_string(out);
    }

    if (strcmp(name, "base64Decode") == 0) {
        if (arg_count != 1 || args[0].type != TYPE_STRING) {
            report_error("base64Decode() requires 1 string argument");
            return make_string("");
        }

        unsigned char out[MAX_STRING_LEN];
        size_t out_len = 0;
        if (!nac_base64_decode(args[0].str_val, out, sizeof(out) - 1, &out_len)) {
            report_error("base64Decode(): invalid base64");
            return make_string("");
        }
        out[out_len] = '\0';
        return make_string((const char *)out);
    }

    if (strcmp(name, "safeEqual") == 0) {
        if (arg_count != 2 || args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("safeEqual() requires 2 string arguments");
            return make_bool(0);
        }

        return make_bool(nac_constant_time_equal(
            (const unsigned char *)args[0].str_val, strlen(args[0].str_val),
            (const unsigned char *)args[1].str_val, strlen(args[1].str_val)));
    }

    if (strcmp(name, "passwordHash") == 0) {
        if (arg_count != 1) {
            report_error("passwordHash() requires 1 string argument");
            return make_string("");
        }
        return password_hash(args[0]);
    }

    if (strcmp(name, "passwordVerify") == 0) {
        if (arg_count != 2) {
            report_error("passwordVerify() requires 2 string arguments (password, hash)");
            return make_bool(0);
        }
        return make_bool(password_verify(args[0], args[1]));
    }

    if (strcmp(name, "jwtSign") == 0) {
        if (arg_count != 2) {
            report_error("jwtSign() requires 2 arguments (payload map, secret)");
            return make_string("");
        }
        return jwt_sign(args[0], args[1]);
    }

    if (strcmp(name, "jwtVerify") == 0) {
        if (arg_count != 2) {
            report_error("jwtVerify() requires 2 arguments (token, secret)");
            return make_null();
        }
        return jwt_verify(args[0], args[1]);
    }

    if (strcmp(name, "setCookie") == 0) {
        return cookie_builtin(name, args, arg_count, 0);
    }

    if (strcmp(name, "clearCookie") == 0) {
        return cookie_builtin(name, args, arg_count, 1);
    }

    report_error("Unknown security built-in function");
    return make_null();
}
