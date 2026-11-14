#ifndef NAC_SECURITY_BUILTIN_H
#define NAC_SECURITY_BUILTIN_H

#include <stdbool.h>

#include "../runtime/value.h"

/* Security / account builtins:
 *   randomBytes(n)  uuid()  sessionId()
 *   sha256(s)  hmac(key, s)  base64Encode(s)  base64Decode(s)
 *   safeEqual(a, b)  passwordHash(pw)  passwordVerify(pw, hash)
 *   setCookie(name, value [, options])  clearCookie(name [, options])
 */
bool is_security_builtin(const char *name);
Value call_security_builtin(const char *name, Value *args, int arg_count);

#endif
