#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/interpreter.h"
#include "../lexer/lexer.h"
#include "../util/error.h"

static ASTNode *create_node(ASTNodeType type) {
    ASTNode *node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = type;
    return node;
}

void free_ast(ASTNode *node) {
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

static void expect(NaCTokenType type) {
    if (current_token.type != type) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Expected token type %d, got %d", type, current_token.type);
        report_error(msg);
        next_token();
    } else {
        next_token();
    }
}

static ASTNode *parse_primary(void) {
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

        if (current_token.type == TOK_LBRACKET) {
            next_token();
            ASTNode *index = parse_expression();
            expect(TOK_RBRACKET);
            node = create_node(AST_ARRAY_ACCESS);
            strncpy(node->array_access.var_name, name, MAX_TOKEN_LEN - 1);
            node->array_access.index = index;
            return node;
        }

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
    return create_node(AST_INT_LITERAL);
}

static ASTNode *parse_multiplicative(void) {
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

static ASTNode *parse_additive(void) {
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

static ASTNode *parse_comparison(void) {
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

static ASTNode *parse_logical(void) {
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

ASTNode *parse_expression(void) {
    return parse_logical();
}

static ASTNode *parse_block(void) {
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

ASTNode *parse_statement(void) {
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

        return NULL;
    }

    if (current_token.type == TOK_RN) {
        next_token();
        ASTNode *node = create_node(AST_RETURN);
        node->return_stmt.value = parse_expression();
        expect(TOK_SEMI);
        return node;
    }

    if (current_token.type == TOK_BREAK) {
        next_token();
        expect(TOK_SEMI);
        return create_node(AST_BREAK);
    }

    if (current_token.type == TOK_CONTINUE) {
        next_token();
        expect(TOK_SEMI);
        return create_node(AST_CONTINUE);
    }

    if (current_token.type == TOK_OUT) {
        next_token();
        expect(TOK_LPAREN);
        ASTNode *node = create_node(AST_OUT);
        node->out_stmt.value = parse_expression();
        expect(TOK_RPAREN);
        expect(TOK_SEMI);
        return node;
    }

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

        if (current_token.type == TOK_LBRACKET) {
            next_token();
            ASTNode *index = parse_expression();
            expect(TOK_RBRACKET);
            expect(TOK_RPAREN);
            expect(TOK_SEMI);

            ASTNode *node = create_node(AST_ARRAY_ASSIGN);
            strncpy(node->array_assign.var_name, var_name, MAX_TOKEN_LEN - 1);
            node->array_assign.index = index;

            ASTNode *in_node = create_node(AST_IN);
            strncpy(in_node->in_stmt.var_name, "__temp_in", MAX_TOKEN_LEN - 1);
            node->array_assign.value = in_node;

            return node;
        }

        ASTNode *node = create_node(AST_IN);
        strncpy(node->in_stmt.var_name, var_name, MAX_TOKEN_LEN - 1);
        expect(TOK_RPAREN);
        expect(TOK_SEMI);
        return node;
    }

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

    if (current_token.type == TOK_FOR) {
        next_token();
        expect(TOK_LPAREN);

        ASTNode *node = create_node(AST_FOR);

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

        node->for_stmt.condition = parse_expression();
        expect(TOK_SEMI);

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

        node->for_stmt.body = parse_block();
        expect(TOK_SEMI);

        return node;
    }

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

    if (current_token.type == TOK_HTTP) {
        next_token();
        expect(TOK_LPAREN);

        ASTNode *node = create_node(AST_HTTP);

        node->http_stmt.method = parse_expression();
        expect(TOK_COMMA);

        node->http_stmt.url = parse_expression();

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

    if (current_token.type == TOK_IDENT) {
        char var_name[MAX_TOKEN_LEN];
        strncpy(var_name, current_token.ident, MAX_TOKEN_LEN - 1);
        next_token();

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

        if (current_token.type == TOK_PLUSPLUS) {
            next_token();
            expect(TOK_SEMI);
            ASTNode *node = create_node(AST_INCREMENT);
            strncpy(node->inc_dec.var_name, var_name, MAX_TOKEN_LEN - 1);
            return node;
        }

        if (current_token.type == TOK_MINUSMINUS) {
            next_token();
            expect(TOK_SEMI);
            ASTNode *node = create_node(AST_DECREMENT);
            strncpy(node->inc_dec.var_name, var_name, MAX_TOKEN_LEN - 1);
            return node;
        }

        if (current_token.type == TOK_ASSIGN) {
            next_token();
            ASTNode *node = create_node(AST_ASSIGN);
            strncpy(node->assign.var_name, var_name, MAX_TOKEN_LEN - 1);
            node->assign.value = parse_expression();
            expect(TOK_SEMI);
            return node;
        }
    }

    if (current_token.type == TOK_SEMI) {
        next_token();
        return NULL;
    }

    report_error("Invalid statement");
    next_token();
    return NULL;
}
