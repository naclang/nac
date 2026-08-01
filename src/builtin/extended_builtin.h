#ifndef NAC_EXT_BUILTIN_H
#define NAC_EXT_BUILTIN_H

#include <stdbool.h>

#include "../runtime/value.h"

bool is_extended_builtin(const char *name);
Value call_extended_builtin(const char *name, Value *args, int arg_count);

#endif
