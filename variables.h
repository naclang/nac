#ifndef VARIABLES_H
#define VARIABLES_H

#include "types.h"

extern Variable vars[100];
extern int varCount;

Variable* findVariable(const char* name);
Variable* createVariable(const char* name);
void setVariable(const char* name, const char* value, int makeConst);
void setArrayElement(const char* name, int index, int value);
int getArrayElement(const char* name, int index);
void pushToArray(const char* name, int value);
int popFromArray(const char* name);
int getArrayLength(const char* name);

#endif
