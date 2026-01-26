/*
 * NaC Language Interpreter
 * -------------------------
 * Sembol ağırlıklı, minimal, C-benzeri bir yorumlanan dil.
 * 
 * Derleme: gcc -o nac nac.c
 * Kullanım: ./nac program.nac
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/* ==================== SABITLER ==================== */
#define MAX_VARS 26
#define MAX_FUNCS 100
#define MAX_PARAMS 10
#define MAX_CALL_DEPTH 100
#define MAX_TOKEN_LEN 256

/* ==================== TOKEN TİPLERİ ==================== */
typedef enum {
    TOK_EOF,
    TOK_NUMBER,      // #123
    TOK_VAR,         // $x
    TOK_PLUS,        // +
    TOK_MINUS,       // -
    TOK_STAR,        // *
    TOK_SLASH,       // /
    TOK_EQ,          // ==
    TOK_NEQ,         // !=
    TOK_LT,          // <
    TOK_GT,          // >
    TOK_LTE,         // <=
    TOK_GTE,         // >=
    TOK_AND,         // &&
    TOK_OR,          // ||
    TOK_NOT,         // !
    TOK_ASSIGN,      // =
    TOK_SEMI,        // ;
    TOK_COMMA,       // ,
    TOK_LPAREN,      // (
    TOK_RPAREN,      // )
    TOK_LBRACE,      // {
    TOK_RBRACE,      // }
    TOK_QUESTION,    // ? (if)
    TOK_COLON,       // : (else)
    TOK_AT,          // @ (for)
    TOK_PLUSPLUS,    // ++
    TOK_MINUSMINUS,  // --
    TOK_FN,          // fn
    TOK_RN,          // rn (return)
    TOK_IN,          // in
    TOK_OUT,         // out
    TOK_BREAK,       // break
    TOK_CONTINUE     // continue
} TokenType;

/* ==================== YAPILAR ==================== */
typedef struct {
    TokenType type;
    int value;
    char name[MAX_TOKEN_LEN];
} Token;

typedef struct {
    char name[MAX_TOKEN_LEN];
    char params[MAX_PARAMS];
    int param_count;
    int body_start;
    int body_end;
} Function;

typedef struct {
    int vars[MAX_VARS];
    bool var_defined[MAX_VARS];
} Scope;

/* ==================== GLOBAL DEĞİŞKENLER ==================== */
static char *code = NULL;
static int pos = 0;
static int code_len = 0;
static Token current_token;

static Scope global_scope;
static Scope *call_stack[MAX_CALL_DEPTH];
static int call_depth = 0;

static Function functions[MAX_FUNCS];
static int func_count = 0;

static bool should_break = false;
static bool should_continue = false;
static bool should_return = false;
static int return_value = 0;

/* ==================== İLERİ BİLDİRİMLER ==================== */
static void next_token(void);
static int parse_expression(void);
static void parse_statement(void);
static void parse_block(void);

/* ==================== HATA YÖNETİMİ ==================== */
static void error(const char *msg) {
    // Kodun başına gidip hata anına kadar kaç tane \n geçtiğini sayar
    int line = 1;
    for (int i = 0; i < pos; i++) {
        if (code[i] == '\n') line++;
    }
    fprintf(stderr, "Hata (Satir %d, Pozisyon %d): %s\n", line, pos, msg);
    exit(1);
}

/* ==================== LEXER ==================== */
static void skip_whitespace_and_comments(void) {
    while (pos < code_len) {
        // Boşluk atla
        while (pos < code_len && isspace(code[pos])) {
            pos++;
        }
        // Yorum satırı atla
        if (pos < code_len - 1 && code[pos] == '/' && code[pos + 1] == '/') {
            while (pos < code_len && code[pos] != '\n') {
                pos++;
            }
            continue;
        }
        break;
    }
}

static void next_token(void) {
    skip_whitespace_and_comments();
    
    if (pos >= code_len) {
        current_token.type = TOK_EOF;
        return;
    }
    
    char c = code[pos];
    
    // Sayı: #123 veya #-123
    if (c == '#') {
        pos++;
        int value = 0;
        int sign = 1;
        if (pos < code_len && code[pos] == '-') {
            sign = -1;
            pos++;
        }
        while (pos < code_len && isdigit(code[pos])) {
            value = value * 10 + (code[pos] - '0');
            pos++;
        }
        current_token.type = TOK_NUMBER;
        current_token.value = sign * value;
        return;
    }
    
    // Değişken: $x
    if (c == '$') {
        pos++;
        if (pos < code_len && isalpha(code[pos])) {
            char var = tolower(code[pos]);
            pos++;
            current_token.type = TOK_VAR;
            current_token.value = var - 'a';
            current_token.name[0] = var;
            current_token.name[1] = '\0';
            return;
        }
        error("Degisken adi bekleniyor");
    }
    
    // Anahtar kelimeler
    if (strncmp(&code[pos], "fn", 2) == 0 && !isalnum(code[pos + 2])) {
        pos += 2;
        current_token.type = TOK_FN;
        return;
    }
    if (strncmp(&code[pos], "rn", 2) == 0 && !isalnum(code[pos + 2])) {
        pos += 2;
        current_token.type = TOK_RN;
        return;
    }
    if (strncmp(&code[pos], "in", 2) == 0 && !isalnum(code[pos + 2])) {
        pos += 2;
        current_token.type = TOK_IN;
        return;
    }
    if (strncmp(&code[pos], "out", 3) == 0 && !isalnum(code[pos + 3])) {
        pos += 3;
        current_token.type = TOK_OUT;
        return;
    }
    if (strncmp(&code[pos], "break", 5) == 0 && !isalnum(code[pos + 5])) {
        pos += 5;
        current_token.type = TOK_BREAK;
        return;
    }
    if (strncmp(&code[pos], "continue", 8) == 0 && !isalnum(code[pos + 8])) {
        pos += 8;
        current_token.type = TOK_CONTINUE;
        return;
    }
    
    // Çift karakterli operatörler
    if (pos < code_len - 1) {
        char c2 = code[pos + 1];
        if (c == '=' && c2 == '=') { pos += 2; current_token.type = TOK_EQ; return; }
        if (c == '!' && c2 == '=') { pos += 2; current_token.type = TOK_NEQ; return; }
        if (c == '<' && c2 == '=') { pos += 2; current_token.type = TOK_LTE; return; }
        if (c == '>' && c2 == '=') { pos += 2; current_token.type = TOK_GTE; return; }
        if (c == '&' && c2 == '&') { pos += 2; current_token.type = TOK_AND; return; }
        if (c == '|' && c2 == '|') { pos += 2; current_token.type = TOK_OR; return; }
        if (c == '+' && c2 == '+') { pos += 2; current_token.type = TOK_PLUSPLUS; return; }
        if (c == '-' && c2 == '-') { pos += 2; current_token.type = TOK_MINUSMINUS; return; }
    }
    
    // Tek karakterli operatörler
    pos++;
    switch (c) {
        case '+': current_token.type = TOK_PLUS; return;
        case '-': current_token.type = TOK_MINUS; return;
        case '*': current_token.type = TOK_STAR; return;
        case '/': current_token.type = TOK_SLASH; return;
        case '<': current_token.type = TOK_LT; return;
        case '>': current_token.type = TOK_GT; return;
        case '!': current_token.type = TOK_NOT; return;
        case '=': current_token.type = TOK_ASSIGN; return;
        case ';': current_token.type = TOK_SEMI; return;
        case ',': current_token.type = TOK_COMMA; return;
        case '(': current_token.type = TOK_LPAREN; return;
        case ')': current_token.type = TOK_RPAREN; return;
        case '{': current_token.type = TOK_LBRACE; return;
        case '}': current_token.type = TOK_RBRACE; return;
        case '?': current_token.type = TOK_QUESTION; return;
        case ':': current_token.type = TOK_COLON; return;
        case '@': current_token.type = TOK_AT; return;
        default:
            fprintf(stderr, "Bilinmeyen karakter: '%c' (ASCII: %d)\n", c, (int)c);
            error("Bilinmeyen karakter");
    }
}

static void expect(TokenType type) {
    if (current_token.type != type) {
        fprintf(stderr, "Beklenen token: %d, bulunan: %d\n", type, current_token.type);
        error("Beklenmeyen token");
    }
    next_token();
}

/* ==================== SCOPE YÖNETİMİ ==================== */
static Scope* current_scope(void) {
    if (call_depth > 0) {
        return call_stack[call_depth - 1];
    }
    return &global_scope;
}

static int get_var(int idx) {
    Scope *scope = current_scope();
    if (!scope->var_defined[idx]) {
        if (call_depth > 0 && global_scope.var_defined[idx]) {
            return global_scope.vars[idx];
        }
        return 0;
    }
    return scope->vars[idx];
}

static void set_var(int idx, int value) {
    Scope *scope = current_scope();
    scope->vars[idx] = value;
    scope->var_defined[idx] = true;
}

/* ==================== FONKSİYON YÖNETİMİ ==================== */
static Function* find_function(const char *name) {
    for (int i = 0; i < func_count; i++) {
        if (strcmp(functions[i].name, name) == 0) {
            return &functions[i];
        }
    }
    return NULL;
}

static int call_function(Function *func, int *args, int arg_count) {
    if (arg_count != func->param_count) {
        error("Yanlis parametre sayisi");
    }
    
    if (call_depth >= MAX_CALL_DEPTH) {
        error("Cagri yigini tasmasi");
    }
    
    // Yeni scope
    Scope *new_scope = (Scope*)malloc(sizeof(Scope));
    memset(new_scope, 0, sizeof(Scope));
    
    // Parametreleri ayarla
    for (int i = 0; i < arg_count; i++) {
        int idx = func->params[i] - 'a';
        new_scope->vars[idx] = args[i];
        new_scope->var_defined[idx] = true;
    }
    
    call_stack[call_depth++] = new_scope;
    
    // Durumu kaydet
    int old_pos = pos;
    
    // Fonksiyon gövdesine git
    pos = func->body_start;
    
    should_return = false;
    return_value = 0;
    
    next_token();
    while (pos < func->body_end && !should_return) {
        parse_statement();
        if (current_token.type == TOK_EOF) break;
    }
    
    // Durumu geri yükle
    pos = old_pos;
    
    // Scope'u temizle
    free(call_stack[--call_depth]);
    
    should_return = false;
    int ret = return_value;
    return_value = 0;
    
    next_token();
    return ret;
}

/* ==================== PARSER - İFADE ==================== */
static int parse_primary(void) {
    if (current_token.type == TOK_NUMBER) {
        int val = current_token.value;
        next_token();
        return val;
    }
    
    if (current_token.type == TOK_VAR) {
        int idx = current_token.value;
        char name[2] = { (char)('a' + idx), '\0' };
        next_token();
        
        // Fonksiyon çağrısı?
        if (current_token.type == TOK_LPAREN) {
            Function *func = find_function(name);
            if (!func) {
                fprintf(stderr, "Tanimsiz fonksiyon: $%s\n", name);
                error("Tanimsiz fonksiyon");
            }
            
            next_token(); // (
            int args[MAX_PARAMS];
            int arg_count = 0;
            
            if (current_token.type != TOK_RPAREN) {
                args[arg_count++] = parse_expression();
                while (current_token.type == TOK_COMMA) {
                    next_token();
                    args[arg_count++] = parse_expression();
                }
            }
            expect(TOK_RPAREN);
            
            return call_function(func, args, arg_count);
        }
        
        return get_var(idx);
    }
    
    if (current_token.type == TOK_IN) {
        next_token();
        int val;
        printf("> ");
        fflush(stdout);
        if (scanf("%d", &val) != 1) {
            error("Giris hatasi");
        }
        return val;
    }
    
    if (current_token.type == TOK_LPAREN) {
        next_token();
        int val = parse_expression();
        expect(TOK_RPAREN);
        return val;
    }
    
    if (current_token.type == TOK_NOT) {
        next_token();
        return !parse_primary();
    }
    
    if (current_token.type == TOK_MINUS) {
        next_token();
        return -parse_primary();
    }
    
    error("Ifade bekleniyor");
    return 0;
}

static int parse_term(void) {
    int left = parse_primary();
    
    while (current_token.type == TOK_STAR || current_token.type == TOK_SLASH) {
        TokenType op = current_token.type;
        next_token();
        int right = parse_primary();
        
        if (op == TOK_STAR) left = left * right;
        else {
            if (right == 0) error("Sifira bolme hatasi");
            left = left / right;
        }
    }
    
    return left;
}

static int parse_additive(void) {
    int left = parse_term();
    
    while (current_token.type == TOK_PLUS || current_token.type == TOK_MINUS) {
        TokenType op = current_token.type;
        next_token();
        int right = parse_term();
        
        if (op == TOK_PLUS) left = left + right;
        else left = left - right;
    }
    
    return left;
}

static int parse_comparison(void) {
    int left = parse_additive();
    
    while (current_token.type == TOK_LT || current_token.type == TOK_GT ||
           current_token.type == TOK_LTE || current_token.type == TOK_GTE) {
        TokenType op = current_token.type;
        next_token();
        int right = parse_additive();
        
        switch (op) {
            case TOK_LT: left = left < right; break;
            case TOK_GT: left = left > right; break;
            case TOK_LTE: left = left <= right; break;
            case TOK_GTE: left = left >= right; break;
            default: break;
        }
    }
    
    return left;
}

static int parse_equality(void) {
    int left = parse_comparison();
    
    while (current_token.type == TOK_EQ || current_token.type == TOK_NEQ) {
        TokenType op = current_token.type;
        next_token();
        int right = parse_comparison();
        
        if (op == TOK_EQ) left = (left == right);
        else left = (left != right);
    }
    
    return left;
}

static int parse_and(void) {
    int left = parse_equality();
    
    while (current_token.type == TOK_AND) {
        next_token();
        int right = parse_equality();
        left = left && right;
    }
    
    return left;
}

static int parse_or(void) {
    int left = parse_and();
    
    while (current_token.type == TOK_OR) {
        next_token();
        int right = parse_and();
        left = left || right;
    }
    
    return left;
}

static int parse_expression(void) {
    return parse_or();
}

/* ==================== PARSER - KOMUT ==================== */

// Blok atla - sadece pozisyonu ilerletir
static void skip_block(void) {
    expect(TOK_LBRACE);
    int depth = 1;
    while (depth > 0 && current_token.type != TOK_EOF) {
        if (current_token.type == TOK_LBRACE) depth++;
        else if (current_token.type == TOK_RBRACE) depth--;
        if (depth > 0) next_token();
    }
    expect(TOK_RBRACE);
}

// Blok çözümle ve çalıştır
static void parse_block(void) {
    expect(TOK_LBRACE);
    
    while (current_token.type != TOK_RBRACE && current_token.type != TOK_EOF) {
        if (should_break || should_continue || should_return) break;
        parse_statement();
    }
    
    // Kalan komutları atla (break/continue durumunda)
    while (current_token.type != TOK_RBRACE && current_token.type != TOK_EOF) {
        if (current_token.type == TOK_LBRACE) {
            skip_block();
        } else {
            next_token();
        }
    }
    
    expect(TOK_RBRACE);
}

// Fonksiyon tanımı
static void parse_function_def(void) {
    expect(TOK_FN);
    
    if (current_token.type != TOK_VAR) {
        error("Fonksiyon adi bekleniyor");
    }
    
    Function *func = &functions[func_count++];
    func->name[0] = 'a' + current_token.value;
    func->name[1] = '\0';
    func->param_count = 0;
    
    next_token();
    expect(TOK_LPAREN);
    
    // Parametreleri oku
    if (current_token.type == TOK_VAR) {
        func->params[func->param_count++] = 'a' + current_token.value;
        next_token();
        
        while (current_token.type == TOK_COMMA) {
            next_token();
            if (current_token.type != TOK_VAR) {
                error("Parametre bekleniyor");
            }
            func->params[func->param_count++] = 'a' + current_token.value;
            next_token();
        }
    }
    
    expect(TOK_RPAREN);
    
    // Gövde başlangıcını kaydet
    expect(TOK_LBRACE);
    func->body_start = pos;
    
    // Gövdeyi atla
    int depth = 1;
    while (depth > 0 && pos < code_len) {
        if (code[pos] == '{') depth++;
        else if (code[pos] == '}') depth--;
        if (depth > 0) pos++;
    }
    func->body_end = pos;
    pos++; // } atla
    
    next_token();
    expect(TOK_SEMI);
}

// If-else: ?( KOŞUL ){ ... }:{ ... };
static void parse_if(void) {
    // TOK_QUESTION zaten okundu, next_token ile devam et
    next_token();
    expect(TOK_LPAREN);
    
    int condition = parse_expression();
    
    expect(TOK_RPAREN);
    
    if (condition) {
        // if bloğunu çalıştır
        parse_block();
        
        // else varsa atla
        if (current_token.type == TOK_COLON) {
            next_token();
            skip_block();
        }
    } else {
        // if bloğunu atla
        skip_block();
        
        // else varsa çalıştır
        if (current_token.type == TOK_COLON) {
            next_token();
            parse_block();
        }
    }
    
    expect(TOK_SEMI);
}

// For: @( INIT ; COND ; INC ){ ... };
// Ornek: @( $i = #0 ; $i < #5 ; $i++ ){ out($i); };
static void parse_for(void) {
    // TOK_AT zaten okundu
    next_token(); // ( oku
    
    if (current_token.type != TOK_LPAREN) {
        error("For dongusunde '(' bekleniyor");
    }
    
    // Pozisyonlari karakter bazli bul
    skip_whitespace_and_comments();
    int init_pos = pos;
    
    // INIT'i atla, ilk ; bul
    int depth = 0;
    while (pos < code_len) {
        if (code[pos] == '(') depth++;
        else if (code[pos] == ')') {
            if (depth == 0) break;
            depth--;
        }
        else if (code[pos] == ';' && depth == 0) break;
        pos++;
    }
    if (code[pos] != ';') {
        error("For dongusunde ilk ';' bekleniyor");
    }
    pos++; // ; atla
    
    // COND pozisyonunu kaydet
    skip_whitespace_and_comments();
    int cond_pos = pos;
    
    // COND'u atla, ikinci ; bul
    depth = 0;
    while (pos < code_len) {
        if (code[pos] == '(') depth++;
        else if (code[pos] == ')') {
            if (depth == 0) break;
            depth--;
        }
        else if (code[pos] == ';' && depth == 0) break;
        pos++;
    }
    if (code[pos] != ';') {
        error("For dongusunde ikinci ';' bekleniyor");
    }
    pos++; // ; atla
    
    // INC pozisyonunu kaydet
    skip_whitespace_and_comments();
    int inc_pos = pos;
    
    // INC'i atla, ) bul
    depth = 0;
    while (pos < code_len) {
        if (code[pos] == '(') depth++;
        else if (code[pos] == ')') {
            if (depth == 0) break;
            depth--;
        }
        pos++;
    }
    pos++; // ) atla
    
    // Blok baslangicinı kaydet
    skip_whitespace_and_comments();
    int block_pos = pos;
    
    // Blok sonunu bul
    if (code[pos] != '{') {
        error("For dongusunde '{' bekleniyor");
    }
    pos++; // { atla
    depth = 1;
    while (depth > 0 && pos < code_len) {
        if (code[pos] == '{') depth++;
        else if (code[pos] == '}') depth--;
        if (depth > 0) pos++;
    }
    pos++; // } atla
    
    // ; sonrasi pozisyonu kaydet
    skip_whitespace_and_comments();
    if (code[pos] == ';') {
        pos++; // ; atla
    }
    skip_whitespace_and_comments();
    int after_pos = pos;
    
    // INIT calistir (bir kez)
    pos = init_pos;
    next_token();
    if (current_token.type == TOK_VAR) {
        int idx = current_token.value;
        next_token();
        if (current_token.type == TOK_ASSIGN) {
            next_token();
            int val = parse_expression();
            set_var(idx, val);
        }
    }
    
    // Dongu
    while (1) {
        // COND degerlendir
        pos = cond_pos;
        next_token();
        int condition = parse_expression();
        
        if (!condition) break;
        
        // Blogu calistir
        pos = block_pos;
        next_token();
        
        should_continue = false;
        parse_block();
        
        if (should_break) {
            should_break = false;
            break;
        }
        if (should_return) break;
        
        // INC calistir
        pos = inc_pos;
        next_token();
        
        if (current_token.type == TOK_VAR) {
            int idx = current_token.value;
            next_token();
            
            if (current_token.type == TOK_PLUSPLUS) {
                set_var(idx, get_var(idx) + 1);
            } else if (current_token.type == TOK_MINUSMINUS) {
                set_var(idx, get_var(idx) - 1);
            } else if (current_token.type == TOK_ASSIGN) {
                next_token();
                int val = parse_expression();
                set_var(idx, val);
            }
        }
    }
    
    should_continue = false;
    
    // ; sonrasina git
    pos = after_pos;
    next_token();
}

// Komut çözümleyici
static void parse_statement(void) {
    if (should_break || should_continue || should_return) return;
    
    // Fonksiyon tanımı
    if (current_token.type == TOK_FN) {
        parse_function_def();
        return;
    }
    
    // Return
    if (current_token.type == TOK_RN) {
        next_token();
        return_value = parse_expression();
        should_return = true;
        expect(TOK_SEMI);
        return;
    }
    
    // Break
    if (current_token.type == TOK_BREAK) {
        next_token();
        should_break = true;
        expect(TOK_SEMI);
        return;
    }
    
    // Continue
    if (current_token.type == TOK_CONTINUE) {
        next_token();
        should_continue = true;
        expect(TOK_SEMI);
        return;
    }
    
    // Output
    if (current_token.type == TOK_OUT) {
        next_token();
        expect(TOK_LPAREN);
        int val = parse_expression();
        expect(TOK_RPAREN);
        expect(TOK_SEMI);
        printf("%d\n", val);
        return;
    }
    
    // If
    if (current_token.type == TOK_QUESTION) {
        parse_if();
        return;
    }
    
    // For
    if (current_token.type == TOK_AT) {
        parse_for();
        return;
    }
    
    // $x++ veya $x--
    if (current_token.type == TOK_VAR) {
        int idx = current_token.value;
        next_token();
        
        if (current_token.type == TOK_PLUSPLUS) {
            set_var(idx, get_var(idx) + 1);
            next_token();
            expect(TOK_SEMI);
            return;
        }
        if (current_token.type == TOK_MINUSMINUS) {
            set_var(idx, get_var(idx) - 1);
            next_token();
            expect(TOK_SEMI);
            return;
        }
        
        // Atama
        expect(TOK_ASSIGN);
        int val = parse_expression();
        set_var(idx, val);
        expect(TOK_SEMI);
        return;
    }
    
    // Boş komut
    if (current_token.type == TOK_SEMI) {
        next_token();
        return;
    }
    
    if (current_token.type == TOK_EOF) {
        return;
    }
    
    error("Gecersiz Komut");
}

/* ==================== ANA PROGRAM ==================== */
static char* read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Dosya acilamadi: %s\n", filename);
        exit(1);
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = (char*)malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    
    fclose(f);
    return content;
}

static void init_interpreter(void) {
    memset(&global_scope, 0, sizeof(Scope));
    func_count = 0;
    call_depth = 0;
    should_break = false;
    should_continue = false;
    should_return = false;
    return_value = 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("NaC Dili Yorumlayicisi v1.0\n");
        printf("Kullanim: %s <dosya.nac>\n\n", argv[0]);
        printf("Ornek:\n");
        printf("  %s program.nac\n", argv[0]);
        return 1;
    }
    
    init_interpreter();
    
    code = read_file(argv[1]);
    code_len = strlen(code);
    pos = 0;
    
    next_token();
    
    while (current_token.type != TOK_EOF) {
        parse_statement();
    }
    
    free(code);
    
    return 0;
}