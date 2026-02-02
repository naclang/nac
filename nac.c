/*
 * NaC Language Interpreter v2.0.1
 * -----------------------------
 * Sembol agirlikli, minimal, C-benzeri bir yorumlanan dil.
 * 
 * Derleme: gcc -o nac nac.c -lm
 * Kullanim: ./nac program.nac
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

/* ==================== SABITLER ==================== */
#define MAX_VARS 26
#define MAX_FUNCS 100
#define MAX_PARAMS 10
#define MAX_CALL_DEPTH 100
#define MAX_TOKEN_LEN 256
#define MAX_STRING_LEN 1024

/* ==================== VERI TURLERI ==================== */
typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING
} ValueType;

typedef struct {
    ValueType type;
    union {
        int int_val;
        double float_val;
        char str_val[MAX_STRING_LEN];
    };
} Value;

/* ==================== TOKEN TIPLERI ==================== */
typedef enum {
    TOK_EOF,
    TOK_INT,         // 123
    TOK_FLOAT,       // 3.14
    TOK_STRING,      // "hello"
    TOK_VAR,         // $x
    TOK_PLUS,        // +
    TOK_MINUS,       // -
    TOK_STAR,        // *
    TOK_SLASH,       // /
    TOK_PERCENT,     // %
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
    TOK_IF,          // if
    TOK_COLON,       // : (else)
    TOK_FOR,         // for
    TOK_PLUSPLUS,    // ++
    TOK_MINUSMINUS,  // --
    TOK_FN,          // fn
    TOK_RN,          // rn (return)
    TOK_IN,          // in
    TOK_OUT,         // out
    TOK_TIME,        // time
    TOK_BREAK,       // break
    TOK_CONTINUE     // continue
} TokenType;

/* ==================== YAPILAR ==================== */
typedef struct {
    TokenType type;
    Value value;
    int var_idx;
} Token;

typedef struct {
    char name[MAX_TOKEN_LEN];
    char params[MAX_PARAMS];
    int param_count;
    int body_start;
    int body_end;
} Function;

typedef struct {
    Value vars[MAX_VARS];
    bool var_defined[MAX_VARS];
} Scope;

/* ==================== GLOBAL DEGISKENLER ==================== */
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
static Value return_value;

/* ==================== ILERI BILDIRIMLER ==================== */
static void next_token(void);
static Value parse_expression(void);
static void parse_statement(void);
static void parse_block(void);

/* ==================== HATA YONETIMI ==================== */
static void error(const char *msg) {
    int line = 1;
    for (int i = 0; i < pos; i++) {
        if (code[i] == '\n') line++;
    }
    fprintf(stderr, "Hata (Satir %d, Pozisyon %d): %s\n", line, pos, msg);
    exit(1);
}

/* ==================== DEGER YARDIMCILARI ==================== */
static Value make_int(int v) {
    Value val;
    val.type = TYPE_INT;
    val.int_val = v;
    return val;
}

static Value make_float(double v) {
    Value val;
    val.type = TYPE_FLOAT;
    val.float_val = v;
    return val;
}

static Value make_string(const char *s) {
    Value val;
    val.type = TYPE_STRING;
    strncpy(val.str_val, s, MAX_STRING_LEN - 1);
    val.str_val[MAX_STRING_LEN - 1] = '\0';
    return val;
}

static double to_float(Value v) {
    switch (v.type) {
        case TYPE_INT: return (double)v.int_val;
        case TYPE_FLOAT: return v.float_val;
        case TYPE_STRING: return atof(v.str_val);
    }
    return 0.0;
}

static int to_int(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val;
        case TYPE_FLOAT: return (int)v.float_val;
        case TYPE_STRING: return atoi(v.str_val);
    }
    return 0;
}

static int to_bool(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val != 0;
        case TYPE_FLOAT: return v.float_val != 0.0;
        case TYPE_STRING: return strlen(v.str_val) > 0;
    }
    return 0;
}

static void print_value(Value v) {
    switch (v.type) {
        case TYPE_INT: printf("%d\n", v.int_val); break;
        case TYPE_FLOAT: printf("%g\n", v.float_val); break;
        case TYPE_STRING: printf("%s\n", v.str_val); break;
    }
}

/* ==================== LEXER ==================== */
static void skip_whitespace_and_comments(void) {
    while (pos < code_len) {
        while (pos < code_len && isspace(code[pos])) {
            pos++;
        }
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
    
    // Sayi: 123, -123, 3.14, -3.14
    if (isdigit(c) || (c == '-' && pos + 1 < code_len && isdigit(code[pos + 1]))) {
        int start = pos;
        int sign = 1;
        if (c == '-') {
            sign = -1;
            pos++;
        }
        
        // Tam kismi oku
        double value = 0;
        while (pos < code_len && isdigit(code[pos])) {
            value = value * 10 + (code[pos] - '0');
            pos++;
        }
        
        // Ondalik kisim?
        if (pos < code_len && code[pos] == '.') {
            pos++;
            double decimal = 0.1;
            while (pos < code_len && isdigit(code[pos])) {
                value += (code[pos] - '0') * decimal;
                decimal *= 0.1;
                pos++;
            }
            current_token.type = TOK_FLOAT;
            current_token.value = make_float(sign * value);
        } else {
            current_token.type = TOK_INT;
            current_token.value = make_int(sign * (int)value);
        }
        return;
    }
    
    // String: "..."
    if (c == '"') {
        pos++;
        char str[MAX_STRING_LEN];
        int len = 0;
        while (pos < code_len && code[pos] != '"' && len < MAX_STRING_LEN - 1) {
            if (code[pos] == '\\' && pos + 1 < code_len) {
                pos++;
                switch (code[pos]) {
                    case 'n': str[len++] = '\n'; break;
                    case 't': str[len++] = '\t'; break;
                    case '\\': str[len++] = '\\'; break;
                    case '"': str[len++] = '"'; break;
                    default: str[len++] = code[pos]; break;
                }
            } else {
                str[len++] = code[pos];
            }
            pos++;
        }
        str[len] = '\0';
        if (pos < code_len && code[pos] == '"') pos++;
        current_token.type = TOK_STRING;
        current_token.value = make_string(str);
        return;
    }
    
    // Degisken: $x
    if (c == '$') {
        pos++;
        if (pos < code_len && isalpha(code[pos])) {
            char var = tolower(code[pos]);
            pos++;
            current_token.type = TOK_VAR;
            current_token.var_idx = var - 'a';
            return;
        }
        error("Degisken adi bekleniyor");
    }
    
    // Anahtar kelimeler
    if (strncmp(&code[pos], "fn", 2) == 0 && !isalnum(code[pos + 2])) {
        pos += 2; current_token.type = TOK_FN; return;
    }
    if (strncmp(&code[pos], "rn", 2) == 0 && !isalnum(code[pos + 2])) {
        pos += 2; current_token.type = TOK_RN; return;
    }
    if (strncmp(&code[pos], "if", 2) == 0 && !isalnum(code[pos + 2])) {
        pos += 2;current_token.type = TOK_IF; return;
    }
    if (strncmp(&code[pos], "in", 2) == 0 && !isalnum(code[pos + 2])) {
        pos += 2; current_token.type = TOK_IN; return;
    }
    if (strncmp(&code[pos], "for", 3) == 0 && !isalnum(code[pos + 3])) {
        pos += 3;current_token.type = TOK_FOR; return;
    }
    if (strncmp(&code[pos], "out", 3) == 0 && !isalnum(code[pos + 3])) {
        pos += 3; current_token.type = TOK_OUT; return;
    }
    if (strncmp(&code[pos], "time", 4) == 0 && !isalnum(code[pos + 4])) {
        pos += 4;
        current_token.type = TOK_TIME;
        return;
    }
    if (strncmp(&code[pos], "break", 5) == 0 && !isalnum(code[pos + 5])) {
        pos += 5; current_token.type = TOK_BREAK; return;
    }
    if (strncmp(&code[pos], "next", 4) == 0 && !isalnum(code[pos + 4])) {
        pos += 4; current_token.type = TOK_CONTINUE; return;
    }
    
    // Cift karakterli operatorler
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
    
    // Tek karakterli operatorler
    pos++;
    switch (c) {
        case '+': current_token.type = TOK_PLUS; return;
        case '-': current_token.type = TOK_MINUS; return;
        case '*': current_token.type = TOK_STAR; return;
        case '/': current_token.type = TOK_SLASH; return;
        case '%': current_token.type = TOK_PERCENT; return;
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
        case ':': current_token.type = TOK_COLON; return;
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

/* ==================== SCOPE YONETIMI ==================== */
static Scope* current_scope(void) {
    if (call_depth > 0) {
        return call_stack[call_depth - 1];
    }
    return &global_scope;
}

static Value get_var(int idx) {
    Scope *scope = current_scope();
    if (!scope->var_defined[idx]) {
        if (call_depth > 0 && global_scope.var_defined[idx]) {
            return global_scope.vars[idx];
        }
        return make_int(0);
    }
    return scope->vars[idx];
}

static void set_var(int idx, Value value) {
    Scope *scope = current_scope();
    scope->vars[idx] = value;
    scope->var_defined[idx] = true;
}

/* ==================== FONKSIYON YONETIMI ==================== */
static Function* find_function(const char *name) {
    for (int i = 0; i < func_count; i++) {
        if (strcmp(functions[i].name, name) == 0) {
            return &functions[i];
        }
    }
    return NULL;
}

static Value call_function(Function *func, Value *args, int arg_count) {
    if (arg_count != func->param_count) {
        error("Yanlis parametre sayisi");
    }
    
    if (call_depth >= MAX_CALL_DEPTH) {
        error("Cagri yigini tasmasi");
    }
    
    Scope *new_scope = (Scope*)malloc(sizeof(Scope));
    memset(new_scope, 0, sizeof(Scope));
    
    for (int i = 0; i < arg_count; i++) {
        int idx = func->params[i] - 'a';
        new_scope->vars[idx] = args[i];
        new_scope->var_defined[idx] = true;
    }
    
    call_stack[call_depth++] = new_scope;
    
    int old_pos = pos;
    pos = func->body_start;
    
    should_return = false;
    return_value = make_int(0);
    
    next_token();
    while (pos < func->body_end && !should_return) {
        parse_statement();
        if (current_token.type == TOK_EOF) break;
    }
    
    pos = old_pos;
    free(call_stack[--call_depth]);
    
    should_return = false;
    Value ret = return_value;
    return_value = make_int(0);
    
    next_token();
    return ret;
}

/* ==================== PARSER - IFADE ==================== */
static Value parse_primary(void) {
    if (current_token.type == TOK_INT || current_token.type == TOK_FLOAT || 
        current_token.type == TOK_STRING) {
        Value val = current_token.value;
        next_token();
        return val;
    }

    if (current_token.type == TOK_TIME) {
        next_token();
        return make_int((int)time(NULL));
    }
    
    if (current_token.type == TOK_VAR) {
        int idx = current_token.var_idx;
        char name[2] = { (char)('a' + idx), '\0' };
        next_token();
        
        if (current_token.type == TOK_LPAREN) {
            Function *func = find_function(name);
            if (!func) {
                fprintf(stderr, "Tanimsiz fonksiyon: $%s\n", name);
                error("Tanimsiz fonksiyon");
            }
            
            next_token();
            Value args[MAX_PARAMS];
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
        char buf[MAX_STRING_LEN];
        printf("> ");
        fflush(stdout);
        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            error("Giris hatasi");
        }
        // Yeni satiri kaldir
        buf[strcspn(buf, "\n")] = '\0';
        
        // Sayi mi string mi?
        char *endptr;
        long int_val = strtol(buf, &endptr, 10);
        if (*endptr == '\0') {
            return make_int((int)int_val);
        }
        double float_val = strtod(buf, &endptr);
        if (*endptr == '\0') {
            return make_float(float_val);
        }
        return make_string(buf);
    }
    
    if (current_token.type == TOK_LPAREN) {
        next_token();
        Value val = parse_expression();
        expect(TOK_RPAREN);
        return val;
    }
    
    if (current_token.type == TOK_NOT) {
        next_token();
        return make_int(!to_bool(parse_primary()));
    }
    
    if (current_token.type == TOK_MINUS) {
        next_token();
        Value v = parse_primary();
        if (v.type == TYPE_FLOAT) return make_float(-v.float_val);
        return make_int(-to_int(v));
    }
    
    error("Ifade bekleniyor");
    return make_int(0);
}

static Value parse_term(void) {
    Value left = parse_primary();
    
    while (current_token.type == TOK_STAR || current_token.type == TOK_SLASH ||
           current_token.type == TOK_PERCENT) {
        TokenType op = current_token.type;
        next_token();
        Value right = parse_primary();
        
        // String concatenation with *
        if (left.type == TYPE_STRING && op == TOK_STAR) {
            int times = to_int(right);
            char result[MAX_STRING_LEN] = "";
            for (int i = 0; i < times && strlen(result) + strlen(left.str_val) < MAX_STRING_LEN - 1; i++) {
                strcat(result, left.str_val);
            }
            left = make_string(result);
            continue;
        }
        
        // Float islemleri
        if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
            double l = to_float(left), r = to_float(right);
            if (op == TOK_STAR) left = make_float(l * r);
            else if (op == TOK_SLASH) {
                if (r == 0) error("Sifira bolme hatasi");
                left = make_float(l / r);
            } else {
                left = make_int((int)l % (int)r);
            }
        } else {
            int l = to_int(left), r = to_int(right);
            if (op == TOK_STAR) left = make_int(l * r);
            else if (op == TOK_SLASH) {
                if (r == 0) error("Sifira bolme hatasi");
                left = make_int(l / r);
            } else {
                left = make_int(l % r);
            }
        }
    }
    
    return left;
}

static Value parse_additive(void) {
    Value left = parse_term();
    
    while (current_token.type == TOK_PLUS || current_token.type == TOK_MINUS) {
        TokenType op = current_token.type;
        next_token();
        Value right = parse_term();
        
        // String birlestirme
        if (left.type == TYPE_STRING || right.type == TYPE_STRING) {
            if (op == TOK_PLUS) {
                char result[MAX_STRING_LEN];
                if (left.type == TYPE_STRING) {
                    strcpy(result, left.str_val);
                } else if (left.type == TYPE_FLOAT) {
                    sprintf(result, "%g", left.float_val);
                } else {
                    sprintf(result, "%d", left.int_val);
                }
                
                if (right.type == TYPE_STRING) {
                    strncat(result, right.str_val, MAX_STRING_LEN - strlen(result) - 1);
                } else if (right.type == TYPE_FLOAT) {
                    char tmp[64];
                    sprintf(tmp, "%g", right.float_val);
                    strncat(result, tmp, MAX_STRING_LEN - strlen(result) - 1);
                } else {
                    char tmp[64];
                    sprintf(tmp, "%d", right.int_val);
                    strncat(result, tmp, MAX_STRING_LEN - strlen(result) - 1);
                }
                left = make_string(result);
            } else {
                error("Stringlerden cikarma yapilamaz");
            }
            continue;
        }
        
        // Float islemleri
        if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
            double l = to_float(left), r = to_float(right);
            if (op == TOK_PLUS) left = make_float(l + r);
            else left = make_float(l - r);
        } else {
            int l = to_int(left), r = to_int(right);
            if (op == TOK_PLUS) left = make_int(l + r);
            else left = make_int(l - r);
        }
    }
    
    return left;
}

static Value parse_comparison(void) {
    Value left = parse_additive();
    
    while (current_token.type == TOK_LT || current_token.type == TOK_GT ||
           current_token.type == TOK_LTE || current_token.type == TOK_GTE) {
        TokenType op = current_token.type;
        next_token();
        Value right = parse_additive();
        
        // String karsilastirma
        if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
            int cmp = strcmp(left.str_val, right.str_val);
            switch (op) {
                case TOK_LT: left = make_int(cmp < 0); break;
                case TOK_GT: left = make_int(cmp > 0); break;
                case TOK_LTE: left = make_int(cmp <= 0); break;
                case TOK_GTE: left = make_int(cmp >= 0); break;
                default: break;
            }
            continue;
        }
        
        double l = to_float(left), r = to_float(right);
        switch (op) {
            case TOK_LT: left = make_int(l < r); break;
            case TOK_GT: left = make_int(l > r); break;
            case TOK_LTE: left = make_int(l <= r); break;
            case TOK_GTE: left = make_int(l >= r); break;
            default: break;
        }
    }
    
    return left;
}

static Value parse_equality(void) {
    Value left = parse_comparison();
    
    while (current_token.type == TOK_EQ || current_token.type == TOK_NEQ) {
        TokenType op = current_token.type;
        next_token();
        Value right = parse_comparison();
        
        int eq;
        if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
            eq = (strcmp(left.str_val, right.str_val) == 0);
        } else if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
            eq = (to_float(left) == to_float(right));
        } else {
            eq = (to_int(left) == to_int(right));
        }
        
        if (op == TOK_EQ) left = make_int(eq);
        else left = make_int(!eq);
    }
    
    return left;
}

static Value parse_and(void) {
    Value left = parse_equality();
    
    while (current_token.type == TOK_AND) {
        next_token();
        Value right = parse_equality();
        left = make_int(to_bool(left) && to_bool(right));
    }
    
    return left;
}

static Value parse_or(void) {
    Value left = parse_and();
    
    while (current_token.type == TOK_OR) {
        next_token();
        Value right = parse_and();
        left = make_int(to_bool(left) || to_bool(right));
    }
    
    return left;
}

static Value parse_expression(void) {
    return parse_or();
}

/* ==================== PARSER - KOMUT ==================== */

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

static void parse_block(void) {
    expect(TOK_LBRACE);
    
    while (current_token.type != TOK_RBRACE && current_token.type != TOK_EOF) {
        if (should_break || should_continue || should_return) break;
        parse_statement();
    }
    
    while (current_token.type != TOK_RBRACE && current_token.type != TOK_EOF) {
        if (current_token.type == TOK_LBRACE) {
            skip_block();
        } else {
            next_token();
        }
    }
    
    expect(TOK_RBRACE);
}

static void parse_function_def(void) {
    // TOK_FN zaten okundu, devam et
    next_token();
    
    if (current_token.type != TOK_VAR) {
        error("Fonksiyon adi bekleniyor");
    }
    
    Function *func = &functions[func_count++];
    func->name[0] = 'a' + current_token.var_idx;
    func->name[1] = '\0';
    func->param_count = 0;
    
    next_token();
    expect(TOK_LPAREN);
    
    if (current_token.type == TOK_VAR) {
        func->params[func->param_count++] = 'a' + current_token.var_idx;
        next_token();
        
        while (current_token.type == TOK_COMMA) {
            next_token();
            if (current_token.type != TOK_VAR) {
                error("Parametre bekleniyor");
            }
            func->params[func->param_count++] = 'a' + current_token.var_idx;
            next_token();
        }
    }
    
    expect(TOK_RPAREN);
    
    if (current_token.type != TOK_LBRACE) {
        error("Fonksiyon govdesi icin '{' bekleniyor");
    }
    
    // { icindeki body'nin baslangic pozisyonunu bul
    // pos simdi { karakterinin sonrasinda
    skip_whitespace_and_comments();
    func->body_start = pos;
    
    // Body sonunu bul (eslesen } karakteri)
    int depth = 1;
    while (depth > 0 && pos < code_len) {
        if (code[pos] == '{') depth++;
        else if (code[pos] == '}') depth--;
        if (depth > 0) pos++;
    }
    func->body_end = pos;
    
    // } ve ; atla
    pos++; // } atla
    skip_whitespace_and_comments();
    if (pos < code_len && code[pos] == ';') {
        pos++; // ; atla
    }
    
    next_token();
}

static void parse_if(void) {
    next_token();
    expect(TOK_LPAREN);
    
    Value condition = parse_expression();
    
    expect(TOK_RPAREN);
    
    if (to_bool(condition)) {
        parse_block();
        if (current_token.type == TOK_COLON) {
            next_token();
            skip_block();
        }
    } else {
        skip_block();
        if (current_token.type == TOK_COLON) {
            next_token();
            parse_block();
        }
    }
    
    expect(TOK_SEMI);
}

static void parse_for(void) {
    next_token();
    
    if (current_token.type != TOK_LPAREN) {
        error("For dongusunde '(' bekleniyor");
    }
    
    skip_whitespace_and_comments();
    int init_pos = pos;
    
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
    pos++;
    
    skip_whitespace_and_comments();
    int cond_pos = pos;
    
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
    pos++;
    
    skip_whitespace_and_comments();
    int inc_pos = pos;
    
    depth = 0;
    while (pos < code_len) {
        if (code[pos] == '(') depth++;
        else if (code[pos] == ')') {
            if (depth == 0) break;
            depth--;
        }
        pos++;
    }
    pos++;
    
    skip_whitespace_and_comments();
    int block_pos = pos;
    
    if (code[pos] != '{') {
        error("For dongusunde '{' bekleniyor");
    }
    pos++;
    depth = 1;
    while (depth > 0 && pos < code_len) {
        if (code[pos] == '{') depth++;
        else if (code[pos] == '}') depth--;
        if (depth > 0) pos++;
    }
    pos++;
    
    skip_whitespace_and_comments();
    if (code[pos] == ';') {
        pos++;
    }
    skip_whitespace_and_comments();
    int after_pos = pos;
    
    // INIT
    pos = init_pos;
    next_token();
    if (current_token.type == TOK_VAR) {
        int idx = current_token.var_idx;
        next_token();
        if (current_token.type == TOK_ASSIGN) {
            next_token();
            Value val = parse_expression();
            set_var(idx, val);
        }
    }
    
    // Dongu
    while (1) {
        pos = cond_pos;
        next_token();
        Value condition = parse_expression();
        
        if (!to_bool(condition)) break;
        
        pos = block_pos;
        next_token();
        
        should_continue = false;
        parse_block();
        
        if (should_break) {
            should_break = false;
            break;
        }
        if (should_return) break;
        
        // INC
        pos = inc_pos;
        next_token();
        
        if (current_token.type == TOK_VAR) {
            int idx = current_token.var_idx;
            next_token();
            
            if (current_token.type == TOK_PLUSPLUS) {
                Value v = get_var(idx);
                if (v.type == TYPE_FLOAT) set_var(idx, make_float(v.float_val + 1));
                else set_var(idx, make_int(to_int(v) + 1));
            } else if (current_token.type == TOK_MINUSMINUS) {
                Value v = get_var(idx);
                if (v.type == TYPE_FLOAT) set_var(idx, make_float(v.float_val - 1));
                else set_var(idx, make_int(to_int(v) - 1));
            } else if (current_token.type == TOK_ASSIGN) {
                next_token();
                Value val = parse_expression();
                set_var(idx, val);
            }
        }
    }
    
    should_continue = false;
    pos = after_pos;
    next_token();
}

static void parse_statement(void) {
    if (should_break || should_continue || should_return) return;
    
    if (current_token.type == TOK_FN) {
        parse_function_def();
        return;
    }
    
    if (current_token.type == TOK_RN) {
        next_token();
        return_value = parse_expression();
        should_return = true;
        expect(TOK_SEMI);
        return;
    }
    
    if (current_token.type == TOK_BREAK) {
        next_token();
        should_break = true;
        expect(TOK_SEMI);
        return;
    }
    
    if (current_token.type == TOK_CONTINUE) {
        next_token();
        should_continue = true;
        expect(TOK_SEMI);
        return;
    }
    
    if (current_token.type == TOK_OUT) {
        next_token();
        expect(TOK_LPAREN);
        Value val = parse_expression();
        expect(TOK_RPAREN);
        expect(TOK_SEMI);
        print_value(val);
        return;
    }
    
    if (current_token.type == TOK_IF) {
        parse_if();
        return;
    }
    
    if (current_token.type == TOK_FOR) {
        parse_for();
        return;
    }
    
    if (current_token.type == TOK_VAR) {
        int idx = current_token.var_idx;
        next_token();
        
        if (current_token.type == TOK_PLUSPLUS) {
            Value v = get_var(idx);
            if (v.type == TYPE_FLOAT) set_var(idx, make_float(v.float_val + 1));
            else set_var(idx, make_int(to_int(v) + 1));
            next_token();
            expect(TOK_SEMI);
            return;
        }
        if (current_token.type == TOK_MINUSMINUS) {
            Value v = get_var(idx);
            if (v.type == TYPE_FLOAT) set_var(idx, make_float(v.float_val - 1));
            else set_var(idx, make_int(to_int(v) - 1));
            next_token();
            expect(TOK_SEMI);
            return;
        }
        
        expect(TOK_ASSIGN);
        Value val = parse_expression();
        set_var(idx, val);
        expect(TOK_SEMI);
        return;
    }
    
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
    return_value = make_int(0);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("NaC Dili Yorumlayicisi v2.0.1\n");
        printf("Kullanim: %s <dosya.nac>\n\n", argv[0]);
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