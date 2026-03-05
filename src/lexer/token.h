#ifndef NAC_TOKEN_H
#define NAC_TOKEN_H

#define MAX_TOKEN_LEN 256
#define MAX_STRING_LEN 1024

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

typedef struct {
    NaCTokenType type;
    int line;
    int col;
    union {
        int int_val;
        double float_val;
        char str_val[MAX_STRING_LEN];
        char ident[MAX_TOKEN_LEN];
    };
} Token;

#endif
