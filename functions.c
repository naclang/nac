#include "functions.h"
#include "variables.h"
#include "expressions.h"
#include "io.h"
#include "utils.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

Function functions[50];
int functionCount = 0;

extern void trim_whitespace(char* s);
extern int executeBlock(FILE* f, int* lineNumber, const char* endKeyword);

void registerFunction(const char* name, char params[][256], int paramCount, long bodyPos, int bodyLine) {
    if (functionCount >= 50) {
        fprintf(stderr, "Hata: maksimum fonksiyon sayısına ulaşıldı\n");
        exit(1);
    }
    
    Function* func = &functions[functionCount++];
    strncpy(func->name, name, sizeof(func->name)-1);
    func->name[sizeof(func->name)-1] = 0;
    func->paramCount = paramCount;
    for (int i = 0; i < paramCount; i++) {
        strncpy(func->params[i], params[i], sizeof(func->params[i])-1);
        func->params[i][sizeof(func->params[i])-1] = 0;
    }
    func->bodyStartPos = bodyPos;
    func->bodyStartLine = bodyLine;
}

Function* findFunction(const char* name) {
    for (int i = 0; i < functionCount; i++) {
        if (strcmp(functions[i].name, name) == 0) {
            return &functions[i];
        }
    }
    return NULL;
}

void callFunction(const char* name, char args[][256], int argCount, FILE* f, int* lineNumber) {
    Function* func = findFunction(name);
    if (!func) {
        fprintf(stderr, "Hata: fonksiyon bulunamadı: %s\n", name);
        exit(1);
    }
    
    if (argCount != func->paramCount) {
        fprintf(stderr, "Hata: fonksiyon '%s' %d parametre bekliyor, %d verildi\n", 
                name, func->paramCount, argCount);
        exit(1);
    }
    
    // Mevcut değişkenleri kaydet (basit scope)
    int savedVarCount = varCount;
    
    // Parametreleri yerel değişken olarak ayarla
    for (int i = 0; i < argCount; i++) {
        char evaluated[1024];
        evaluateExpression(args[i], evaluated, sizeof(evaluated));
        setVariable(func->params[i], evaluated, 0);
    }
    
    // Fonksiyon gövdesini çalıştır
    long savedPos = ftell(f);
    int savedLine = *lineNumber;
    
    fseek(f, func->bodyStartPos, SEEK_SET);
    *lineNumber = func->bodyStartLine;
    
    executeBlock(f, lineNumber, "end");
    
    // Yerel değişkenleri temizle
    varCount = savedVarCount;
    
    // Eski pozisyona dön
    fseek(f, savedPos, SEEK_SET);
    *lineNumber = savedLine;
}

int executeBlock(FILE* f, int* lineNumber, const char* endKeyword) {
    char line[2048];
    
    while (fgets(line, sizeof(line), f)) {
        (*lineNumber)++;
        line[strcspn(line, "\r\n")] = 0;
        
        char* l = line;
        while (*l == ' ' || *l == '\t') l++;
        
        if (l[0] == '\0') continue;
        
        char* commentPos = strchr(l, '#');
        if (commentPos) *commentPos = '\0';
        
        trim_whitespace(l);
        
        if (strcmp(l, endKeyword) == 0) {
            return 1;
        }
        
        // Variable assignment
        int eqPos = find_unquoted_char_index(l, '=');
        if (eqPos >= 0 && strncmp(l, "var ", 4) != 0 && strncmp(l, "const ", 6) != 0) {
            char left[256], right[1024];
            strncpy(left, l, eqPos);
            left[eqPos] = 0;
            strcpy(right, l + eqPos + 1);
            trim_whitespace(left);
            trim_whitespace(right);
            setVariable(left, right, 0);
            continue;
        }
        
        // var declaration
        if (strncmp(l, "var ", 4) == 0) {
            char rest[1024];
            if (sscanf(l, "var %1023[^\n]", rest) == 1) {
                char name[256], colon[2], value[1024];
                if (sscanf(rest, "%255s %1s %1023[^\n]", name, colon, value) == 3 && strcmp(colon, "=") == 0) {
                    setVariable(name, value, 0);
                }
            }
            continue;
        }
        
        // print
        if (strncmp(l, "print ", 6) == 0) {
            char* rest = l + 6;
            while (*rest == ' ' || *rest == '\t') rest++;
            printValue(rest, 1);
            continue;
        }
        
        // input
        if (strncmp(l, "input ", 6) == 0) {
            char varName[256];
            if (sscanf(l + 6, "%255s", varName) == 1) {
                readInput(varName);
            }
            continue;
        }
    }
    
    return 0;
}
