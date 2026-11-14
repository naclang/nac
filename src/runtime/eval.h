#ifndef NAC_EVAL_H
#define NAC_EVAL_H

#include "../parser/ast.h"
#include "value.h"

Value eval_node(ASTNode *node);

/* Calls a user-defined NaC function (e.g. an HTTP route handler) from C code.
 * Returns 1 and sets *out_result if the function exists, 0 otherwise. */
int call_nac_function(const char *name, Value *args, int arg_count, Value *out_result);

/* Calls a function *value* (TYPE_FUNC) with the given arguments. Unlike
 * call_nac_function() this is lenient like JavaScript: missing arguments
 * become null and extra ones are dropped, so a callback may declare fewer
 * parameters than it is called with. Returns 1 if called, 0 if `fn` is not
 * a valid function value. */
int call_func_value(Value fn, Value *args, int arg_count, Value *out_result);

#endif
