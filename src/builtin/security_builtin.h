#ifndef NAC_SECURITY_BUILTIN_H
#define NAC_SECURITY_BUILTIN_H

#include <stdbool.h>

#include "../runtime/value.h"

/* Security / account builtins:
 *   randomBytes(n)  uuid()  sessionId()
 *   sha256(s)  hmac(key, s)  base64Encode(s)  base64Decode(s)
 *   safeEqual(a, b)  passwordHash(pw)  passwordVerify(pw, hash)
 *   setCookie(name, value [, options])  clearCookie(name [, options])
 *   jwtSign(payload, secret)  jwtVerify(token, secret)
 *
 * JWTs are HS256 only. jwtSign() signs a claims map; add "exp"/"nbf"
 * (seconds, e.g. now() + 3600) yourself. jwtVerify() returns the claims map,
 * or null for a malformed, wrongly signed, expired or not-yet-valid token.
 */
bool is_security_builtin(const char *name);
Value call_security_builtin(const char *name, Value *args, int arg_count);

#endif
