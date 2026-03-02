#ifndef NAC_BUILTIN_H
#define NAC_BUILTIN_H

#include <stdbool.h>

#include "../runtime/value.h"

bool is_builtin_function(const char *name);
Value call_builtin_function(const char *name, Value *args, int arg_count);

#endif
