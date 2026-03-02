#ifndef NAC_PARSER_H
#define NAC_PARSER_H

#include "ast.h"

#define MAX_FUNCS 100
#define MAX_PARAMS 10

typedef struct {
    char name[MAX_TOKEN_LEN];
    char params[MAX_PARAMS][MAX_TOKEN_LEN];
    int param_count;
    ASTNode *body;
} Function;

ASTNode *parse_expression(void);
ASTNode *parse_statement(void);
void free_ast(ASTNode *node);

#endif
