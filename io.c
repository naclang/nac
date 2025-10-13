#include "io.h"
#include "expressions.h"
#include "variables.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void printValue(const char* expr, int newline) {
    char result[2048];
    evaluateExpression(expr, result, sizeof(result));
    if (newline)
        printf("%s\n", result);
    else
        printf("%s", result);
    fflush(stdout);
}

void readInput(const char* varName) {
    char input[1024];

    if (fgets(input, sizeof(input), stdin) == NULL) {
        fprintf(stderr, "Hata: input okunamadı\n");
        exit(1);
    }
    
    input[strcspn(input, "\r\n")] = 0;
    
    if (!is_valid_utf8(input)) {
        fprintf(stderr, "Hata: geçersiz UTF-8 karakteri girdiniz\n");
        exit(1);
    }
    
    Variable* v = findVariable(varName);
    if (!v) {
        if (varCount >= 100) {
            fprintf(stderr, "Hata: maksimum değişken sayısına ulaşıldı\n");
            exit(1);
        }
        v = &vars[varCount++];
        strncpy(v->name, varName, sizeof(v->name)-1);
    }
    
    int isNumber = 1;
    int i = 0;
    
    if (input[0] == '-') i = 1;
    
    if (input[i] == '\0') isNumber = 0;
    
    while (input[i] != '\0') {
        if (!isdigit((unsigned char)input[i])) {
            isNumber = 0;
            break;
        }
        i++;
    }
    
    if (isNumber && input[0] != '\0') {
        v->type = TYPE_NUMBER;
        v->numberValue = atoi(input);
    } else {
        v->type = TYPE_STRING;
        strncpy(v->stringValue, input, sizeof(v->stringValue)-1);
        v->stringValue[sizeof(v->stringValue)-1] = 0;
    }
}
