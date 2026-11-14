#ifndef NAC_AST_H
#define NAC_AST_H

#include "../lexer/token.h"

typedef enum {
    AST_INT_LITERAL,
    AST_FLOAT_LITERAL,
    AST_STRING_LITERAL,
    AST_VARIABLE,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_ASSIGN,
    AST_CALL,
    AST_BLOCK,
    AST_IF,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_OUT,
    AST_INCREMENT,
    AST_DECREMENT,
    AST_ARRAY_LITERAL,
    AST_WHILE,
    AST_BOOL_LITERAL,
    AST_NULL_LITERAL,
    AST_INDEX,
    AST_INDEX_ASSIGN,
    AST_MAP_LITERAL,
    AST_FOREACH,
    AST_FUNC_LITERAL,   /* fn(params) { ... } used as a value; var_name = internal function name */
    AST_HTTP_CALL,      /* http.get { ... }, http.post { ... }, ... */
    AST_TRY,            /* try { ... } catch e { ... } */
    AST_THROW           /* throw value */
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
            NaCTokenType compound_op; /* TOK_EOF = plain '=', else +=/-=/*=// desugars to var OP value */
        } assign;
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
            struct ASTNode *value;
        } return_stmt;
        struct {
            struct ASTNode *value;
        } out_stmt;
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
            struct ASTNode *object;
            struct ASTNode *index;
        } index;
        struct {
            struct ASTNode *target; /* an AST_INDEX node */
            struct ASTNode *value;
            NaCTokenType compound_op;
        } index_assign;
        struct {
            struct ASTNode **keys;
            struct ASTNode **values;
            int count;
        } map_literal;
        struct {
            struct ASTNode *try_block;
            char var_name[MAX_TOKEN_LEN];   /* catch variable, "" if none */
            struct ASTNode *catch_block;
        } try_stmt;
        struct {
            struct ASTNode *value;
        } throw_stmt;
        struct {
            char method[16];          /* "GET", "POST", ... */
            struct ASTNode *data;     /* the { ... } options map literal */
        } http_call;
        struct {
            char var_name[MAX_TOKEN_LEN];
            struct ASTNode *iterable;
            struct ASTNode *body;
        } foreach_stmt;
    };
} ASTNode;

#endif
