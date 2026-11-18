#include "registry.h"

#include "builtin.h"
#include "db_builtin.h"
#include "extended_builtin.h"
#include "security_builtin.h"
#include "time_builtin.h"

typedef struct {
    NacBuiltinKind kind;
    bool (*has)(const char *name);
    Value (*call)(const char *name, Value *args, int arg_count);
} BuiltinModule;

/* Order matters only if two modules ever claim the same name: first wins. */
static const BuiltinModule modules[] = {
    { NAC_BUILTIN_CORE,     is_builtin_function,  call_builtin_function  },
    { NAC_BUILTIN_WEB,      is_extended_builtin,  call_extended_builtin  },
    { NAC_BUILTIN_SECURITY, is_security_builtin,  call_security_builtin  },
    { NAC_BUILTIN_TIME,     is_time_builtin,      call_time_builtin      },
    { NAC_BUILTIN_DB,       is_db_builtin,        call_db_builtin        },
};

NacBuiltinKind nac_builtin_dispatch(const char *name, Value *args, int arg_count, Value *out) {
    for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
        if (modules[i].has(name)) {
            *out = modules[i].call(name, args, arg_count);
            return modules[i].kind;
        }
    }
    return NAC_BUILTIN_NONE;
}
