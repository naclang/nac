#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include "types.h"
#include "variables.h"
#include "expressions.h"
#include "functions.h"
#include "io.h"
#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif


IfBlock ifBlockStack[32];
int ifBlockDepth = 0;
int skipBlock = 0;

LoopInfo loopStack[32];
int loopDepth = 0;

WhileInfo whileStack[32];
int whileDepth = 0;


int parseNumberOrVariable(const char* str) {
    char cleaned[256];
    strncpy(cleaned, str, sizeof(cleaned)-1);
    cleaned[sizeof(cleaned)-1] = 0;
    trim_whitespace(cleaned);
    
    if (isalpha((unsigned char)cleaned[0]) || cleaned[0] == '_') {
        Variable* v = findVariable(cleaned);
        if (!v) {
            fprintf(stderr, "Hata: değişken bulunamadı: %s\n", cleaned);
            exit(1);
        }
        if (v->type != TYPE_NUMBER) {
            fprintf(stderr, "Hata: %s bir sayı değil\n", cleaned);
            exit(1);
        }
        return v->numberValue;
    }
    else {
        return atoi(cleaned);
    }
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif

    if (argc < 2) {
        fprintf(stderr, "Kullanım: %s dosya.nac\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Dosya açılamadı: %s\n", argv[1]);
        return 1;
    }

    unsigned char bom[3];
    size_t read = fread(bom, 1, 3, f);
    if (!(read == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF))
        fseek(f, 0, SEEK_SET);

    char line[2048];
    int lineNumber = 0;

    while (fgets(line, sizeof(line), f)) {
        lineNumber++;
        line[strcspn(line, "\r\n")] = 0;
        
        char* l = line;
        while (*l == ' ' || *l == '\t') l++;
        
        if (l[0] == '\0') continue;
        
        char* commentPos = strchr(l, '#');
        if (commentPos) *commentPos = '\0';
        
        trim_whitespace(l);
        
        if (strncmp(l, "func ", 5) == 0) {
            char funcName[256];
            char paramStr[1024];
            
            char* parenStart = strchr(l + 5, '(');
            char* parenEnd = strchr(l + 5, ')');
            
            if (!parenStart || !parenEnd || parenStart >= parenEnd) {
                fprintf(stderr, "Hata (satır %d): geçersiz fonksiyon sözdizimi\n", lineNumber);
                continue;
            }
            
            int nameLen = parenStart - (l + 5);
            strncpy(funcName, l + 5, nameLen);
            funcName[nameLen] = 0;
            trim_whitespace(funcName);
            
            int paramLen = parenEnd - parenStart - 1;
            strncpy(paramStr, parenStart + 1, paramLen);
            paramStr[paramLen] = 0;
            
            char params[10][256];
            int paramCount = 0;
            
            if (strlen(paramStr) > 0) {
                char* token = strtok(paramStr, ",");
                while (token && paramCount < 10) {
                    strncpy(params[paramCount], token, sizeof(params[paramCount])-1);
                    params[paramCount][sizeof(params[paramCount])-1] = 0;
                    trim_whitespace(params[paramCount]);
                    paramCount++;
                    token = strtok(NULL, ",");
                }
            }
            
            long bodyPos = ftell(f);
            int bodyLine = lineNumber + 1;
            
            registerFunction(funcName, params, paramCount, bodyPos, bodyLine);
            
            int depth = 1;
            while (fgets(line, sizeof(line), f)) {
                lineNumber++;
                line[strcspn(line, "\r\n")] = 0;
                char* ll = line;
                while (*ll == ' ' || *ll == '\t') ll++;
                
                if (ll[0] == '\0') continue;
                
                char* commentPos_inner = strchr(ll, '#');
                if (commentPos_inner) *commentPos_inner = '\0';
                trim_whitespace(ll);
                
                if (strncmp(ll, "func ", 5) == 0) depth++;
                else if (strncmp(ll, "end", 3) == 0) {
                    depth--;
                    if (depth == 0) break;
                }
            }
        }
    }
    
    fseek(f, 0, SEEK_SET);
    read = fread(bom, 1, 3, f);
    if (!(read == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF))
        fseek(f, 0, SEEK_SET);
    
    lineNumber = 0;

    while (fgets(line, sizeof(line), f)) {
        lineNumber++;
        line[strcspn(line, "\r\n")] = 0;

        if (!is_valid_utf8(line)) {
            fprintf(stderr, "Hata (satır %d): geçersiz UTF-8 dizisi\n", lineNumber);
            continue;
        }

        char* commentPos = strchr(line, '#');
        if (commentPos) *commentPos = '\0';

        char* l = line;
        while (*l == ' ' || *l == '\t') l++;

        if (l[0] == '\0') continue;

        char trimmed[2048];
        strncpy(trimmed, l, sizeof(trimmed)-1);
        trimmed[sizeof(trimmed)-1] = 0;
        trim_whitespace(trimmed);

        // Skip function definitions
        if (strncmp(l, "func ", 5) == 0) {
            int depth = 1;
            while (fgets(line, sizeof(line), f)) {
                lineNumber++;
                line[strcspn(line, "\r\n")] = 0;
                char* ll = line;
                while (*ll == ' ' || *ll == '\t') ll++;
                
                if (ll[0] == '\0') continue;
                
                char* commentPos_inner = strchr(ll, '#');
                if (commentPos_inner) *commentPos_inner = '\0';
                trim_whitespace(ll);
                
                if (strncmp(ll, "func ", 5) == 0) depth++;
                else if (strncmp(ll, "end", 3) == 0) {
                    depth--;
                    if (depth == 0) break;
                }
            }
            continue;
        }

        // Function calls
        if (!skipBlock && strchr(l, '(') && strchr(l, ')')) {
            char funcName[256];
            char* parenStart = strchr(l, '(');
            int nameLen = parenStart - l;
            strncpy(funcName, l, nameLen);
            funcName[nameLen] = 0;
            trim_whitespace(funcName);
            
            if (findFunction(funcName)) {
                char* parenEnd = strchr(l, ')');
                char argStr[1024];
                int argLen = parenEnd - parenStart - 1;
                strncpy(argStr, parenStart + 1, argLen);
                argStr[argLen] = 0;
                
                char args[10][256];
                int argCount = 0;
                
                if (strlen(argStr) > 0) {
                    char* token = strtok(argStr, ",");
                    while (token && argCount < 10) {
                        strncpy(args[argCount], token, sizeof(args[argCount])-1);
                        args[argCount][sizeof(args[argCount])-1] = 0;
                        trim_whitespace(args[argCount]);
                        argCount++;
                        token = strtok(NULL, ",");
                    }
                }
                
                callFunction(funcName, args, argCount, f, &lineNumber);
                continue;
            }
        }

        // Array methods
        if (!skipBlock && strchr(l, '.') && strchr(l, '(') && strchr(l, ')')) {
            char result[1024];
            evaluateExpression(l, result, sizeof(result)); 
            continue;
        }

        // Increment/Decrement
        if (!skipBlock && strlen(trimmed) > 2 && strcmp(trimmed + strlen(trimmed) - 2, "++") == 0) {
            char varName[256];
            strncpy(varName, trimmed, strlen(trimmed) - 2);
            varName[strlen(trimmed) - 2] = 0;
            trim_whitespace(varName);
            Variable* v = findVariable(varName);
            if (!v || v->type != TYPE_NUMBER) {
                fprintf(stderr, "Hata (satır %d): sayı değişkeni bulunamadı: %s\n", lineNumber, varName);
                continue;
            }
            v->numberValue += 1;
            continue;
        } else if (!skipBlock && strlen(trimmed) > 2 && strcmp(trimmed + strlen(trimmed) - 2, "--") == 0) {
            char varName[256];
            strncpy(varName, trimmed, strlen(trimmed) - 2);
            varName[strlen(trimmed) - 2] = 0;
            trim_whitespace(varName);
            Variable* v = findVariable(varName);
            if (!v || v->type != TYPE_NUMBER) {
                fprintf(stderr, "Hata (satır %d): sayı değişkeni bulunamadı: %s\n", lineNumber, varName);
                continue;
            }
            v->numberValue -= 1;
            continue;
        }

        // Variable assignment (not var/const declaration)
        if (!skipBlock && strncmp(l, "var ", 4) != 0 && strncmp(l, "const ", 6) != 0) {
            int eqPos = find_unquoted_char_index(l, '=');
            if (eqPos >= 0) {
                char left[256], right[1024];
                strncpy(left, l, eqPos);
                left[eqPos] = 0;
                strcpy(right, l + eqPos + 1);

                trim_whitespace(left);
                trim_whitespace(right);

                if (strchr(left, '[') && strchr(left, ']')) {
                    char arrName[256];
                    int index;
                    if (sscanf(left, "%255[^[][%d]", arrName, &index) == 2) {
                        trim_whitespace(arrName);
                        char evaluated[1024];
                        evaluateExpression(right, evaluated, sizeof(evaluated));
                        setArrayElement(arrName, index, atoi(evaluated));
                        continue;
                    }
                }

                setVariable(left, right, 0);
                continue;
            }
        }

        // const declaration
        if (strncmp(l, "const ", 6) == 0 && !skipBlock) {
            char rest[1024];
            if (sscanf(l, "const %1023[^\n]", rest) != 1) {
                fprintf(stderr, "Hata (satır %d): geçersiz const sözdizimi\n", lineNumber);
                continue;
            }

            char name[256], colon[2], value[1024];
            if (sscanf(rest, "%255s %1s %1023[^\n]", name, colon, value) != 3 || strcmp(colon, "=") != 0) {
                fprintf(stderr, "Hata (satır %d): geçersiz const tanımı\n", lineNumber);
                continue;
            }

            setVariable(name, value, 1);
        } 
        // var declaration
        else if (strncmp(l, "var ", 4) == 0 && !skipBlock) {
            char rest[1024];
            if (sscanf(l, "var %1023[^\n]", rest) != 1) {
                fprintf(stderr, "Hata (satır %d): geçersiz var sözdizimi\n", lineNumber);
                continue;
            }

            char name[256], colon[2], value[1024];
            if (sscanf(rest, "%255s %1s %1023[^\n]", name, colon, value) != 3 || strcmp(colon, "=") != 0) {
                fprintf(stderr, "Hata (satır %d): geçersiz değişken tanımı\n", lineNumber);
                continue;
            }

            setVariable(name, value, 0);
        } 
        // print
        else if (strncmp(l, "print ", 6) == 0 && !skipBlock) {
            char* rest = l + 6;
            while (*rest == ' ' || *rest == '\t') rest++;

            int nextIsInput = 0;
            long pos = ftell(f);
            char peekLine[2048];
            if (fgets(peekLine, sizeof(peekLine), f)) {
                char* pl = peekLine;
                while (*pl == ' ' || *pl == '\t') pl++;
                if (strncmp(pl, "input ", 6) == 0) nextIsInput = 1;
            }
            fseek(f, pos, SEEK_SET);

            printValue(rest, !nextIsInput);
        } 
        // input
        else if (strncmp(l, "input ", 6) == 0 && !skipBlock) {
            char varName[256];
            if (sscanf(l + 6, "%255s", varName) != 1) {
                fprintf(stderr, "Hata (satır %d): geçersiz input sözdizimi\n", lineNumber);
                continue;
            }
            readInput(varName);
        } 
        // for loop
        else if (strncmp(l, "for ", 4) == 0 && !skipBlock) {
            if (loopDepth >= 32) {
                fprintf(stderr, "Hata (satır %d): maksimum döngü derinliği aşıldı\n", lineNumber);
                exit(1);
            }

            char varName[256], startStr[256], endStr[256];

            char* inPos = strstr(l + 4, " in ");
            if (!inPos) {
                fprintf(stderr, "Hata (satır %d): geçersiz for sözdizimi\n", lineNumber);
                continue;
            }

            int varLen = inPos - (l + 4);
            strncpy(varName, l + 4, varLen);
            varName[varLen] = 0;
            trim_whitespace(varName);

            char* rangeStart = inPos + 4;
            char* dotdot = strstr(rangeStart, "..");
            if (!dotdot) {
                fprintf(stderr, "Hata (satır %d): geçersiz for range sözdizimi\n", lineNumber);
                continue;
            }

            int startLen = dotdot - rangeStart;
            strncpy(startStr, rangeStart, startLen);
            startStr[startLen] = 0;

            strcpy(endStr, dotdot + 2);

            int start = parseNumberOrVariable(startStr);
            int end = parseNumberOrVariable(endStr);

            loopStack[loopDepth].startPos = ftell(f);
            loopStack[loopDepth].startLine = lineNumber + 1;
            strncpy(loopStack[loopDepth].varName, varName, sizeof(loopStack[loopDepth].varName) - 1);
            loopStack[loopDepth].varName[sizeof(loopStack[loopDepth].varName)-1] = 0;
            loopStack[loopDepth].start = start;
            loopStack[loopDepth].end = end;
            loopStack[loopDepth].current = start;
            loopDepth++;

            Variable* v = findVariable(varName);
            if (!v) {
                v = createVariable(varName);
            }
            v->type = TYPE_NUMBER;
            v->numberValue = start;
        } 
        // while loop
        else if (strncmp(l, "while ", 6) == 0 && !skipBlock) {
            if (whileDepth >= 32) {
                fprintf(stderr, "Hata (satır %d): maksimum while derinliği aşıldı\n", lineNumber);
                exit(1);
            }

            char condition[512];
            strncpy(condition, l + 6, sizeof(condition) - 1);
            condition[sizeof(condition) - 1] = 0;
            
            if (evaluateCondition(condition)) {
                whileStack[whileDepth].startPos = ftell(f);
                whileStack[whileDepth].startLine = lineNumber + 1;
                strncpy(whileStack[whileDepth].left, condition, sizeof(whileStack[whileDepth].left) - 1);
                whileStack[whileDepth].left[sizeof(whileStack[whileDepth].left)-1] = 0;
                whileDepth++;
            } else {
                int depth = 1;
                while (fgets(line, sizeof(line), f)) {
                    lineNumber++;
                    line[strcspn(line, "\r\n")] = 0;
                    char* ll = line;
                    while (*ll == ' ' || *ll == '\t') ll++;
                    
                    if (ll[0] == '\0') continue;
                    
                    char* commentPos_inner = strchr(ll, '#');
                    if (commentPos_inner) *commentPos_inner = '\0';
                    trim_whitespace(ll);
                    
                    if (strncmp(ll, "while ", 6) == 0) depth++;
                    else if (strncmp(ll, "end", 3) == 0) {
                        depth--;
                        if (depth == 0) break;
                    }
                }
            }
        } 
        // if statement
        else if (strncmp(l, "if ", 3) == 0) {
            if (ifBlockDepth >= 32) {
                fprintf(stderr, "Hata (satır %d): maksimum if derinliği aşıldı\n", lineNumber);
                exit(1);
            }

            char condition[512];
            strncpy(condition, l + 3, sizeof(condition) - 1);
            condition[sizeof(condition) - 1] = 0;

            int result = 0;
            if (skipBlock == 0) {
                result = evaluateCondition(condition);
            }
            
            ifBlockStack[ifBlockDepth].conditionResult = result;
            ifBlockStack[ifBlockDepth].hasElse = 0;
            ifBlockStack[ifBlockDepth].inElseBlock = 0;
            ifBlockDepth++;
            
            if (skipBlock > 0 || !result) {
                skipBlock++;
            }
        } 
        // else statement
        else if (strncmp(l, "else", 4) == 0) {
            if (ifBlockDepth > 0) {
                IfBlock* currentIf = &ifBlockStack[ifBlockDepth-1];
                currentIf->hasElse = 1;
                currentIf->inElseBlock = 1;
                
                if (currentIf->conditionResult) {
                    skipBlock++;
                } 
                else {
                    if (skipBlock > 0) skipBlock--;
                }
            }
        } 
        // end statement
        else if (strncmp(l, "end", 3) == 0) {
            if (whileDepth > 0 && skipBlock == 0) {
                WhileInfo* currentWhile = &whileStack[whileDepth-1];
                if (evaluateCondition(currentWhile->left)) {
                    fseek(f, currentWhile->startPos, SEEK_SET);
                    lineNumber = currentWhile->startLine - 1;
                } else {
                    whileDepth--;
                }
            }
            else if (loopDepth > 0 && skipBlock == 0) {
                LoopInfo* currentLoop = &loopStack[loopDepth-1];
                currentLoop->current++;
                
                if (currentLoop->current <= currentLoop->end) {
                    Variable* v = findVariable(currentLoop->varName);
                    if (v) v->numberValue = currentLoop->current;
                    
                    fseek(f, currentLoop->startPos, SEEK_SET);
                    lineNumber = currentLoop->startLine - 1;
                } else {
                    loopDepth--;
                }
            }
            else if (ifBlockDepth > 0) {
                if (skipBlock > 0) {
                    skipBlock--;
                }
                
                ifBlockDepth--;
            }
        }
    }

    fclose(f);
    return 0;
}
