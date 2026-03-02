#ifndef NAC_EVAL_H
#define NAC_EVAL_H

#include "../parser/ast.h"
#include "value.h"

Value eval_node(ASTNode *node);

#endif
