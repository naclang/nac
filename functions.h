#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "types.h"
#include <stdio.h>

extern Function functions[50];
extern int functionCount;

void registerFunction(const char* name, char params[][256], int paramCount, long bodyPos, int bodyLine);
Function* findFunction(const char* name);
void callFunction(const char* name, char args[][256], int argCount, FILE* f, int* lineNumber);

#endif
