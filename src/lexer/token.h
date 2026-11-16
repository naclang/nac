#ifndef NAC_TOKEN_H
#define NAC_TOKEN_H

#define MAX_TOKEN_LEN 256
#define MAX_STRING_LEN 8192

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
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_STAR_ASSIGN,
    TOK_SLASH_ASSIGN,
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
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_WHILE,
    TOK_ELSE,
    TOK_ELIF,
    TOK_DOT,
    TOK_ROUTE,
    TOK_BOOL,
    TOK_NULL,
    TOK_TRY,
    TOK_CATCH,
    TOK_THROW
} NaCTokenType;

typedef struct {
    NaCTokenType type;
    int line;
    int col;
    int nl_before; /* 1 if a newline separates this token from the previous one */
    int int_val;
    double float_val;
    char *str_val;              /* heap-allocated for TOK_STRING; NULL otherwise */
    char ident[MAX_TOKEN_LEN];
} Token;

#endif
