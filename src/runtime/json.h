#ifndef NAC_JSON_H
#define NAC_JSON_H

#include <stdbool.h>

#include "value.h"

bool json_parse_value(const char *json, Value *out);
char *json_stringify_value(Value value);

#endif
