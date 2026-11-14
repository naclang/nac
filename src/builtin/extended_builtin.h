#ifndef NAC_EXT_BUILTIN_H
#define NAC_EXT_BUILTIN_H

#include <stdbool.h>

#include "../runtime/value.h"

bool is_extended_builtin(const char *name);
Value call_extended_builtin(const char *name, Value *args, int arg_count);

/* Implements `http.get { ... }`, `http.post { ... }`, `http.put { ... }`,
 * `http.patch { ... }` and `http.delete { ... }`.
 *
 * `opts` is the { ... } map:
 *   url       (string, required)
 *   body      (string sent as-is, or map/array sent as JSON)  -- not for GET
 *   headers   (map of header name -> value)
 *   timeout   (seconds, default 30)
 *   callback  (function called with the response once it arrives)
 *
 * Returns the response map { status, ok, body, data, error }. */
Value http_call_with_options(const char *method, Value opts);

#endif
