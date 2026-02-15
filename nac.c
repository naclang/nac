/*
 * NaC Language Interpreter v3.1.0
 * ------------------------------------------
 * 
 * Compile windows: gcc -o nac.exe nac.c -lm -lwinhttp
 * Compile linux & macOS: gcc -o nac nac.c -lm -lcurl
 * Usage: ./nac program.nac
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
    #define UNICODE
    #define _UNICODE
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#else
    #include <curl/curl.h>
#endif

/* ==================== CONSTANTS ==================== */
#define MAX_FUNCS 100
#define MAX_PARAMS 10
#define MAX_CALL_DEPTH 100
#define MAX_TOKEN_LEN 256
#define MAX_STRING_LEN 1024
#define HASH_TABLE_SIZE 256
#define MAX_ARRAY_SIZE 10000

/* ==================== DATA TYPES ==================== */
typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_ARRAY
} ValueType;

typedef struct Value {
    ValueType type;
    union {
        int int_val;
        double float_val;
        char str_val[MAX_STRING_LEN];
        struct {
            struct Value *elements;
            int size;
            int capacity;
        } array_val;
    };
} Value;

/* ==================== TOKEN TYPES ==================== */
typedef enum {
    TOK_EOF,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_IDENT,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_GT,
    TOK_LTE,
    TOK_GTE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_ASSIGN,
    TOK_SEMI,
    TOK_COMMA,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_IF,
    TOK_COLON,
    TOK_FOR,
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,
    TOK_FN,
    TOK_RN,
    TOK_IN,
    TOK_OUT,
    TOK_TIME,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_ARRAY,
    TOK_WHILE,
    TOK_HTTP
} NaCTokenType;

/* ==================== AST NODE TYPES ==================== */
typedef enum {
    AST_INT_LITERAL,
    AST_FLOAT_LITERAL,
    AST_STRING_LITERAL,
    AST_VARIABLE,
    AST_ARRAY_ACCESS,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_ASSIGN,
    AST_ARRAY_ASSIGN,
    AST_CALL,
    AST_BLOCK,
    AST_IF,
    AST_FOR,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_OUT,
    AST_IN,
    AST_INCREMENT,
    AST_DECREMENT,
    AST_ARRAY_LITERAL,
    AST_WHILE,
    AST_HTTP
} ASTNodeType;

typedef struct ASTNode {
    ASTNodeType type;
    union {
        int int_val;
        double float_val;
        char str_val[MAX_STRING_LEN];
        char var_name[MAX_TOKEN_LEN];
        struct {
            NaCTokenType op;
            struct ASTNode *left;
            struct ASTNode *right;
        } binary;
        struct {
            NaCTokenType op;
            struct ASTNode *operand;
        } unary;
        struct {
            char var_name[MAX_TOKEN_LEN];
            struct ASTNode *value;
        } assign;
        struct {
            char var_name[MAX_TOKEN_LEN];
            struct ASTNode *index;
            struct ASTNode *value;
        } array_assign;
        struct {
            char var_name[MAX_TOKEN_LEN];
            struct ASTNode *index;
        } array_access;
        struct {
            char func_name[MAX_TOKEN_LEN];
            struct ASTNode **args;
            int arg_count;
        } call;
        struct {
            struct ASTNode **statements;
            int count;
        } block;
        struct {
            struct ASTNode *condition;
            struct ASTNode *then_block;
            struct ASTNode *else_block;
        } if_stmt;
        struct {
            struct ASTNode *init;
            struct ASTNode *condition;
            struct ASTNode *increment;
            struct ASTNode *body;
        } for_stmt;
        struct {
            struct ASTNode *value;
        } return_stmt;
        struct {
            struct ASTNode *value;
        } out_stmt;
        struct {
            char var_name[MAX_TOKEN_LEN];
        } in_stmt;
        struct {
            char var_name[MAX_TOKEN_LEN];
        } inc_dec;
        struct {
            struct ASTNode **elements;
            int count;
        } array_literal;
        struct {
            struct ASTNode *condition;
            struct ASTNode *body;
        } while_stmt;
        struct {
            struct ASTNode *method;
            struct ASTNode *url;
            struct ASTNode *body;
        } http_stmt;
    };
} ASTNode;

/* ==================== HASH TABLE FOR VARIABLES ==================== */
typedef struct VarEntry {
    char name[MAX_TOKEN_LEN];
    Value value;
    struct VarEntry *next;
} VarEntry;

typedef struct {
    VarEntry *buckets[HASH_TABLE_SIZE];
} VarTable;

/* ==================== TOKEN STRUCTURE ==================== */
typedef struct {
    NaCTokenType type;
    union {
        int int_val;
        double float_val;
        char str_val[MAX_STRING_LEN];
        char ident[MAX_TOKEN_LEN];
    };
} Token;

/* ==================== FUNCTION STRUCTURE ==================== */
typedef struct {
    char name[MAX_TOKEN_LEN];
    char params[MAX_PARAMS][MAX_TOKEN_LEN];
    int param_count;
    ASTNode *body;
} Function;

/* ==================== GLOBAL VARIABLES ==================== */
static char *code = NULL;
static int pos = 0;
static int code_len = 0;
static Token current_token;

static VarTable *global_vars;
static VarTable *call_stack_vars[MAX_CALL_DEPTH];
static int call_depth = 0;

static Function functions[MAX_FUNCS];
static int func_count = 0;

static bool should_break = false;
static bool should_continue = false;
static bool should_return = false;
static Value return_value;

static bool error_occurred = false;
static int error_count = 0;

/* ==================== FORWARD DECLARATIONS ==================== */
static void next_token(void);
static ASTNode* parse_expression(void);
static ASTNode* parse_statement(void);
static Value eval_node(ASTNode *node);

/* ==================== ERROR HANDLING ==================== */
static void report_error(const char *msg) {
    int line = 1;
    int col = 1;
    for (int i = 0; i < pos && i < code_len; i++) {
        if (code[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    fprintf(stderr, "Error (Line %d, Column %d): %s\n", line, col, msg);
    error_occurred = true;
    error_count++;
}

static void error_and_exit(const char *msg) {
    report_error(msg);
    exit(1);
}

/* ==================== HASH TABLE FUNCTIONS ==================== */
static unsigned int hash(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % HASH_TABLE_SIZE;
}

static VarTable* create_var_table(void) {
    VarTable *table = (VarTable*)calloc(1, sizeof(VarTable));
    return table;
}

static void free_value(Value *v) {
    if (v->type == TYPE_ARRAY && v->array_val.elements) {
        for (int i = 0; i < v->array_val.size; i++) {
            free_value(&v->array_val.elements[i]);
        }
        free(v->array_val.elements);
    }
}

static void free_var_table(VarTable *table) {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        VarEntry *entry = table->buckets[i];
        while (entry) {
            VarEntry *next = entry->next;
            free_value(&entry->value);
            free(entry);
            entry = next;
        }
    }
    free(table);
}

static Value* get_var(const char *name) {
    // Check local scope first
    if (call_depth > 0) {
        VarTable *local = call_stack_vars[call_depth - 1];
        unsigned int idx = hash(name);
        VarEntry *entry = local->buckets[idx];
        while (entry) {
            if (strcmp(entry->name, name) == 0) {
                return &entry->value;
            }
            entry = entry->next;
        }
    }
    
    // Check global scope
    unsigned int idx = hash(name);
    VarEntry *entry = global_vars->buckets[idx];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return &entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

static Value copy_value(Value v) {
    if (v.type == TYPE_ARRAY) {
        Value new_val;
        new_val.type = TYPE_ARRAY;
        new_val.array_val.size = v.array_val.size;
        new_val.array_val.capacity = v.array_val.size;
        new_val.array_val.elements = (Value*)malloc(sizeof(Value) * new_val.array_val.capacity);
        for (int i = 0; i < v.array_val.size; i++) {
            new_val.array_val.elements[i] = copy_value(v.array_val.elements[i]);
        }
        return new_val;
    }
    return v;
}

static void set_var(const char *name, Value value) {
    VarTable *table = (call_depth > 0) ? call_stack_vars[call_depth - 1] : global_vars;
    unsigned int idx = hash(name);
    
    VarEntry *entry = table->buckets[idx];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            free_value(&entry->value);
            entry->value = copy_value(value);
            return;
        }
        entry = entry->next;
    }
    
    // Create new entry
    VarEntry *new_entry = (VarEntry*)malloc(sizeof(VarEntry));
    strncpy(new_entry->name, name, MAX_TOKEN_LEN - 1);
    new_entry->name[MAX_TOKEN_LEN - 1] = '\0';
    new_entry->value = copy_value(value);
    new_entry->next = table->buckets[idx];
    table->buckets[idx] = new_entry;
}

/* ==================== VALUE HELPERS ==================== */
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

static Value make_array(int size) {
    Value val;
    val.type = TYPE_ARRAY;
    val.array_val.size = size;
    val.array_val.capacity = size;
    val.array_val.elements = (Value*)calloc(size, sizeof(Value));
    for (int i = 0; i < size; i++) {
        val.array_val.elements[i] = make_int(0);
    }
    return val;
}

static double to_float(Value v) {
    switch (v.type) {
        case TYPE_INT: return (double)v.int_val;
        case TYPE_FLOAT: return v.float_val;
        case TYPE_STRING: return atof(v.str_val);
        case TYPE_ARRAY: return 0.0;
    }
    return 0.0;
}

static int to_int(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val;
        case TYPE_FLOAT: return (int)v.float_val;
        case TYPE_STRING: return atoi(v.str_val);
        case TYPE_ARRAY: return v.array_val.size;
    }
    return 0;
}

static int to_bool(Value v) {
    switch (v.type) {
        case TYPE_INT: return v.int_val != 0;
        case TYPE_FLOAT: return v.float_val != 0.0;
        case TYPE_STRING: return strlen(v.str_val) > 0;
        case TYPE_ARRAY: return v.array_val.size > 0;
    }
    return 0;
}

static void print_value(Value v) {
    switch (v.type) {
        case TYPE_INT: printf("%d\n", v.int_val); break;
        case TYPE_FLOAT: printf("%g\n", v.float_val); break;
        case TYPE_STRING: printf("%s\n", v.str_val); break;
        case TYPE_ARRAY:
            printf("[");
            for (int i = 0; i < v.array_val.size; i++) {
                if (i > 0) printf(", ");
                switch (v.array_val.elements[i].type) {
                    case TYPE_INT: printf("%d", v.array_val.elements[i].int_val); break;
                    case TYPE_FLOAT: printf("%g", v.array_val.elements[i].float_val); break;
                    case TYPE_STRING: printf("\"%s\"", v.array_val.elements[i].str_val); break;
                    default: printf("?");
                }
            }
            printf("]\n");
            break;
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
    
    // Numbers
    if (isdigit(c) || (c == '-' && pos + 1 < code_len && isdigit(code[pos + 1]))) {
        int sign = 1;
        if (c == '-') {
            sign = -1;
            pos++;
        }
        
        double value = 0;
        while (pos < code_len && isdigit(code[pos])) {
            value = value * 10 + (code[pos] - '0');
            pos++;
        }
        
        if (pos < code_len && code[pos] == '.') {
            pos++;
            double decimal = 0.1;
            while (pos < code_len && isdigit(code[pos])) {
                value += (code[pos] - '0') * decimal;
                decimal *= 0.1;
                pos++;
            }
            current_token.type = TOK_FLOAT;
            current_token.float_val = sign * value;
        } else {
            current_token.type = TOK_INT;
            current_token.int_val = sign * (int)value;
        }
        return;
    }
    
    // Strings
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
        strncpy(current_token.str_val, str, MAX_STRING_LEN - 1);
        return;
    }
    
    // Identifiers and keywords
    if (isalpha(c) || c == '_' || c == '$') {
        int start = pos;
        while (pos < code_len && (isalnum(code[pos]) || code[pos] == '_')) {
            pos++;
        }
        int len = pos - start;
        char ident[MAX_TOKEN_LEN];
        strncpy(ident, &code[start], len);
        ident[len] = '\0';
        
        // Check keywords
        if (strcmp(ident, "fn") == 0) { current_token.type = TOK_FN; return; }
        if (strcmp(ident, "rn") == 0) { current_token.type = TOK_RN; return; }
        if (strcmp(ident, "if") == 0) { current_token.type = TOK_IF; return; }
        if (strcmp(ident, "for") == 0) { current_token.type = TOK_FOR; return; }
        if (strcmp(ident, "while") == 0) { current_token.type = TOK_WHILE; return; }
        if (strcmp(ident, "in") == 0) { current_token.type = TOK_IN; return; }
        if (strcmp(ident, "out") == 0) { current_token.type = TOK_OUT; return; }
        if (strcmp(ident, "time") == 0) { current_token.type = TOK_TIME; return; }
        if (strcmp(ident, "break") == 0) { current_token.type = TOK_BREAK; return; }
        if (strcmp(ident, "continue") == 0) { current_token.type = TOK_CONTINUE; return; }
        if (strcmp(ident, "array") == 0) { current_token.type = TOK_ARRAY; return; }
        if (strcmp(ident, "http") == 0) { current_token.type = TOK_HTTP; return; }
        
        current_token.type = TOK_IDENT;
        strncpy(current_token.ident, ident, MAX_TOKEN_LEN - 1);
        return;
    }
    
    // Operators
    if (c == '+') {
        if (pos + 1 < code_len && code[pos + 1] == '+') {
            pos += 2; current_token.type = TOK_PLUSPLUS; return;
        }
        pos++; current_token.type = TOK_PLUS; return;
    }
    if (c == '-') {
        if (pos + 1 < code_len && code[pos + 1] == '-') {
            pos += 2; current_token.type = TOK_MINUSMINUS; return;
        }
        pos++; current_token.type = TOK_MINUS; return;
    }
    if (c == '*') { pos++; current_token.type = TOK_STAR; return; }
    if (c == '/') { pos++; current_token.type = TOK_SLASH; return; }
    if (c == '%') { pos++; current_token.type = TOK_PERCENT; return; }
    if (c == '=') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            pos += 2; current_token.type = TOK_EQ; return;
        }
        pos++; current_token.type = TOK_ASSIGN; return;
    }
    if (c == '!') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            pos += 2; current_token.type = TOK_NEQ; return;
        }
        pos++; current_token.type = TOK_NOT; return;
    }
    if (c == '<') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            pos += 2; current_token.type = TOK_LTE; return;
        }
        pos++; current_token.type = TOK_LT; return;
    }
    if (c == '>') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            pos += 2; current_token.type = TOK_GTE; return;
        }
        pos++; current_token.type = TOK_GT; return;
    }
    if (c == '&' && pos + 1 < code_len && code[pos + 1] == '&') {
        pos += 2; current_token.type = TOK_AND; return;
    }
    if (c == '|' && pos + 1 < code_len && code[pos + 1] == '|') {
        pos += 2; current_token.type = TOK_OR; return;
    }
    if (c == ';') { pos++; current_token.type = TOK_SEMI; return; }
    if (c == ',') { pos++; current_token.type = TOK_COMMA; return; }
    if (c == '(') { pos++; current_token.type = TOK_LPAREN; return; }
    if (c == ')') { pos++; current_token.type = TOK_RPAREN; return; }
    if (c == '{') { pos++; current_token.type = TOK_LBRACE; return; }
    if (c == '}') { pos++; current_token.type = TOK_RBRACE; return; }
    if (c == '[') { pos++; current_token.type = TOK_LBRACKET; return; }
    if (c == ']') { pos++; current_token.type = TOK_RBRACKET; return; }
    if (c == ':') { pos++; current_token.type = TOK_COLON; return; }
    
    report_error("Unknown character");
    pos++;
    next_token();
}

/* ==================== AST HELPERS ==================== */
static ASTNode* create_node(ASTNodeType type) {
    ASTNode *node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = type;
    return node;
}

static void free_ast(ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_BINARY_OP:
            free_ast(node->binary.left);
            free_ast(node->binary.right);
            break;
        case AST_UNARY_OP:
            free_ast(node->unary.operand);
            break;
        case AST_ASSIGN:
            free_ast(node->assign.value);
            break;
        case AST_ARRAY_ASSIGN:
            free_ast(node->array_assign.index);
            free_ast(node->array_assign.value);
            break;
        case AST_ARRAY_ACCESS:
            free_ast(node->array_access.index);
            break;
        case AST_CALL:
            for (int i = 0; i < node->call.arg_count; i++) {
                free_ast(node->call.args[i]);
            }
            free(node->call.args);
            break;
        case AST_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                free_ast(node->block.statements[i]);
            }
            free(node->block.statements);
            break;
        case AST_IF:
            free_ast(node->if_stmt.condition);
            free_ast(node->if_stmt.then_block);
            free_ast(node->if_stmt.else_block);
            break;
        case AST_FOR:
            free_ast(node->for_stmt.init);
            free_ast(node->for_stmt.condition);
            free_ast(node->for_stmt.increment);
            free_ast(node->for_stmt.body);
            break;
        case AST_WHILE:
            free_ast(node->while_stmt.condition);
            free_ast(node->while_stmt.body);
            break;
        case AST_RETURN:
            free_ast(node->return_stmt.value);
            break;
        case AST_OUT:
            free_ast(node->out_stmt.value);
            break;
        case AST_HTTP:
            free_ast(node->http_stmt.method);
            free_ast(node->http_stmt.url);
            free_ast(node->http_stmt.body);
            break;
        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->array_literal.count; i++) {
                free_ast(node->array_literal.elements[i]);
            }
            free(node->array_literal.elements);
            break;
        default:
            break;
    }
    
    free(node);
}

/* ==================== PARSER ==================== */
static void expect(NaCTokenType type) {
    if (current_token.type != type) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Expected token type %d, got %d", type, current_token.type);
        report_error(msg);
        // Try to recover by advancing
        next_token();
    } else {
        next_token();
    }
}

static ASTNode* parse_primary(void) {
    ASTNode *node = NULL;
    
    if (current_token.type == TOK_INT) {
        node = create_node(AST_INT_LITERAL);
        node->int_val = current_token.int_val;
        next_token();
        return node;
    }
    
    if (current_token.type == TOK_FLOAT) {
        node = create_node(AST_FLOAT_LITERAL);
        node->float_val = current_token.float_val;
        next_token();
        return node;
    }
    
    if (current_token.type == TOK_STRING) {
        node = create_node(AST_STRING_LITERAL);
        strncpy(node->str_val, current_token.str_val, MAX_STRING_LEN - 1);
        next_token();
        return node;
    }
    
    if (current_token.type == TOK_IDENT) {
        char name[MAX_TOKEN_LEN];
        strncpy(name, current_token.ident, MAX_TOKEN_LEN - 1);
        next_token();
        
        // Array access
        if (current_token.type == TOK_LBRACKET) {
            next_token();
            ASTNode *index = parse_expression();
            expect(TOK_RBRACKET);
            node = create_node(AST_ARRAY_ACCESS);
            strncpy(node->array_access.var_name, name, MAX_TOKEN_LEN - 1);
            node->array_access.index = index;
            return node;
        }
        
        // Function call
        if (current_token.type == TOK_LPAREN) {
            next_token();
            node = create_node(AST_CALL);
            strncpy(node->call.func_name, name, MAX_TOKEN_LEN - 1);
            
            int capacity = 4;
            node->call.args = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
            node->call.arg_count = 0;
            
            if (current_token.type != TOK_RPAREN) {
                do {
                    if (node->call.arg_count >= capacity) {
                        capacity *= 2;
                        node->call.args = (ASTNode**)realloc(node->call.args, sizeof(ASTNode*) * capacity);
                    }
                    node->call.args[node->call.arg_count++] = parse_expression();
                    if (current_token.type == TOK_COMMA) {
                        next_token();
                    } else {
                        break;
                    }
                } while (current_token.type != TOK_RPAREN && current_token.type != TOK_EOF);
            }
            expect(TOK_RPAREN);
            return node;
        }
        
        // Variable
        node = create_node(AST_VARIABLE);
        strncpy(node->var_name, name, MAX_TOKEN_LEN - 1);
        return node;
    }
    
    if (current_token.type == TOK_TIME) {
        next_token();
        expect(TOK_LPAREN);
        expect(TOK_RPAREN);
        node = create_node(AST_INT_LITERAL);
        node->int_val = (int)time(NULL);
        return node;
    }
    
    if (current_token.type == TOK_ARRAY) {
        next_token();
        expect(TOK_LPAREN);
        ASTNode *size_expr = parse_expression();
        expect(TOK_RPAREN);
        
        // Create array literal node
        node = create_node(AST_ARRAY_LITERAL);
        node->array_literal.count = 1;
        node->array_literal.elements = (ASTNode**)malloc(sizeof(ASTNode*));
        node->array_literal.elements[0] = size_expr;
        return node;
    }
    
    if (current_token.type == TOK_LBRACKET) {
        next_token();
        node = create_node(AST_ARRAY_LITERAL);
        
        int capacity = 4;
        node->array_literal.elements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
        node->array_literal.count = 0;
        
        if (current_token.type != TOK_RBRACKET) {
            do {
                if (node->array_literal.count >= capacity) {
                    capacity *= 2;
                    node->array_literal.elements = (ASTNode**)realloc(node->array_literal.elements, sizeof(ASTNode*) * capacity);
                }
                node->array_literal.elements[node->array_literal.count++] = parse_expression();
                if (current_token.type == TOK_COMMA) {
                    next_token();
                } else {
                    break;
                }
            } while (1);
        }
        expect(TOK_RBRACKET);
        return node;
    }
    
    if (current_token.type == TOK_LPAREN) {
        next_token();
        node = parse_expression();
        expect(TOK_RPAREN);
        return node;
    }
    
    if (current_token.type == TOK_MINUS) {
        next_token();
        node = create_node(AST_UNARY_OP);
        node->unary.op = TOK_MINUS;
        node->unary.operand = parse_primary();
        return node;
    }
    
    if (current_token.type == TOK_NOT) {
        next_token();
        node = create_node(AST_UNARY_OP);
        node->unary.op = TOK_NOT;
        node->unary.operand = parse_primary();
        return node;
    }
    
    report_error("Expected expression");
    return create_node(AST_INT_LITERAL); // Return dummy node
}

static ASTNode* parse_multiplicative(void) {
    ASTNode *left = parse_primary();
    
    while (current_token.type == TOK_STAR || 
           current_token.type == TOK_SLASH || 
           current_token.type == TOK_PERCENT) {
        NaCTokenType op = current_token.type;
        next_token();
        ASTNode *right = parse_primary();
        
        ASTNode *node = create_node(AST_BINARY_OP);
        node->binary.op = op;
        node->binary.left = left;
        node->binary.right = right;
        left = node;
    }
    
    return left;
}

static ASTNode* parse_additive(void) {
    ASTNode *left = parse_multiplicative();
    
    while (current_token.type == TOK_PLUS || current_token.type == TOK_MINUS) {
        NaCTokenType op = current_token.type;
        next_token();
        ASTNode *right = parse_multiplicative();
        
        ASTNode *node = create_node(AST_BINARY_OP);
        node->binary.op = op;
        node->binary.left = left;
        node->binary.right = right;
        left = node;
    }
    
    return left;
}

static ASTNode* parse_comparison(void) {
    ASTNode *left = parse_additive();
    
    while (current_token.type == TOK_LT || current_token.type == TOK_GT ||
           current_token.type == TOK_LTE || current_token.type == TOK_GTE ||
           current_token.type == TOK_EQ || current_token.type == TOK_NEQ) {
        NaCTokenType op = current_token.type;
        next_token();
        ASTNode *right = parse_additive();
        
        ASTNode *node = create_node(AST_BINARY_OP);
        node->binary.op = op;
        node->binary.left = left;
        node->binary.right = right;
        left = node;
    }
    
    return left;
}

static ASTNode* parse_logical(void) {
    ASTNode *left = parse_comparison();
    
    while (current_token.type == TOK_AND || current_token.type == TOK_OR) {
        NaCTokenType op = current_token.type;
        next_token();
        ASTNode *right = parse_comparison();
        
        ASTNode *node = create_node(AST_BINARY_OP);
        node->binary.op = op;
        node->binary.left = left;
        node->binary.right = right;
        left = node;
    }
    
    return left;
}

static ASTNode* parse_expression(void) {
    return parse_logical();
}

static ASTNode* parse_block(void) {
    expect(TOK_LBRACE);
    
    ASTNode *block = create_node(AST_BLOCK);
    int capacity = 8;
    block->block.statements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    block->block.count = 0;
    
    while (current_token.type != TOK_RBRACE && current_token.type != TOK_EOF) {
        if (should_break || should_continue || should_return) break;
        
        if (block->block.count >= capacity) {
            capacity *= 2;
            block->block.statements = (ASTNode**)realloc(block->block.statements, sizeof(ASTNode*) * capacity);
        }
        
        ASTNode *stmt = parse_statement();
        if (stmt) {
            block->block.statements[block->block.count++] = stmt;
        }
    }
    
    expect(TOK_RBRACE);
    return block;
}

static ASTNode* parse_statement(void) {
    // Function definition
    if (current_token.type == TOK_FN) {
        next_token();
        
        if (current_token.type != TOK_IDENT) {
            report_error("Expected function name");
            return NULL;
        }
        
        Function *func = &functions[func_count++];
        strncpy(func->name, current_token.ident, MAX_TOKEN_LEN - 1);
        next_token();
        
        expect(TOK_LPAREN);
        func->param_count = 0;
        
        if (current_token.type != TOK_RPAREN) {
            do {
                if (current_token.type != TOK_IDENT) {
                    report_error("Expected parameter name");
                    break;
                }
                strncpy(func->params[func->param_count++], current_token.ident, MAX_TOKEN_LEN - 1);
                next_token();
                
                if (current_token.type == TOK_COMMA) {
                    next_token();
                } else {
                    break;
                }
            } while (func->param_count < MAX_PARAMS);
        }
        
        expect(TOK_RPAREN);
        func->body = parse_block();
        expect(TOK_SEMI);
        
        return NULL; // Function definitions don't produce AST nodes in statement list
    }
    
    // Return
    if (current_token.type == TOK_RN) {
        next_token();
        ASTNode *node = create_node(AST_RETURN);
        node->return_stmt.value = parse_expression();
        expect(TOK_SEMI);
        return node;
    }
    
    // Break
    if (current_token.type == TOK_BREAK) {
        next_token();
        expect(TOK_SEMI);
        return create_node(AST_BREAK);
    }
    
    // Continue
    if (current_token.type == TOK_CONTINUE) {
        next_token();
        expect(TOK_SEMI);
        return create_node(AST_CONTINUE);
    }
    
    // Output
    if (current_token.type == TOK_OUT) {
        next_token();
        expect(TOK_LPAREN);
        ASTNode *node = create_node(AST_OUT);
        node->out_stmt.value = parse_expression();
        expect(TOK_RPAREN);
        expect(TOK_SEMI);
        return node;
    }
    
    // Input
    if (current_token.type == TOK_IN) {
        next_token();
        expect(TOK_LPAREN);
        
        if (current_token.type != TOK_IDENT) {
            report_error("Expected variable name for input");
            return NULL;
        }
        
        char var_name[MAX_TOKEN_LEN];
        strncpy(var_name, current_token.ident, MAX_TOKEN_LEN - 1);
        next_token();
        
        // Check for array element input: in(arr[i])
        if (current_token.type == TOK_LBRACKET) {
            next_token();
            ASTNode *index = parse_expression();
            expect(TOK_RBRACKET);
            expect(TOK_RPAREN);
            expect(TOK_SEMI);
            
            // Create array assignment node with IN as value
            ASTNode *node = create_node(AST_ARRAY_ASSIGN);
            strncpy(node->array_assign.var_name, var_name, MAX_TOKEN_LEN - 1);
            node->array_assign.index = index;
            
            // Create special IN node as the value
            ASTNode *in_node = create_node(AST_IN);
            strncpy(in_node->in_stmt.var_name, "__temp_in", MAX_TOKEN_LEN - 1);
            node->array_assign.value = in_node;
            
            return node;
        }
        
        // Regular variable input
        ASTNode *node = create_node(AST_IN);
        strncpy(node->in_stmt.var_name, var_name, MAX_TOKEN_LEN - 1);
        expect(TOK_RPAREN);
        expect(TOK_SEMI);
        return node;
    }
    
    // If statement
    if (current_token.type == TOK_IF) {
        next_token();
        expect(TOK_LPAREN);
        
        ASTNode *node = create_node(AST_IF);
        node->if_stmt.condition = parse_expression();
        expect(TOK_RPAREN);
        
        node->if_stmt.then_block = parse_block();
        
        if (current_token.type == TOK_COLON) {
            next_token();
            node->if_stmt.else_block = parse_block();
        } else {
            node->if_stmt.else_block = NULL;
        }
        
        expect(TOK_SEMI);
        return node;
    }
    
    // For loop
    if (current_token.type == TOK_FOR) {
        next_token();
        expect(TOK_LPAREN);
        
        ASTNode *node = create_node(AST_FOR);
        
        // Initialization
        if (current_token.type == TOK_IDENT) {
            char var_name[MAX_TOKEN_LEN];
            strncpy(var_name, current_token.ident, MAX_TOKEN_LEN - 1);
            next_token();
            
            if (current_token.type == TOK_ASSIGN) {
                next_token();
                ASTNode *assign = create_node(AST_ASSIGN);
                strncpy(assign->assign.var_name, var_name, MAX_TOKEN_LEN - 1);
                assign->assign.value = parse_expression();
                node->for_stmt.init = assign;
            } else {
                node->for_stmt.init = NULL;
            }
        } else {
            node->for_stmt.init = NULL;
        }
        
        expect(TOK_SEMI);
        
        // Condition
        node->for_stmt.condition = parse_expression();
        expect(TOK_SEMI);
        
        // Increment
        if (current_token.type == TOK_IDENT) {
            char var_name[MAX_TOKEN_LEN];
            strncpy(var_name, current_token.ident, MAX_TOKEN_LEN - 1);
            next_token();
            
            if (current_token.type == TOK_PLUSPLUS) {
                next_token();
                ASTNode *inc = create_node(AST_INCREMENT);
                strncpy(inc->inc_dec.var_name, var_name, MAX_TOKEN_LEN - 1);
                node->for_stmt.increment = inc;
            } else if (current_token.type == TOK_MINUSMINUS) {
                next_token();
                ASTNode *dec = create_node(AST_DECREMENT);
                strncpy(dec->inc_dec.var_name, var_name, MAX_TOKEN_LEN - 1);
                node->for_stmt.increment = dec;
            } else if (current_token.type == TOK_ASSIGN) {
                next_token();
                ASTNode *assign = create_node(AST_ASSIGN);
                strncpy(assign->assign.var_name, var_name, MAX_TOKEN_LEN - 1);
                assign->assign.value = parse_expression();
                node->for_stmt.increment = assign;
            } else {
                node->for_stmt.increment = NULL;
            }
        } else {
            node->for_stmt.increment = NULL;
        }
        
        expect(TOK_RPAREN);
        
        // Body
        node->for_stmt.body = parse_block();
        expect(TOK_SEMI);
        
        return node;
    }
    
    // While loop
    if (current_token.type == TOK_WHILE) {
        next_token();
        expect(TOK_LPAREN);
        
        ASTNode *node = create_node(AST_WHILE);
        node->while_stmt.condition = parse_expression();
        expect(TOK_RPAREN);
        node->while_stmt.body = parse_block();
        expect(TOK_SEMI);
        
        return node;
    }
    
    // HTTP request
    if (current_token.type == TOK_HTTP) {
        next_token();
        expect(TOK_LPAREN);
        
        ASTNode *node = create_node(AST_HTTP);
        
        // Method
        node->http_stmt.method = parse_expression();
        expect(TOK_COMMA);
        
        // URL
        node->http_stmt.url = parse_expression();
        
        // Optional body
        if (current_token.type == TOK_COMMA) {
            next_token();
            node->http_stmt.body = parse_expression();
        } else {
            node->http_stmt.body = NULL;
        }
        
        expect(TOK_RPAREN);
        expect(TOK_SEMI);
        
        return node;
    }
    
    // Assignment or increment/decrement
    if (current_token.type == TOK_IDENT) {
        char var_name[MAX_TOKEN_LEN];
        strncpy(var_name, current_token.ident, MAX_TOKEN_LEN - 1);
        next_token();
        
        // Array assignment
        if (current_token.type == TOK_LBRACKET) {
            next_token();
            ASTNode *index = parse_expression();
            expect(TOK_RBRACKET);
            expect(TOK_ASSIGN);
            
            ASTNode *node = create_node(AST_ARRAY_ASSIGN);
            strncpy(node->array_assign.var_name, var_name, MAX_TOKEN_LEN - 1);
            node->array_assign.index = index;
            node->array_assign.value = parse_expression();
            expect(TOK_SEMI);
            return node;
        }
        
        // Increment
        if (current_token.type == TOK_PLUSPLUS) {
            next_token();
            expect(TOK_SEMI);
            ASTNode *node = create_node(AST_INCREMENT);
            strncpy(node->inc_dec.var_name, var_name, MAX_TOKEN_LEN - 1);
            return node;
        }
        
        // Decrement
        if (current_token.type == TOK_MINUSMINUS) {
            next_token();
            expect(TOK_SEMI);
            ASTNode *node = create_node(AST_DECREMENT);
            strncpy(node->inc_dec.var_name, var_name, MAX_TOKEN_LEN - 1);
            return node;
        }
        
        // Assignment
        if (current_token.type == TOK_ASSIGN) {
            next_token();
            ASTNode *node = create_node(AST_ASSIGN);
            strncpy(node->assign.var_name, var_name, MAX_TOKEN_LEN - 1);
            node->assign.value = parse_expression();
            expect(TOK_SEMI);
            return node;
        }
    }
    
    // Empty statement
    if (current_token.type == TOK_SEMI) {
        next_token();
        return NULL;
    }
    
    report_error("Invalid statement");
    next_token(); // Try to recover
    return NULL;
}

/* ==================== HTTP HELPER ==================== */
#ifdef _WIN32
static void http_request_win(const char *method, const char *url, const char *body) {
    // Parse URL: https://host/path veya http://host/path
    bool is_https = (strncmp(url, "https://", 8) == 0);
    bool is_http = (strncmp(url, "http://", 7) == 0);
    
    if (!is_https && !is_http) {
        report_error("URL must start with http:// or https://");
        return;
    }
    
    const char *host_start = is_https ? (url + 8) : (url + 7);
    const char *path_start = strchr(host_start, '/');
    
    wchar_t host[256];
    wchar_t path[1024];
    wchar_t method_w[16];
    
    // Host parse
    int host_len = path_start ? (path_start - host_start) : strlen(host_start);
    MultiByteToWideChar(CP_UTF8, 0, host_start, host_len, host, 256);
    host[host_len] = 0;
    
    // Path parse
    if (path_start) {
        MultiByteToWideChar(CP_UTF8, 0, path_start, -1, path, 1024);
    } else {
        wcscpy(path, L"/");
    }
    
    // Method convert
    MultiByteToWideChar(CP_UTF8, 0, method, -1, method_w, 16);
    
    // WinHTTP session
    HINTERNET session = WinHttpOpen(
        L"NaC/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    
    if (!session) {
        report_error("HTTP: Failed to open session");
        return;
    }
    
    INTERNET_PORT port = is_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET connect = WinHttpConnect(session, host, port, 0);
    
    if (!connect) {
        report_error("HTTP: Failed to connect");
        WinHttpCloseHandle(session);
        return;
    }
    
    DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connect,
        method_w,
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    
    if (!request) {
        report_error("HTTP: Failed to open request");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return;
    }
    
    // Redirect support
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    
    // Send request
    const wchar_t *headers = body ? L"Content-Type: application/json\r\n" : NULL;
    BOOL result = WinHttpSendRequest(
        request,
        headers,
        headers ? -1 : 0,
        (LPVOID)body,
        body ? (DWORD)strlen(body) : 0,
        body ? (DWORD)strlen(body) : 0,
        0
    );
    
    if (!result) {
        report_error("HTTP: Failed to send request");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return;
    }
    
    WinHttpReceiveResponse(request, NULL);
    
    // Read response
    DWORD size = 0;
    do {
        DWORD downloaded = 0;
        WinHttpQueryDataAvailable(request, &size);
        if (!size) break;

        char *buffer = (char*)malloc(size + 1);
        WinHttpReadData(request, buffer, size, &downloaded);
        buffer[downloaded] = 0;
        printf("%s", buffer);
        free(buffer);
    } while (size > 0);
    
    printf("\n");
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}
#else
// libcurl callback function
static size_t curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    printf("%.*s", (int)total_size, (char*)contents);
    return total_size;
}

static void http_request_unix(const char *method, const char *url, const char *body) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        report_error("HTTP: Failed to initialize curl");
        return;
    }
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url);
    
    // Set method
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            
            // Set JSON header
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
    } else if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    // Set user agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NaC/1.0");
    
    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    // Set write callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "HTTP: %s", curl_easy_strerror(res));
        report_error(error_msg);
    } else {
        printf("\n");
    }
    
    curl_easy_cleanup(curl);
}
#endif

/* ==================== BUILT-IN FUNCTIONS ==================== */
static bool is_builtin_function(const char *name) {
    const char *builtins[] = {
        "sqrt", "pow", "sin", "cos", "tan", "abs", "floor", "ceil", "round", "log", "exp",
        "length", "upper", "lower", "push", "pop",
        "trim", "replace", "substr", "indexOf",
        "first", "last", "reverse", "slice", "join",
        "read", "write", "append"
    };
    int builtin_count = sizeof(builtins) / sizeof(builtins[0]);
    for (int i = 0; i < builtin_count; i++) {
        if (strcmp(name, builtins[i]) == 0) {
            return true;
        }
    }
    return false;
}

static Value call_builtin_function(const char *name, Value *args, int arg_count) {
    // Math functions
    if (strcmp(name, "sqrt") == 0) {
        if (arg_count != 1) {
            report_error("sqrt() requires 1 argument");
            return make_float(0.0);
        }
        double val = to_float(args[0]);
        if (val < 0) {
            report_error("sqrt() of negative number");
            return make_float(0.0);
        }
        return make_float(sqrt(val));
    }
    
    if (strcmp(name, "pow") == 0) {
        if (arg_count != 2) {
            report_error("pow() requires 2 arguments");
            return make_float(0.0);
        }
        return make_float(pow(to_float(args[0]), to_float(args[1])));
    }
    
    if (strcmp(name, "sin") == 0) {
        if (arg_count != 1) {
            report_error("sin() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(sin(to_float(args[0])));
    }
    
    if (strcmp(name, "cos") == 0) {
        if (arg_count != 1) {
            report_error("cos() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(cos(to_float(args[0])));
    }
    
    if (strcmp(name, "tan") == 0) {
        if (arg_count != 1) {
            report_error("tan() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(tan(to_float(args[0])));
    }
    
    if (strcmp(name, "abs") == 0) {
        if (arg_count != 1) {
            report_error("abs() requires 1 argument");
            return make_float(0.0);
        }
        double val = to_float(args[0]);
        return (args[0].type == TYPE_INT) ? make_int(abs(to_int(args[0]))) : make_float(fabs(val));
    }
    
    if (strcmp(name, "floor") == 0) {
        if (arg_count != 1) {
            report_error("floor() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(floor(to_float(args[0])));
    }
    
    if (strcmp(name, "ceil") == 0) {
        if (arg_count != 1) {
            report_error("ceil() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(ceil(to_float(args[0])));
    }
    
    if (strcmp(name, "round") == 0) {
        if (arg_count != 1) {
            report_error("round() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(round(to_float(args[0])));
    }
    
    if (strcmp(name, "log") == 0) {
        if (arg_count != 1) {
            report_error("log() requires 1 argument");
            return make_float(0.0);
        }
        double val = to_float(args[0]);
        if (val <= 0) {
            report_error("log() of non-positive number");
            return make_float(0.0);
        }
        return make_float(log(val));
    }
    
    if (strcmp(name, "exp") == 0) {
        if (arg_count != 1) {
            report_error("exp() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(exp(to_float(args[0])));
    }
    
    // String functions
    if (strcmp(name, "length") == 0) {
        if (arg_count != 1) {
            report_error("length() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type == TYPE_STRING) {
            return make_int(strlen(args[0].str_val));
        } else if (args[0].type == TYPE_ARRAY) {
            return make_int(args[0].array_val.size);
        }
        return make_int(0);
    }
    
    if (strcmp(name, "upper") == 0) {
        if (arg_count != 1) {
            report_error("upper() requires 1 argument");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("upper() requires a string");
            return make_string("");
        }
        char result[MAX_STRING_LEN];
        strncpy(result, args[0].str_val, MAX_STRING_LEN - 1);
        for (int i = 0; result[i]; i++) {
            result[i] = toupper(result[i]);
        }
        return make_string(result);
    }
    
    if (strcmp(name, "lower") == 0) {
        if (arg_count != 1) {
            report_error("lower() requires 1 argument");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("lower() requires a string");
            return make_string("");
        }
        char result[MAX_STRING_LEN];
        strncpy(result, args[0].str_val, MAX_STRING_LEN - 1);
        for (int i = 0; result[i]; i++) {
            result[i] = tolower(result[i]);
        }
        return make_string(result);
    }
    
    if (strcmp(name, "trim") == 0) {
        if (arg_count != 1) {
            report_error("trim() requires 1 argument");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("trim() requires a string");
            return make_string("");
        }
        const char *str = args[0].str_val;
        // Find first non-whitespace
        int start = 0;
        while (str[start] && isspace(str[start])) start++;
        // Find last non-whitespace
        int end = strlen(str) - 1;
        while (end >= start && isspace(str[end])) end--;
        
        char result[MAX_STRING_LEN];
        if (end < start) {
            result[0] = '\0';
        } else {
            int len = end - start + 1;
            strncpy(result, str + start, len);
            result[len] = '\0';
        }
        return make_string(result);
    }
    
    if (strcmp(name, "replace") == 0) {
        if (arg_count != 3) {
            report_error("replace() requires 3 arguments (string, old, new)");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING || args[1].type != TYPE_STRING || args[2].type != TYPE_STRING) {
            report_error("replace() requires string arguments");
            return make_string("");
        }
        
        const char *str = args[0].str_val;
        const char *old_substr = args[1].str_val;
        const char *new_substr = args[2].str_val;
        
        char result[MAX_STRING_LEN] = "";
        int old_len = strlen(old_substr);
        int new_len = strlen(new_substr);
        
        const char *p = str;
        while (*p) {
            if (strncmp(p, old_substr, old_len) == 0) {
                strncat(result, new_substr, MAX_STRING_LEN - strlen(result) - 1);
                p += old_len;
            } else {
                int len = strlen(result);
                if (len < MAX_STRING_LEN - 1) {
                    result[len] = *p;
                    result[len + 1] = '\0';
                }
                p++;
            }
        }
        return make_string(result);
    }
    
    if (strcmp(name, "substr") == 0) {
        if (arg_count != 3) {
            report_error("substr() requires 3 arguments (string, start, length)");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("substr() requires a string as first argument");
            return make_string("");
        }
        
        const char *str = args[0].str_val;
        int start = to_int(args[1]);
        int len = to_int(args[2]);
        int str_len = strlen(str);
        
        if (start < 0 || start >= str_len || len < 0) {
            return make_string("");
        }
        
        if (start + len > str_len) {
            len = str_len - start;
        }
        
        char result[MAX_STRING_LEN];
        strncpy(result, str + start, len);
        result[len] = '\0';
        return make_string(result);
    }
    
    if (strcmp(name, "indexOf") == 0) {
        if (arg_count != 2) {
            report_error("indexOf() requires 2 arguments (string, substring)");
            return make_int(-1);
        }
        if (args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("indexOf() requires string arguments");
            return make_int(-1);
        }
        
        const char *str = args[0].str_val;
        const char *substr = args[1].str_val;
        const char *p = strstr(str, substr);
        
        if (p) {
            return make_int(p - str);
        }
        return make_int(-1);
    }
    
    // Array functions
    if (strcmp(name, "first") == 0) {
        if (arg_count != 1) {
            report_error("first() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type != TYPE_ARRAY || args[0].array_val.size == 0) {
            report_error("first() on non-array or empty array");
            return make_int(0);
        }
        return args[0].array_val.elements[0];
    }
    
    if (strcmp(name, "last") == 0) {
        if (arg_count != 1) {
            report_error("last() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type != TYPE_ARRAY || args[0].array_val.size == 0) {
            report_error("last() on non-array or empty array");
            return make_int(0);
        }
        return args[0].array_val.elements[args[0].array_val.size - 1];
    }
    
    if (strcmp(name, "reverse") == 0) {
        if (arg_count != 1) {
            report_error("reverse() requires 1 argument");
            return make_array(0);
        }
        if (args[0].type != TYPE_ARRAY) {
            report_error("reverse() requires an array");
            return make_array(0);
        }
        
        Value arr = copy_value(args[0]);
        // Reverse in place
        for (int i = 0; i < arr.array_val.size / 2; i++) {
            int j = arr.array_val.size - 1 - i;
            Value temp = arr.array_val.elements[i];
            arr.array_val.elements[i] = arr.array_val.elements[j];
            arr.array_val.elements[j] = temp;
        }
        return arr;
    }
    
    if (strcmp(name, "slice") == 0) {
        if (arg_count != 3) {
            report_error("slice() requires 3 arguments (array, start, end)");
            return make_array(0);
        }
        if (args[0].type != TYPE_ARRAY) {
            report_error("slice() requires an array");
            return make_array(0);
        }
        
        int start = to_int(args[1]);
        int end = to_int(args[2]);
        int size = args[0].array_val.size;
        
        if (start < 0) start = 0;
        if (end > size) end = size;
        if (start > end) start = end;
        
        int new_size = end - start;
        Value result = make_array(new_size);
        for (int i = 0; i < new_size; i++) {
            result.array_val.elements[i] = copy_value(args[0].array_val.elements[start + i]);
        }
        return result;
    }
    
    if (strcmp(name, "join") == 0) {
        if (arg_count != 2) {
            report_error("join() requires 2 arguments (array, separator)");
            return make_string("");
        }
        if (args[0].type != TYPE_ARRAY || args[1].type != TYPE_STRING) {
            report_error("join() requires an array and string separator");
            return make_string("");
        }
        
        char result[MAX_STRING_LEN] = "";
        const char *sep = args[1].str_val;
        int sep_len = strlen(sep);
        
        for (int i = 0; i < args[0].array_val.size; i++) {
            if (i > 0) {
                strncat(result, sep, MAX_STRING_LEN - strlen(result) - 1);
            }
            
            Value elem = args[0].array_val.elements[i];
            char elem_str[64];
            if (elem.type == TYPE_INT) {
                snprintf(elem_str, sizeof(elem_str), "%d", elem.int_val);
            } else if (elem.type == TYPE_FLOAT) {
                snprintf(elem_str, sizeof(elem_str), "%g", elem.float_val);
            } else if (elem.type == TYPE_STRING) {
                strncpy(elem_str, elem.str_val, sizeof(elem_str) - 1);
            } else {
                elem_str[0] = '\0';
            }
            strncat(result, elem_str, MAX_STRING_LEN - strlen(result) - 1);
        }
        
        return make_string(result);
    }
    
    // File I/O functions
    if (strcmp(name, "read") == 0) {
        if (arg_count != 1) {
            report_error("read() requires 1 argument (filename)");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("read() requires a string filename");
            return make_string("");
        }
        
        const char *filename = args[0].str_val;
        FILE *f = fopen(filename, "rb");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot open file for reading: %s", filename);
            report_error(msg);
            return make_string("");
        }
        
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (size > MAX_STRING_LEN - 1) {
            size = MAX_STRING_LEN - 1;
        }
        
        char buffer[MAX_STRING_LEN];
        fread(buffer, 1, size, f);
        buffer[size] = '\0';
        fclose(f);
        
        return make_string(buffer);
    }
    
    if (strcmp(name, "write") == 0) {
        if (arg_count != 2) {
            report_error("write() requires 2 arguments (filename, content)");
            return make_int(0);
        }
        if (args[0].type != TYPE_STRING) {
            report_error("write() requires a string filename");
            return make_int(0);
        }
        
        const char *filename = args[0].str_val;
        const char *content = "";
        
        if (args[1].type == TYPE_STRING) {
            content = args[1].str_val;
        } else {
            // Convert to string
            static char temp_str[64];
            if (args[1].type == TYPE_INT) {
                snprintf(temp_str, sizeof(temp_str), "%d", args[1].int_val);
            } else if (args[1].type == TYPE_FLOAT) {
                snprintf(temp_str, sizeof(temp_str), "%g", args[1].float_val);
            }
            content = temp_str;
        }
        
        FILE *f = fopen(filename, "wb");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot open file for writing: %s", filename);
            report_error(msg);
            return make_int(0);
        }
        
        fwrite(content, 1, strlen(content), f);
        fclose(f);
        
        return make_int(strlen(content));
    }
    
    if (strcmp(name, "append") == 0) {
        if (arg_count != 2) {
            report_error("append() requires 2 arguments (filename, content)");
            return make_int(0);
        }
        if (args[0].type != TYPE_STRING) {
            report_error("append() requires a string filename");
            return make_int(0);
        }
        
        const char *filename = args[0].str_val;
        const char *content = "";
        
        if (args[1].type == TYPE_STRING) {
            content = args[1].str_val;
        } else {
            // Convert to string
            static char temp_str[64];
            if (args[1].type == TYPE_INT) {
                snprintf(temp_str, sizeof(temp_str), "%d", args[1].int_val);
            } else if (args[1].type == TYPE_FLOAT) {
                snprintf(temp_str, sizeof(temp_str), "%g", args[1].float_val);
            }
            content = temp_str;
        }
        
        FILE *f = fopen(filename, "ab");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot open file for appending: %s", filename);
            report_error(msg);
            return make_int(0);
        }
        
        fwrite(content, 1, strlen(content), f);
        fclose(f);
        
        return make_int(strlen(content));
    }

    if (strcmp(name, "push") == 0) {
        if (arg_count != 2) {
            report_error("push() requires 2 arguments (array, value)");
            return make_int(0);
        }
        // Note: This needs to modify the original array, which requires special handling
        // For now, return the array size
        return make_int(args[0].array_val.size);
    }
    
    if (strcmp(name, "pop") == 0) {
        if (arg_count != 1) {
            report_error("pop() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type != TYPE_ARRAY || args[0].array_val.size == 0) {
            report_error("pop() on empty array");
            return make_int(0);
        }
        return args[0].array_val.elements[args[0].array_val.size - 1];
    }
    
    // Function not found - shouldn't reach here if is_builtin_function is checked first
    report_error("Unknown built-in function");
    return make_int(0);
}

/* ==================== EVALUATOR ==================== */
static Value eval_node(ASTNode *node) {
    if (!node) return make_int(0);
    
    switch (node->type) {
        case AST_INT_LITERAL:
            return make_int(node->int_val);
            
        case AST_FLOAT_LITERAL:
            return make_float(node->float_val);
            
        case AST_STRING_LITERAL:
            return make_string(node->str_val);
            
        case AST_VARIABLE: {
            Value *v = get_var(node->var_name);
            if (!v) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable: %s", node->var_name);
                report_error(msg);
                return make_int(0);
            }
            return *v;
        }
        
        case AST_ARRAY_ACCESS: {
            Value *arr = get_var(node->array_access.var_name);
            if (!arr) {
                report_error("Undefined array variable");
                return make_int(0);
            }
            if (arr->type != TYPE_ARRAY) {
                report_error("Variable is not an array");
                return make_int(0);
            }
            
            Value idx_val = eval_node(node->array_access.index);
            int idx = to_int(idx_val);
            
            if (idx < 0 || idx >= arr->array_val.size) {
                report_error("Array index out of bounds");
                return make_int(0);
            }
            
            return arr->array_val.elements[idx];
        }
        
        case AST_BINARY_OP: {
            Value left = eval_node(node->binary.left);
            Value right = eval_node(node->binary.right);
            
            switch (node->binary.op) {
                case TOK_PLUS:
                    if (left.type == TYPE_STRING || right.type == TYPE_STRING) {
                        char result[MAX_STRING_LEN];
                        if (left.type == TYPE_STRING) {
                            strncpy(result, left.str_val, MAX_STRING_LEN - 1);
                        } else {
                            snprintf(result, MAX_STRING_LEN, "%g", to_float(left));
                        }
                        
                        int len = strlen(result);
                        if (right.type == TYPE_STRING) {
                            strncat(result, right.str_val, MAX_STRING_LEN - len - 1);
                        } else {
                            char temp[64];
                            snprintf(temp, sizeof(temp), "%g", to_float(right));
                            strncat(result, temp, MAX_STRING_LEN - len - 1);
                        }
                        return make_string(result);
                    }
                    if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
                        return make_float(to_float(left) + to_float(right));
                    }
                    return make_int(to_int(left) + to_int(right));
                    
                case TOK_MINUS:
                    if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
                        return make_float(to_float(left) - to_float(right));
                    }
                    return make_int(to_int(left) - to_int(right));
                    
                case TOK_STAR:
                    if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
                        return make_float(to_float(left) * to_float(right));
                    }
                    return make_int(to_int(left) * to_int(right));
                    
                case TOK_SLASH:
                    if (to_float(right) == 0) {
                        report_error("Division by zero");
                        return make_int(0);
                    }
                    if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
                        return make_float(to_float(left) / to_float(right));
                    }
                    return make_int(to_int(left) / to_int(right));
                    
                case TOK_PERCENT:
                    if (to_int(right) == 0) {
                        report_error("Modulo by zero");
                        return make_int(0);
                    }
                    return make_int(to_int(left) % to_int(right));
                    
                case TOK_EQ:
                    return make_int(to_float(left) == to_float(right));
                case TOK_NEQ:
                    return make_int(to_float(left) != to_float(right));
                case TOK_LT:
                    return make_int(to_float(left) < to_float(right));
                case TOK_GT:
                    return make_int(to_float(left) > to_float(right));
                case TOK_LTE:
                    return make_int(to_float(left) <= to_float(right));
                case TOK_GTE:
                    return make_int(to_float(left) >= to_float(right));
                case TOK_AND:
                    return make_int(to_bool(left) && to_bool(right));
                case TOK_OR:
                    return make_int(to_bool(left) || to_bool(right));
                    
                default:
                    report_error("Unknown binary operator");
                    return make_int(0);
            }
        }
        
        case AST_UNARY_OP: {
            Value operand = eval_node(node->unary.operand);
            switch (node->unary.op) {
                case TOK_MINUS:
                    if (operand.type == TYPE_FLOAT) {
                        return make_float(-operand.float_val);
                    }
                    return make_int(-to_int(operand));
                case TOK_NOT:
                    return make_int(!to_bool(operand));
                default:
                    return make_int(0);
            }
        }
        
        case AST_ASSIGN: {
            Value val = eval_node(node->assign.value);
            set_var(node->assign.var_name, val);
            return val;
        }
        
        case AST_ARRAY_ASSIGN: {
            Value *arr = get_var(node->array_assign.var_name);
            if (!arr || arr->type != TYPE_ARRAY) {
                report_error("Variable is not an array");
                return make_int(0);
            }
            
            Value idx_val = eval_node(node->array_assign.index);
            int idx = to_int(idx_val);
            
            if (idx < 0 || idx >= arr->array_val.size) {
                report_error("Array index out of bounds");
                return make_int(0);
            }
            
            Value val = eval_node(node->array_assign.value);
            free_value(&arr->array_val.elements[idx]);
            arr->array_val.elements[idx] = copy_value(val);
            return val;
        }
        
        case AST_CALL: {
            // Evaluate arguments FIRST (before any function lookup)
            Value *arg_values = (Value*)malloc(sizeof(Value) * node->call.arg_count);
            for (int i = 0; i < node->call.arg_count; i++) {
                arg_values[i] = eval_node(node->call.args[i]);
            }
            
            // Check for built-in functions first
            if (is_builtin_function(node->call.func_name)) {
                Value result = call_builtin_function(node->call.func_name, arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }
            
            // Find user-defined function
            Function *func = NULL;
            for (int i = 0; i < func_count; i++) {
                if (strcmp(functions[i].name, node->call.func_name) == 0) {
                    func = &functions[i];
                    break;
                }
            }
            
            if (!func) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined function: %s", node->call.func_name);
                report_error(msg);
                free(arg_values);
                return make_int(0);
            }
            
            if (node->call.arg_count != func->param_count) {
                report_error("Argument count mismatch");
                free(arg_values);
                return make_int(0);
            }
            
            // Create new scope
            if (call_depth >= MAX_CALL_DEPTH) {
                free(arg_values);
                report_error("Stack overflow");
                return make_int(0);
            }
            
            call_stack_vars[call_depth] = create_var_table();
            call_depth++;
            
            // Bind parameters in new scope
            for (int i = 0; i < func->param_count; i++) {
                set_var(func->params[i], arg_values[i]);
            }
            free(arg_values);
            
            // Execute function
            should_return = false;
            eval_node(func->body);
            
            Value result = return_value;
            should_return = false;
            
            // Clean up scope
            call_depth--;
            free_var_table(call_stack_vars[call_depth]);
            call_stack_vars[call_depth] = NULL;
            
            return result;
        }
        
        case AST_BLOCK: {
            for (int i = 0; i < node->block.count; i++) {
                if (should_break || should_continue || should_return) break;
                eval_node(node->block.statements[i]);
            }
            return make_int(0);
        }
        
        case AST_IF: {
            Value condition = eval_node(node->if_stmt.condition);
            if (to_bool(condition)) {
                eval_node(node->if_stmt.then_block);
            } else if (node->if_stmt.else_block) {
                eval_node(node->if_stmt.else_block);
            }
            return make_int(0);
        }
        
        case AST_FOR: {
            // Initialize
            if (node->for_stmt.init) {
                eval_node(node->for_stmt.init);
            }
            
            // Loop
            while (1) {
                Value condition = eval_node(node->for_stmt.condition);
                if (!to_bool(condition)) break;
                
                should_continue = false;
                eval_node(node->for_stmt.body);
                
                if (should_break) {
                    should_break = false;
                    break;
                }
                if (should_return) break;
                
                if (node->for_stmt.increment) {
                    eval_node(node->for_stmt.increment);
                }
            }
            
            should_continue = false;
            return make_int(0);
        }
        
        case AST_WHILE: {
            while (1) {
                Value condition = eval_node(node->while_stmt.condition);
                if (!to_bool(condition)) break;
                
                should_continue = false;
                eval_node(node->while_stmt.body);
                
                if (should_break) {
                    should_break = false;
                    break;
                }
                if (should_return) break;
            }
            
            should_continue = false;
            return make_int(0);
        }
        
        case AST_HTTP: {
            Value method_val = eval_node(node->http_stmt.method);
            Value url_val = eval_node(node->http_stmt.url);
            
            if (method_val.type != TYPE_STRING || url_val.type != TYPE_STRING) {
                report_error("http() requires string arguments");
                return make_int(0);
            }
            
            const char *body_str = NULL;
            if (node->http_stmt.body) {
                Value body_val = eval_node(node->http_stmt.body);
                if (body_val.type == TYPE_STRING) {
                    body_str = body_val.str_val;
                }
            }
            
            #ifdef _WIN32
                http_request_win(method_val.str_val, url_val.str_val, body_str);
            #else
                http_request_unix(method_val.str_val, url_val.str_val, body_str);
            #endif
            
            return make_int(0);
        }
        
        case AST_RETURN: {
            Value val = eval_node(node->return_stmt.value);
            return_value = copy_value(val);  // CRITICAL: Deep copy for arrays!
            should_return = true;
            return return_value;
        }
        
        case AST_BREAK:
            should_break = true;
            return make_int(0);
            
        case AST_CONTINUE:
            should_continue = true;
            return make_int(0);
            
        case AST_OUT: {
            Value val = eval_node(node->out_stmt.value);
            print_value(val);
            return make_int(0);
        }
        
        case AST_IN: {
            char input[MAX_STRING_LEN];
            if (fgets(input, MAX_STRING_LEN, stdin)) {
                // Remove newline
                input[strcspn(input, "\n")] = '\0';
                
                // Try to parse as number
                char *endptr;
                long int_val = strtol(input, &endptr, 10);
                Value result;
                if (*endptr == '\0') {
                    result = make_int(int_val);
                } else {
                    double float_val = strtod(input, &endptr);
                    if (*endptr == '\0') {
                        result = make_float(float_val);
                    } else {
                        result = make_string(input);
                    }
                }
                
                // Only set variable if not __temp_in (used for array element input)
                if (strcmp(node->in_stmt.var_name, "__temp_in") != 0) {
                    set_var(node->in_stmt.var_name, result);
                }
                
                return result;
            }
            return make_int(0);
        }
        
        case AST_INCREMENT: {
            Value *v = get_var(node->inc_dec.var_name);
            if (!v) {
                report_error("Undefined variable");
                return make_int(0);
            }
            if (v->type == TYPE_FLOAT) {
                Value new_val = make_float(v->float_val + 1);
                set_var(node->inc_dec.var_name, new_val);
            } else {
                Value new_val = make_int(to_int(*v) + 1);
                set_var(node->inc_dec.var_name, new_val);
            }
            return make_int(0);
        }
        
        case AST_DECREMENT: {
            Value *v = get_var(node->inc_dec.var_name);
            if (!v) {
                report_error("Undefined variable");
                return make_int(0);
            }
            if (v->type == TYPE_FLOAT) {
                Value new_val = make_float(v->float_val - 1);
                set_var(node->inc_dec.var_name, new_val);
            } else {
                Value new_val = make_int(to_int(*v) - 1);
                set_var(node->inc_dec.var_name, new_val);
            }
            return make_int(0);
        }
        
        case AST_ARRAY_LITERAL: {
            if (node->array_literal.count == 1) {
                // array(n) syntax
                Value size_val = eval_node(node->array_literal.elements[0]);
                int size = to_int(size_val);
                if (size < 0 || size > MAX_ARRAY_SIZE) {
                    report_error("Invalid array size");
                    return make_int(0);
                }
                return make_array(size);
            } else {
                // [a, b, c] syntax
                Value arr = make_array(node->array_literal.count);
                for (int i = 0; i < node->array_literal.count; i++) {
                    arr.array_val.elements[i] = eval_node(node->array_literal.elements[i]);
                }
                return arr;
            }
        }
        
        default:
            return make_int(0);
    }
}

/* ==================== MAIN PROGRAM ==================== */
static char* read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
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
    global_vars = create_var_table();
    func_count = 0;
    call_depth = 0;
    should_break = false;
    should_continue = false;
    should_return = false;
    return_value = make_int(0);
    error_occurred = false;
    error_count = 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("NaC Language Interpreter v3.1.0 \n");
        printf("Usage: %s <file.nac>\n\n", argv[0]);
        return 1;
    }
    
    init_interpreter();
    
    code = read_file(argv[1]);
    code_len = strlen(code);
    pos = 0;
    
    next_token();
    
    // Parse and execute
    while (current_token.type != TOK_EOF) {
        ASTNode *stmt = parse_statement();
        if (stmt) {
            eval_node(stmt);
            free_ast(stmt);
        }
        
        // Stop if too many errors
        if (error_count > 10) {
            fprintf(stderr, "Too many errors, stopping execution.\n");
            break;
        }
    }
    
    free(code);
    free_var_table(global_vars);
    
    if (error_occurred) {
        fprintf(stderr, "\nExecution completed with %d error(s).\n", error_count);
        return 1;
    }
    
    return 0;
}