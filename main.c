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

// ---------------- UTF-8 Yardımcı Fonksiyonlar ----------------

// UTF-8 karakter sayısını hesapla (byte değil, gerçek karakter sayısı)
int utf8_strlen(const char* s) {
    int count = 0;
    while (*s) {
        if ((*s & 0xC0) != 0x80) count++;
        s++;
    }
    return count;
}

// UTF-8 string'in geçerli olup olmadığını kontrol et
int is_valid_utf8(const char* s) {
    while (*s) {
        if ((*s & 0x80) == 0) {
            // 1-byte karakter (ASCII)
            s++;
        } else if ((*s & 0xE0) == 0xC0) {
            // 2-byte karakter
            if ((s[1] & 0xC0) != 0x80) return 0;
            s += 2;
        } else if ((*s & 0xF0) == 0xE0) {
            // 3-byte karakter
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
            s += 3;
        } else if ((*s & 0xF8) == 0xF0) {
            // 4-byte karakter
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
            s += 4;
        } else {
            return 0;
        }
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

void setVariable(const char* name, const char* value) {
    Variable* v = findVariable(name);
    if (!v) {
        if (varCount >= 100) {
            printf("Hata: maksimum değişken sayısına ulaşıldı\n");
            exit(1);
        }
        v = &vars[varCount++];
        strncpy(v->name, name, sizeof(v->name)-1);
        v->name[sizeof(v->name)-1] = 0;
    }

    if (value[0]=='"') {
        v->type = TYPE_STRING;
        char temp[1024];
        strncpy(temp, value, sizeof(temp)-1);
        temp[sizeof(temp)-1] = 0;
        stripQuotes(temp);
        
        // UTF-8 geçerliliğini kontrol et
        if (!is_valid_utf8(temp)) {
            printf("Uyarı: geçersiz UTF-8 dizisi\n");
        }
        
        strncpy(v->stringValue, temp, sizeof(v->stringValue)-1);
        v->stringValue[sizeof(v->stringValue)-1]=0;
    }
    else if (isdigit((unsigned char)value[0]) || (value[0]=='-' && isdigit((unsigned char)value[1]))) {
        v->type = TYPE_NUMBER;
        v->numberValue = atoi(value);
    }
    else {
        Variable* src = findVariable(value);
        if (!src) { 
            printf("Hata: değişken '%s' bulunamadı\n", value); 
            exit(1); 
        }
        v->type = src->type;
        if (v->type==TYPE_NUMBER) 
            v->numberValue = src->numberValue;
        else {
            strncpy(v->stringValue, src->stringValue, sizeof(v->stringValue)-1);
            v->stringValue[sizeof(v->stringValue)-1] = 0;
        }
    }
}

// ---------------- Print işlemi ----------------
void printValue(const char* val, const char* op, const char* second) {
    Variable* v = findVariable(val);

    if (!v) {
        if (val[0]=='"') {
            char tmp[1024];
            strncpy(tmp, val, sizeof(tmp)-1);
            tmp[sizeof(tmp)-1] = 0;
            stripQuotes(tmp);
            printf("%s\n", tmp);
            return;
        }
        printf("Hata: değişken '%s' bulunamadı\n", val);
        exit(1);
    }

    if (v->type==TYPE_NUMBER) {
        int n = v->numberValue;
        if (op) {
            if (strcmp(op,"*")==0) n *= atoi(second);
            else if (strcmp(op,"/")==0) {
                int divisor = atoi(second);
                if (divisor == 0) {
                    printf("Hata: sıfıra bölme hatası\n");
                    exit(1);
                }
                n /= divisor;
            }
            else if (strcmp(op,"+")==0) n += atoi(second);
            else if (strcmp(op,"-")==0) n -= atoi(second);
        }
        printf("%d\n", n);
    } else {
        printf("%s\n", v->stringValue);
    }
}

// ---------------- Koşul değerlendirme ----------------
int evaluateCondition(const char* left, const char* op, const char* right) {
    Variable* lvar = findVariable(left);
    Variable* rvar = findVariable(right);
    int lnum, rnum;

    if (lvar) {
        if (lvar->type == TYPE_NUMBER) 
            lnum = lvar->numberValue;
        else { 
            printf("Hata: string ile karşılaştırma desteklenmiyor.\n"); 
            exit(1); 
        }
    } else {
        lnum = atoi(left);
    }

    if (rvar) {
        if (rvar->type == TYPE_NUMBER) 
            rnum = rvar->numberValue;
        else { 
            printf("Hata: string ile karşılaştırma desteklenmiyor.\n"); 
            exit(1); 
        }
    } else {
        rnum = atoi(right);
    }

    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) return lnum == rnum;
    if (strcmp(op, "!") == 0 || strcmp(op, "!=") == 0) return lnum != rnum;
    if (strcmp(op, ">")  == 0) return lnum >  rnum;
    if (strcmp(op, "<")  == 0) return lnum <  rnum;
    if (strcmp(op, ">=") == 0) return lnum >= rnum;
    if (strcmp(op, "<=") == 0) return lnum <= rnum;

    printf("Hata: bilinmeyen karşılaştırma operatörü '%s'\n", op);
    exit(1);
}

// ---------------- Main ----------------
int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");

    #ifdef _WIN32
    // Windows için UTF-8 konsol desteği
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // stdout'u binary modda UTF-8 için ayarla
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    
    // Virtual terminal işleme desteği
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    #endif

    if (argc < 2) {
        printf("Kullanım: %s dosya.nac\n", argv[0]);
        return 1;
    }

    // UTF-8 modunda dosya aç
    FILE* f = fopen(argv[1], "rb");
    
    if (!f) {
        printf("Dosya açılamadı: %s\n", argv[1]);
        return 1;
    }

    unsigned char bom[3];
    size_t read = fread(bom, 1, 3, f);
    if (read == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        // BOM var, atla
    } else {
        // BOM yok, başa dön
        fseek(f, 0, SEEK_SET);
    }

    char line[2048];
    int skipBlock = 0;
    int inIf = 0;
    int ifResult = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        
        if (!is_valid_utf8(line)) {
            printf("Uyarı: satırda geçersiz UTF-8 dizisi\n");
            continue;
        }
        
        char *l = line; 
        while (*l == ' ' || *l == '\t') l++;

        if (strncmp(l, "var ", 4) == 0 && !skipBlock) {
            char rest[1024]; 
            sscanf(l, "var %[^\n]", rest);
            char name[256], colon[2], value[1024];
            sscanf(rest, "%s %s %[^\n]", name, colon, value);
            setVariable(name, value);
        }
        else if (strncmp(l, "print ", 6) == 0 && !skipBlock) {
            char *rest = l + 6;
            while (*rest == ' ' || *rest == '\t') rest++;
            
            if (*rest == '"') {
                rest++;
                char tmp[1024];
                int i = 0;
                while (rest[i] != '"' && rest[i] != 0 && i < sizeof(tmp) - 1) {
                    tmp[i] = rest[i];
                    i++;
                }
                tmp[i] = 0;
                printf("%s\n", tmp);
            } else {
                char val[256], op[3], second[256];
                int n = sscanf(rest, "%s %s %s", val, op, second);
                if (n == 3) 
                    printValue(val, op, second);
                else 
                    printValue(rest, NULL, NULL);
            }
        }
        else if (strncmp(l, "if ", 3) == 0) {
            char left[256], op[3], right[256];
            sscanf(l + 3, "%s %s %s", left, op, right);
            ifResult = evaluateCondition(left, op, right);
            inIf = 1;
            skipBlock = !ifResult;
        }
        else if (strncmp(l, "else", 4) == 0) {
            if (inIf) {
                skipBlock = ifResult;
                inIf = 0;
            }
        }
    }

    fclose(f);
    return 0;
}