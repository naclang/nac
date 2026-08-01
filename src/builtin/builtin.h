#ifndef NAC_BUILTIN_H
#define NAC_BUILTIN_H

#include <stdbool.h>
#include <stddef.h>

#include "../runtime/value.h"

bool is_builtin_function(const char *name);
Value call_builtin_function(const char *name, Value *args, int arg_count);

/* Turns any Value into its display string form (used by toString(), and
 * shared with other modules like the HTTP server). */
void value_to_display_string(Value v, char *out, size_t out_size);

#endif
