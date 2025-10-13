#ifndef EXPRESSIONS_H
#define EXPRESSIONS_H

#include <stddef.h>

void evaluateExpression(const char* expr, char* result, size_t resultSize);
int evaluateCondition(const char* condition);

#endif
