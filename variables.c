#include "variables.h"
#include "expressions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Variable vars[100];
int varCount = 0;

Variable* findVariable(const char* name) {
    for (int i = 0; i < varCount; i++)
        if (strcmp(vars[i].name, name) == 0)
            return &vars[i];
    return NULL;
}

Variable* createVariable(const char* name) {
    if (varCount >= 100) { 
        fprintf(stderr, "Hata: maksimum değişken sayısına ulaşıldı (100)\n"); 
        exit(1); 
    }
    Variable* v = &vars[varCount++];
    strncpy(v->name, name, sizeof(v->name)-1);
    v->name[sizeof(v->name)-1] = 0;
    v->isConst = 0;
    v->arrayValues = NULL;
    v->arraySize = 0;
    v->arrayCapacity = 0;
    return v;
}

void setVariable(const char* name, const char* value, int makeConst) {
    Variable* v = findVariable(name);
    
    if (v && v->isConst) {
        fprintf(stderr, "Hata: const değişken '%s' tekrar atanamaz\n", name);
        return;
    }

    if (!v) {
        v = createVariable(name);
    }

    if (value[0] == '[') {
        v->type = TYPE_ARRAY;
        v->arrayCapacity = 10;
        v->arrayValues = (int*)malloc(v->arrayCapacity * sizeof(int));
        v->arraySize = 0;
        
        char* ptr = (char*)value + 1;
        while (*ptr && *ptr != ']') {
            while (*ptr == ' ' || *ptr == ',') ptr++;
            if (*ptr == ']') break;
            
            char numStr[256];
            int i = 0;
            while (*ptr && *ptr != ',' && *ptr != ']' && i < 255) {
                numStr[i++] = *ptr++;
            }
            numStr[i] = 0;
            
            char evaluated[1024];
            evaluateExpression(numStr, evaluated, sizeof(evaluated));
            
            if (v->arraySize >= v->arrayCapacity) {
                v->arrayCapacity *= 2;
                v->arrayValues = (int*)realloc(v->arrayValues, v->arrayCapacity * sizeof(int));
            }
            v->arrayValues[v->arraySize++] = atoi(evaluated);
        }
        return;
    }

    char evaluated[1024];
    evaluateExpression(value, evaluated, sizeof(evaluated));
    
    int isNumber = 1;
    int i = 0;
    if (evaluated[0] == '-') i = 1;
    if (evaluated[i] == '\0') isNumber = 0;
    
    while (evaluated[i] != '\0') {
        if (!isdigit((unsigned char)evaluated[i])) {
            isNumber = 0;
            break;
        }
        i++;
    }
    
    if (isNumber && evaluated[0] != '\0') {
        v->type = TYPE_NUMBER;
        v->numberValue = atoi(evaluated);
    } else {
        v->type = TYPE_STRING;
        strncpy(v->stringValue, evaluated, sizeof(v->stringValue)-1);
        v->stringValue[sizeof(v->stringValue)-1] = 0;
    }
    v->isConst = makeConst;
}

void setArrayElement(const char* name, int index, int value) {
    Variable* v = findVariable(name);
    if (!v || v->type != TYPE_ARRAY) {
        fprintf(stderr, "Hata: '%s' bir dizi değil\n", name);
        exit(1);
    }
    if (index < 0 || index >= v->arraySize) {
        fprintf(stderr, "Hata: dizi indeksi sınırların dışında: %d\n", index);
        exit(1);
    }
    v->arrayValues[index] = value;
}

int getArrayElement(const char* name, int index) {
    Variable* v = findVariable(name);
    if (!v || v->type != TYPE_ARRAY) {
        fprintf(stderr, "Hata: '%s' bir dizi değil\n", name);
        exit(1);
    }
    if (index < 0 || index >= v->arraySize) {
        fprintf(stderr, "Hata: dizi indeksi sınırların dışında: %d\n", index);
        exit(1);
    }
    return v->arrayValues[index];
}

void pushToArray(const char* name, int value) {
    Variable* v = findVariable(name);
    if (!v || v->type != TYPE_ARRAY) {
        fprintf(stderr, "Hata: '%s' bir dizi değil\n", name);
        exit(1);
    }
    if (v->arraySize >= v->arrayCapacity) {
        v->arrayCapacity *= 2;
        v->arrayValues = (int*)realloc(v->arrayValues, v->arrayCapacity * sizeof(int));
    }
    v->arrayValues[v->arraySize++] = value;
}

int popFromArray(const char* name) {
    Variable* v = findVariable(name);
    if (!v || v->type != TYPE_ARRAY) {
        fprintf(stderr, "Hata: '%s' bir dizi değil\n", name);
        exit(1);
    }
    if (v->arraySize == 0) {
        fprintf(stderr, "Hata: boş diziden pop yapılamaz\n");
        exit(1);
    }
    return v->arrayValues[--v->arraySize];
}

int getArrayLength(const char* name) {
    Variable* v = findVariable(name);
    if (!v || v->type != TYPE_ARRAY) {
        fprintf(stderr, "Hata: '%s' bir dizi değil\n", name);
        exit(1);
    }
    return v->arraySize;
}
