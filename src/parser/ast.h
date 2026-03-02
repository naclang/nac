#ifndef NAC_AST_H
#define NAC_AST_H

#include "../lexer/token.h"

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

#endif
