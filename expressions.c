#include "expressions.h"
#include "variables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern void trim_whitespace(char* s);
extern int find_unquoted_char_index(const char *s, char target);

int evaluateCondition(const char* condition) {
    char cond[1024];
    strncpy(cond, condition, sizeof(cond)-1);
    cond[sizeof(cond)-1] = 0;
    trim_whitespace(cond);
    
    // NOT operatörü kontrolü
    if (strncmp(cond, "not ", 4) == 0) {
        return !evaluateCondition(cond + 4);
    }
    
    // OR operatörü kontrolü
    char* orPos = strstr(cond, " or ");
    if (orPos) {
        char left[512], right[512];
        int leftLen = orPos - cond;
        strncpy(left, cond, leftLen);
        left[leftLen] = 0;
        strcpy(right, orPos + 4);
        return evaluateCondition(left) || evaluateCondition(right);
    }
    
    // AND operatörü kontrolü
    char* andPos = strstr(cond, " and ");
    if (andPos) {
        char left[512], right[512];
        int leftLen = andPos - cond;
        strncpy(left, cond, leftLen);
        left[leftLen] = 0;
        strcpy(right, andPos + 5);
        return evaluateCondition(left) && evaluateCondition(right);
    }
    
    // Karşılaştırma operatörleri
    char left[256], op[3], right[256];
    if (sscanf(cond, "%255s %2s %255s", left, op, right) == 3) {
        Variable* lvar = findVariable(left);
        Variable* rvar = findVariable(right);
        int lnum, rnum;
        
        if (lvar) { 
            if (lvar->type == TYPE_NUMBER) 
                lnum = lvar->numberValue; 
            else { 
                fprintf(stderr, "Hata: string ile karşılaştırma desteklenmiyor\n"); 
                exit(1); 
            } 
        } else {
            lnum = atoi(left);
        }
        
        if (rvar) { 
            if (rvar->type == TYPE_NUMBER) 
                rnum = rvar->numberValue; 
            else { 
                fprintf(stderr, "Hata: string ile karşılaştırma desteklenmiyor\n"); 
                exit(1); 
            } 
        } else {
            rnum = atoi(right);
        }
        
        if (strcmp(op, "==") == 0) return lnum == rnum;
        if (strcmp(op, "!=") == 0) return lnum != rnum;
        if (strcmp(op, ">") == 0) return lnum > rnum;
        if (strcmp(op, "<") == 0) return lnum < rnum;
        if (strcmp(op, ">=") == 0) return lnum >= rnum;
        if (strcmp(op, "<=") == 0) return lnum <= rnum;
        
        fprintf(stderr, "Hata: bilinmeyen operatör: %s\n", op);
        exit(1);
    }
    
    // Tek değişken/sayı (truthy kontrolü)
    Variable* v = findVariable(cond);
    if (v) {
        if (v->type == TYPE_NUMBER) return v->numberValue != 0;
        if (v->type == TYPE_STRING) return strlen(v->stringValue) > 0;
        if (v->type == TYPE_ARRAY) return v->arraySize > 0;
    }
    
    return atoi(cond) != 0;
}

void evaluateExpression(const char* expr, char* result, size_t resultSize) {
    result[0] = 0;
    
    if (strchr(expr, '[') && strchr(expr, ']')) {
        char arrName[256];
        int index;
        if (sscanf(expr, "%255[^[][%d]", arrName, &index) == 2) {
            trim_whitespace(arrName);
            int value = getArrayElement(arrName, index);
            snprintf(result, resultSize, "%d", value);
            return;
        }
    }
    
    char* dotPos = strchr(expr, '.');
    if (dotPos) {
        char arrName[256];
        int nameLen = dotPos - expr;
        strncpy(arrName, expr, nameLen);
        arrName[nameLen] = 0;
        trim_whitespace(arrName);
        
        char* method = dotPos + 1;
        
        if (strcmp(method, "length") == 0) {
            int len = getArrayLength(arrName);
            snprintf(result, resultSize, "%d", len);
            return;
        }
        else if (strncmp(method, "push(", 5) == 0) {
            char valueStr[256];
            if (sscanf(method, "push(%255[^)])", valueStr) == 1) {
                char evaluated[1024];
                evaluateExpression(valueStr, evaluated, sizeof(evaluated));
                pushToArray(arrName, atoi(evaluated));
                snprintf(result, resultSize, "%d", atoi(evaluated));
                return;
            }
        }
        else if (strcmp(method, "pop()") == 0) {
            int value = popFromArray(arrName);
            snprintf(result, resultSize, "%d", value);
            return;
        }
    }
    
    typedef enum { TOKEN_NUMBER, TOKEN_STRING, TOKEN_OPERATOR } TokenType;
    
    typedef struct {
        TokenType type;
        int numValue;
        char strValue[1024];
        char op;
    } Token;
    
    Token tokens[64];
    int tokenCount = 0;
    
    int i = 0;
    int hasString = 0;
    
    while (expr[i] && tokenCount < 64) {
        while (expr[i] && isspace((unsigned char)expr[i])) i++;
        if (!expr[i]) break;
        
        if (expr[i] == '"') {
            tokens[tokenCount].type = TOKEN_STRING;
            hasString = 1;
            i++;
            int j = 0;
            while (expr[i] && expr[i] != '"' && j < sizeof(tokens[tokenCount].strValue)-1) {
                if (expr[i] == '\\') {
                    i++;
                    switch (expr[i]) {
                        case 'n': tokens[tokenCount].strValue[j++] = '\n'; break;
                        case 't': tokens[tokenCount].strValue[j++] = '\t'; break;
                        case 'r': tokens[tokenCount].strValue[j++] = '\r'; break;
                        case '\\': tokens[tokenCount].strValue[j++] = '\\'; break;
                        case '"': tokens[tokenCount].strValue[j++] = '"'; break;
                        default: tokens[tokenCount].strValue[j++] = expr[i]; break;
                    }
                    i++;
                } else {
                    tokens[tokenCount].strValue[j++] = expr[i++];
                }
            }
            tokens[tokenCount].strValue[j] = 0;
            if (expr[i] == '"') i++;
            tokenCount++;
        }
        else if (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/' || expr[i] == '%') {
            if (expr[i] == '-' && (tokenCount == 0 || tokens[tokenCount-1].type == TOKEN_OPERATOR)) {
                i++;
                char num[64];
                int j = 0;
                num[j++] = '-';
                while (expr[i] && isdigit((unsigned char)expr[i]) && j < sizeof(num)-1) {
                    num[j++] = expr[i++];
                }
                num[j] = 0;
                
                tokens[tokenCount].type = TOKEN_NUMBER;
                tokens[tokenCount].numValue = atoi(num);
                tokenCount++;
            } else {
                tokens[tokenCount].type = TOKEN_OPERATOR;
                tokens[tokenCount].op = expr[i];
                i++;
                tokenCount++;
            }
        }
        else if (isalpha((unsigned char)expr[i]) || expr[i] == '_') {
            char varname[256];
            int j = 0;
            while ((isalnum((unsigned char)expr[i]) || expr[i] == '_') && j < sizeof(varname)-1) {
                varname[j++] = expr[i++];
            }
            varname[j] = 0;
            
            Variable* v = findVariable(varname);
            if (!v) {
                fprintf(stderr, "Hata: değişken bulunamadı: %s\n", varname);
                exit(1);
            }
            
            if (v->type == TYPE_STRING) {
                tokens[tokenCount].type = TOKEN_STRING;
                hasString = 1;
                strncpy(tokens[tokenCount].strValue, v->stringValue, sizeof(tokens[tokenCount].strValue)-1);
                tokens[tokenCount].strValue[sizeof(tokens[tokenCount].strValue)-1] = 0;
            } else {
                tokens[tokenCount].type = TOKEN_NUMBER;
                tokens[tokenCount].numValue = v->numberValue;
            }
            tokenCount++;
        }
        else if (isdigit((unsigned char)expr[i])) {
            char num[64];
            int j = 0;
            while (expr[i] && isdigit((unsigned char)expr[i]) && j < sizeof(num)-1) {
                num[j++] = expr[i++];
            }
            num[j] = 0;
            
            tokens[tokenCount].type = TOKEN_NUMBER;
            tokens[tokenCount].numValue = atoi(num);
            tokenCount++;
        }
        else {
            i++;
        }
    }
    
    if (tokenCount == 0) return;
    
    if (hasString) {
        size_t currentLen = 0;
        for (int t = 0; t < tokenCount; t++) {
            if (tokens[t].type == TOKEN_OPERATOR) continue;
            
            size_t remaining = resultSize - currentLen - 1;
            if (remaining > 0) {
                if (tokens[t].type == TOKEN_NUMBER) {
                    size_t written = snprintf(result + currentLen, remaining + 1, "%d", tokens[t].numValue);
                    if (written < remaining) currentLen += written;
                    else currentLen = resultSize - 1;
                } else {
                    size_t written = snprintf(result + currentLen, remaining + 1, "%s", tokens[t].strValue);
                    if (written < remaining) currentLen += written;
                    else currentLen = resultSize - 1;
                }
            }
        }
        return;
    }
    
    for (int t = 1; t < tokenCount - 1; t++) {
        if (tokens[t].type == TOKEN_OPERATOR && 
            (tokens[t].op == '*' || tokens[t].op == '/' || tokens[t].op == '%')) {
            
            if (tokens[t-1].type != TOKEN_NUMBER || tokens[t+1].type != TOKEN_NUMBER) {
                fprintf(stderr, "Hata: geçersiz ifade\n");
                exit(1);
            }
            
            int leftVal = tokens[t-1].numValue;
            int rightVal = tokens[t+1].numValue;
            int resultVal = 0;
            
            if (tokens[t].op == '*') {
                resultVal = leftVal * rightVal;
            } else if (tokens[t].op == '/') {
                if (rightVal == 0) {
                    fprintf(stderr, "Hata: sıfıra bölme\n");
                    exit(1);
                }
                resultVal = leftVal / rightVal;
            } else if (tokens[t].op == '%') {
                if (rightVal == 0) {
                    fprintf(stderr, "Hata: sıfıra mod alma\n");
                    exit(1);
                }
                resultVal = leftVal % rightVal;
            }
            
            tokens[t-1].numValue = resultVal;
            
            for (int k = t; k < tokenCount - 2; k++) {
                tokens[k] = tokens[k+2];
            }
            tokenCount -= 2;
            t--;
        }
    }
    
    for (int t = 1; t < tokenCount - 1; t++) {
        if (tokens[t].type == TOKEN_OPERATOR && 
            (tokens[t].op == '+' || tokens[t].op == '-')) {
            
            if (tokens[t-1].type != TOKEN_NUMBER || tokens[t+1].type != TOKEN_NUMBER) {
                fprintf(stderr, "Hata: geçersiz ifade\n");
                exit(1);
            }
            
            int leftVal = tokens[t-1].numValue;
            int rightVal = tokens[t+1].numValue;
            int resultVal = 0;
            
            if (tokens[t].op == '+') {
                resultVal = leftVal + rightVal;
            } else if (tokens[t].op == '-') {
                resultVal = leftVal - rightVal;
            }
            
            tokens[t-1].numValue = resultVal;
            
            for (int k = t; k < tokenCount - 2; k++) {
                tokens[k] = tokens[k+2];
            }
            tokenCount -= 2;
            t--;
        }
    }
    
    if (tokenCount == 1 && tokens[0].type == TOKEN_NUMBER) {
        snprintf(result, resultSize, "%d", tokens[0].numValue);
    } else {
        fprintf(stderr, "Hata: ifade değerlendirilemedi\n");
        exit(1);
    }
}
