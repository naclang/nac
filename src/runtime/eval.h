#ifndef NAC_EVAL_H
#define NAC_EVAL_H

#include "../parser/ast.h"
#include "value.h"

Value eval_node(ASTNode *node);

/* Calls a user-defined NaC function (e.g. an HTTP route handler) from C code.
 * Returns 1 and sets *out_result if the function exists, 0 otherwise. */
int call_nac_function(const char *name, Value *args, int arg_count, Value *out_result);

#endif
