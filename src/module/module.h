#ifndef NAC_MODULE_H
#define NAC_MODULE_H

#include "../runtime/value.h"

int module_register(const char *name, Value module_value);
Value module_get_copy(const char *name, int *found);
Value module_load_json_file(const char *path, int *ok);
Value module_require_local(const char *name, int *ok);
Value module_list_names(void);

#endif
