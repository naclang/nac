#ifndef TYPES_H
#define TYPES_H

typedef enum { 
    TYPE_NUMBER, 
    TYPE_STRING, 
    TYPE_ARRAY 
} VarType;

typedef struct {
    char name[256];
    VarType type;
    int numberValue;
    char stringValue[1024];
    int isConst;
    
    int* arrayValues;
    int arraySize;
    int arrayCapacity;
} Variable;

typedef struct {
    char name[256];
    char params[10][256];
    int paramCount;
    long bodyStartPos;
    int bodyStartLine;
} Function;

typedef struct {
    int conditionResult;
    int hasElse;
    int inElseBlock;
} IfBlock;

typedef struct {
    long startPos;
    int startLine;
    char varName[256];
    int start;
    int end;
    int current;
} LoopInfo;

typedef struct {
    long startPos;
    int startLine;
    char left[512];
} WhileInfo;

#endif
