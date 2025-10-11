#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

typedef enum { TYPE_NUMBER, TYPE_STRING } VarType;

typedef struct {
    char name[256];
    VarType type;
    int numberValue;
    char stringValue[1024];
} Variable;

Variable vars[100];
int varCount = 0;

typedef struct {
    int skipBlock;
    int ifResult;
} IfState;

IfState ifStack[32];
int ifStackTop = 0;

// ---------------- UTF-8 Yardımcı Fonksiyonlar ----------------
int utf8_strlen(const char* s) {
    int count = 0;
    while (*s) {
        if ((*s & 0xC0) != 0x80) count++;
        s++;
    }
    return count;
}

int is_valid_utf8(const char* s) {
    while (*s) {
        if ((*s & 0x80) == 0) s++;
        else if ((*s & 0xE0) == 0xC0) { if ((s[1] & 0xC0) != 0x80) return 0; s+=2; }
        else if ((*s & 0xF0) == 0xE0) { if ((s[1]&0xC0)!=0x80 || (s[2]&0xC0)!=0x80) return 0; s+=3; }
        else if ((*s & 0xF8) == 0xF0) { if ((s[1]&0xC0)!=0x80 || (s[2]&0xC0)!=0x80 || (s[3]&0xC0)!=0x80) return 0; s+=4; }
        else return 0;
    }
    return 1;
}

// ---------------- Değişken işlemleri ----------------
Variable* findVariable(const char* name) {
    for (int i = 0; i < varCount; i++)
        if (strcmp(vars[i].name, name) == 0)
            return &vars[i];
    return NULL;
}

void stripQuotes(char* s) {
    int len = strlen(s);
    if (len >= 2 && s[0]=='"' && s[len-1]=='"') {
        memmove(s, s+1, len-2);
        s[len-2]=0;
    }
}

// ---------------- İfade Değerlendirme ----------------
void evaluateExpression(const char* expr, char* result, size_t resultSize) {
    result[0] = 0;
    int i = 0;
    size_t currentLen = 0;
    
    while (expr[i]) {
        while (expr[i] && isspace((unsigned char)expr[i])) i++;

        if (expr[i] == '"') {
            i++;
            char tmp[1024];
            int j = 0;
            while (expr[i] && expr[i] != '"' && j < sizeof(tmp)-1) {
                if (expr[i] == '\\') {
                    i++;
                    switch (expr[i]) {
                        case 'n': tmp[j++] = '\n'; break;
                        case 't': tmp[j++] = '\t'; break;
                        case '\\': tmp[j++] = '\\'; break;
                        case '"': tmp[j++] = '"'; break;
                        default: tmp[j++] = expr[i]; break;
                    }
                    i++;
                } else {
                    tmp[j++] = expr[i++];
                }
            }
            tmp[j] = 0;
            
            size_t remaining = resultSize - currentLen - 1;
            if (remaining > 0) {
                size_t written = snprintf(result + currentLen, remaining + 1, "%s", tmp);
                if (written < remaining) currentLen += written;
                else currentLen = resultSize - 1;
            }
            
            if (expr[i] == '"') i++;
        }
        else if (isalpha((unsigned char)expr[i]) || expr[i]=='_') {
            char varname[256]; int j=0;
            while ((isalnum((unsigned char)expr[i]) || expr[i]=='_') && j<sizeof(varname)-1)
                varname[j++]=expr[i++];
            varname[j]=0;
            Variable* v = findVariable(varname);
            
            if (!v) { 
                fprintf(stderr, "Hata: değişken bulunamadı: %s\n", varname); 
                exit(1); 
            }
            
            size_t remaining = resultSize - currentLen - 1;
            if (remaining > 0) {
                if (v->type==TYPE_STRING) {
                    size_t written = snprintf(result + currentLen, remaining + 1, "%s", v->stringValue);
                    if (written < remaining) currentLen += written;
                    else currentLen = resultSize - 1;
                } else {
                    size_t written = snprintf(result + currentLen, remaining + 1, "%d", v->numberValue);
                    if (written < remaining) currentLen += written;
                    else currentLen = resultSize - 1;
                }
            }
        }
        else if (isdigit((unsigned char)expr[i]) || (expr[i]=='-' && isdigit((unsigned char)expr[i+1]))) {
            char num[64]; int j=0;
            while ((isdigit((unsigned char)expr[i]) || expr[i]=='-') && j<sizeof(num)-1)
                num[j++]=expr[i++];
            num[j]=0;
            
            size_t remaining = resultSize - currentLen - 1;
            if (remaining > 0) {
                size_t written = snprintf(result + currentLen, remaining + 1, "%s", num);
                if (written < remaining) currentLen += written;
                else currentLen = resultSize - 1;
            }
        }
        else if (expr[i] == '+') i++;
        else i++;
    }
}

void setVariable(const char* name, const char* value) {
    Variable* v = findVariable(name);
    if (!v) {
        if (varCount >= 100) { 
            fprintf(stderr, "Hata: maksimum değişken sayısına ulaşıldı (100)\n"); 
            exit(1); 
        }
        v = &vars[varCount++];
        strncpy(v->name,name,sizeof(v->name)-1);
        v->name[sizeof(v->name)-1]=0;
    }

    if (strchr(value,'+')!=NULL || value[0]=='"') {
        v->type = TYPE_STRING;
        evaluateExpression(value, v->stringValue, sizeof(v->stringValue));
    }
    else if (isdigit((unsigned char)value[0]) || (value[0]=='-' && isdigit((unsigned char)value[1]))) {
        v->type = TYPE_NUMBER;
        v->numberValue = atoi(value);
    }
    else {
        Variable* src = findVariable(value);
        if (!src) { 
            fprintf(stderr, "Hata: değişken bulunamadı: %s\n", value); 
            exit(1); 
        }
        *v = *src;
        strncpy(v->name,name,sizeof(v->name)-1);
    }
}

// ---------------- Print işlemi ----------------
void printValue(const char* expr, int newline) {
    char result[2048];
    evaluateExpression(expr, result, sizeof(result));
    if (newline)
        printf("%s\n", result);
    else
        printf("%s", result);
}

// ---------------- Input işlemi ----------------
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
            fprintf(stderr, "Hata: maksimum değişken sayısına ulaşıldı (100)\n");
            exit(1);
        }
        v = &vars[varCount++];
        strncpy(v->name, varName, sizeof(v->name)-1);
        v->name[sizeof(v->name)-1] = 0;
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

// ---------------- Koşul değerlendirme ----------------
int evaluateCondition(const char* left, const char* op, const char* right) {
    Variable* lvar = findVariable(left);
    Variable* rvar = findVariable(right);
    int lnum, rnum;
    
    if (lvar) { 
        if (lvar->type==TYPE_NUMBER) 
            lnum=lvar->numberValue; 
        else { 
            fprintf(stderr, "Hata: string ile karşılaştırma desteklenmiyor\n"); 
            exit(1); 
        } 
    } else {
        lnum=atoi(left);
    }
    
    if (rvar) { 
        if (rvar->type==TYPE_NUMBER) 
            rnum=rvar->numberValue; 
        else { 
            fprintf(stderr, "Hata: string ile karşılaştırma desteklenmiyor\n"); 
            exit(1); 
        } 
    } else {
        rnum=atoi(right);
    }
    
    if (strcmp(op,"=")==0 || strcmp(op,"==")==0) return lnum==rnum;
    if (strcmp(op,"!=")==0) return lnum!=rnum;
    if (strcmp(op,">")==0) return lnum>rnum;
    if (strcmp(op,"<")==0) return lnum<rnum;
    if (strcmp(op,">=")==0) return lnum>=rnum;
    if (strcmp(op,"<=")==0) return lnum<=rnum;
    
    fprintf(stderr, "Hata: bilinmeyen karşılaştırma operatörü: %s\n", op);
    exit(1);
}

// ---------------- Main ----------------
int main(int argc, char* argv[]) {
    setlocale(LC_ALL,"");

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _setmode(_fileno(stdout),_O_BINARY);
    _setmode(_fileno(stdin),_O_BINARY);
    HANDLE hOut=GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode=0;
    GetConsoleMode(hOut,&dwMode);
    dwMode|=ENABLE_PROCESSED_OUTPUT|ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut,dwMode);
#endif

    if (argc<2) { 
        fprintf(stderr, "Kullanım: %s dosya.nac\n", argv[0]); 
        return 1; 
    }

    FILE* f=fopen(argv[1],"rb");
    if (!f) { 
        fprintf(stderr, "Dosya açılamadı: %s\n", argv[1]); 
        return 1; 
    }

    unsigned char bom[3];
    size_t read=fread(bom,1,3,f);
    if (!(read==3 && bom[0]==0xEF && bom[1]==0xBB && bom[2]==0xBF)) fseek(f,0,SEEK_SET);

    char line[2048];
    int lineNumber = 0;
    int skipBlock=0,inIf=0,ifResult=0;

    while (fgets(line,sizeof(line),f)) {
        lineNumber++;
        line[strcspn(line,"\r\n")]=0;
        
        if (!is_valid_utf8(line)) { 
            fprintf(stderr, "Hata (satır %d): geçersiz UTF-8 dizisi\n", lineNumber); 
            continue; 
        }

        char* l=line; while (*l==' '||*l=='\t') l++;

        if (strncmp(l,"var ",4)==0 && !skipBlock) {
            char rest[1024]; 
            if (sscanf(l,"var %1023[^\n]",rest) != 1) {
                fprintf(stderr, "Hata (satır %d): geçersiz var sözdizimi\n", lineNumber);
                continue;
            }
            
            char name[256],colon[2],value[1024];
            if (sscanf(rest,"%255s %1s %1023[^\n]",name,colon,value) != 3) {
                fprintf(stderr, "Hata (satır %d): geçersiz değişken tanımı\n", lineNumber);
                continue;
            }
            
            setVariable(name,value);
        }
        else if (strncmp(l,"print ",6)==0 && !skipBlock) {
            char* rest=l+6; while (*rest==' '||*rest=='\t') rest++;

            int nextIsInput = 0;
            long pos = ftell(f);
            char peekLine[2048];
            if (fgets(peekLine,sizeof(peekLine),f)) {
                char* pl = peekLine; while (*pl==' '||*pl=='\t') pl++;
                if (strncmp(pl,"input ",6)==0) nextIsInput=1;
            }
            fseek(f,pos,SEEK_SET); 

            printValue(rest, !nextIsInput);
        }
        else if (strncmp(l,"input ",6)==0 && !skipBlock) {
            char varName[256];
            if (sscanf(l+6, "%255s", varName) != 1) {
                fprintf(stderr, "Hata (satır %d): geçersiz oku sözdizimi\n", lineNumber);
                continue;
            }
            readInput(varName);
        }
        else if (strncmp(l,"if ",3)==0) {
            char left[256],op[3],right[256]; 
            if (sscanf(l+3,"%255s %2s %255s",left,op,right) != 3) {
                fprintf(stderr, "Hata (satır %d): geçersiz if sözdizimi\n", lineNumber);
                continue;
            }
            
            ifResult=evaluateCondition(left,op,right);
            inIf=1; 
            skipBlock=!ifResult;
        }
        else if (strncmp(l,"else",4)==0) {
            if (inIf) { 
                skipBlock=ifResult; 
                inIf=0; 
            }
        }
        else if (inIf && !skipBlock) {
            inIf = 0;
        }
    }

    fclose(f);
    return 0;
}