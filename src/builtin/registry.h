#ifndef NAC_BUILTIN_REGISTRY_H
#define NAC_BUILTIN_REGISTRY_H

#include "../runtime/value.h"

/* Which builtin module handled a call. */
typedef enum {
    NAC_BUILTIN_NONE = 0,   /* name is not a builtin: caller falls back to user functions */
    NAC_BUILTIN_CORE,       /* builtin.c: strings, arrays, maps, I/O ... */
    NAC_BUILTIN_WEB,        /* extended_builtin.c: HTTP client/server, routing */
    NAC_BUILTIN_SECURITY,   /* security_builtin.c: hashing, JWT, cookies */
    NAC_BUILTIN_TIME,       /* time_builtin.c */
    NAC_BUILTIN_DB          /* db_builtin.c */
} NacBuiltinKind;

/* Single entry point for every native function. Looks `name` up in each
 * builtin module in a fixed order; on a match it runs the call, stores the
 * result in *out and returns the module that handled it. `args` stays owned
 * by the caller. */
NacBuiltinKind nac_builtin_dispatch(const char *name, Value *args, int arg_count, Value *out);

#endif
