#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/interpreter.h"
#include "../lexer/lexer.h"
#include "../net/router.h"
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
        case AST_INDEX:
            free_ast(node->index.object);
            free_ast(node->index.index);
            break;
        case AST_INDEX_ASSIGN:
            free_ast(node->index_assign.target);
            free_ast(node->index_assign.value);
            break;
        case AST_MAP_LITERAL:
            for (int i = 0; i < node->map_literal.count; i++) {
                free_ast(node->map_literal.keys[i]);
                free_ast(node->map_literal.values[i]);
            }
            free(node->map_literal.keys);
            free(node->map_literal.values);
            break;
        case AST_HTTP_CALL:
            free_ast(node->http_call.data);
            break;
        case AST_TRY:
            free_ast(node->try_stmt.try_block);
            free_ast(node->try_stmt.catch_block);
            break;
        case AST_THROW:
            free_ast(node->throw_stmt.value);
            break;
        case AST_FOREACH:
            free_ast(node->foreach_stmt.iterable);
            free_ast(node->foreach_stmt.body);
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

static int is_compound_assign(NaCTokenType type) {
    return type == TOK_PLUS_ASSIGN || type == TOK_MINUS_ASSIGN ||
           type == TOK_STAR_ASSIGN || type == TOK_SLASH_ASSIGN;
}

/* Maps +=/-=/*=// to the underlying arithmetic op (+, -, *, /) used to
 * desugar "x OP= y" into "x = x OP y" at eval time. */
static NaCTokenType compound_to_op(NaCTokenType type) {
    switch (type) {
        case TOK_PLUS_ASSIGN: return TOK_PLUS;
        case TOK_MINUS_ASSIGN: return TOK_MINUS;
        case TOK_STAR_ASSIGN: return TOK_STAR;
        case TOK_SLASH_ASSIGN: return TOK_SLASH;
        default: return TOK_EOF;
    }
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

static ASTNode *parse_primary(void);
static ASTNode *parse_atom(void);
static ASTNode *parse_block(void);

/* Statement terminator: ';' is accepted but optional. */
static void end_stmt(void) {
    if (current_token.type == TOK_SEMI) next_token();
}

/* Parses "(a, b, c)" into func->params. Positioned at '('. */
static void parse_param_list(Function *func) {
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
}

/* http.<verb> { ... } -- the verbs the HTTP client understands. Writes the
 * upper-case method name (e.g. "GET") into `method`. */
static int http_verb_to_method(const char *verb, char *method, size_t method_size) {
    static const char *verbs[] = { "get", "post", "put", "patch", "delete" };
    for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) {
        if (strcmp(verb, verbs[i]) == 0) {
            size_t j = 0;
            for (; verb[j] && j + 1 < method_size; j++) {
                method[j] = (char)(verb[j] - 'a' + 'A');
            }
            method[j] = '\0';
            return 1;
        }
    }
    return 0;
}

static ASTNode *parse_atom(void) {
    ASTNode *node = NULL;

    if (current_token.type == TOK_FN) {
        /* anonymous function used as a value:  fn(res) { ... }
         * It is registered like any other function under a generated
         * name, and the expression evaluates to a function value that
         * refers to it -- so it can be stored in a variable, put in a
         * map, or passed as a callback. */
        next_token();
        if (current_token.type != TOK_LPAREN) {
            report_error("Expected '(' after 'fn' (anonymous function: fn(a, b) { ... })");
            return create_node(AST_NULL_LITERAL);
        }
        if (func_count >= MAX_FUNCS) {
            report_error("Too many functions/routes");
            return create_node(AST_NULL_LITERAL);
        }

        Function *func = &functions[func_count++];
        snprintf(func->name, MAX_TOKEN_LEN, "__fn_%d", func_count);
        parse_param_list(func);
        func->body = parse_block();

        node = create_node(AST_FUNC_LITERAL);
        strncpy(node->var_name, func->name, MAX_TOKEN_LEN - 1);
        return node;
    }

    if (current_token.type == TOK_INT) {
        node = create_node(AST_INT_LITERAL);
        node->int_val = current_token.int_val;
        next_token();
        return node;
    }

    if (current_token.type == TOK_BOOL) {
        node = create_node(AST_BOOL_LITERAL);
        node->int_val = current_token.int_val;
        next_token();
        return node;
    }

    if (current_token.type == TOK_NULL) {
        node = create_node(AST_NULL_LITERAL);
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

        if (current_token.type == TOK_LPAREN && !current_token.nl_before) {
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

    if (current_token.type == TOK_LBRACE) {
        /* map literal:  { "name": "Ada", age: 36, "tags": [1, 2] } */
        next_token();
        node = create_node(AST_MAP_LITERAL);
        int capacity = 4;
        node->map_literal.keys = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
        node->map_literal.values = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
        node->map_literal.count = 0;

        while (current_token.type != TOK_RBRACE && current_token.type != TOK_EOF) {
            ASTNode *key;
            if (current_token.type == TOK_IDENT) {
                key = create_node(AST_STRING_LITERAL);
                strncpy(key->str_val, current_token.ident, MAX_TOKEN_LEN - 1);
                next_token();
            } else {
                key = parse_expression();
            }
            expect(TOK_COLON);
            ASTNode *value = parse_expression();

            if (node->map_literal.count >= capacity) {
                capacity *= 2;
                node->map_literal.keys = (ASTNode**)realloc(node->map_literal.keys, sizeof(ASTNode*) * capacity);
                node->map_literal.values = (ASTNode**)realloc(node->map_literal.values, sizeof(ASTNode*) * capacity);
            }
            node->map_literal.keys[node->map_literal.count] = key;
            node->map_literal.values[node->map_literal.count] = value;
            node->map_literal.count++;

            if (current_token.type == TOK_COMMA) {
                next_token();
            } else {
                break;
            }
        }
        expect(TOK_RBRACE);
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

/* Postfix chain on any primary: a[i], a[i][j], m["k"], m.k, m.k.j, f(x)[0]...
 * A '[' on a new line is NOT treated as an index (so a statement that
 * starts with '[' is never glued onto the previous line). */
static ASTNode *parse_primary(void) {
    ASTNode *node = parse_atom();

    for (;;) {
        if (current_token.type == TOK_LBRACKET && !current_token.nl_before) {
            next_token();
            ASTNode *idx = parse_expression();
            expect(TOK_RBRACKET);
            ASTNode *n = create_node(AST_INDEX);
            n->index.object = node;
            n->index.index = idx;
            node = n;
        } else if (current_token.type == TOK_DOT) {
            next_token();
            if (current_token.type != TOK_IDENT) {
                report_error("Expected a property name after '.'");
                break;
            }
            ASTNode *key = create_node(AST_STRING_LITERAL);
            strncpy(key->str_val, current_token.ident, MAX_TOKEN_LEN - 1);
            next_token();

            /* http.get { url: "..." }  /  http.post { ... } etc.: the
             * options map follows the verb directly, on the same line. */
            char http_method[16];
            if (node->type == AST_VARIABLE && strcmp(node->var_name, "http") == 0 &&
                http_verb_to_method(key->str_val, http_method, sizeof(http_method))) {
                if (current_token.type == TOK_LBRACE && !current_token.nl_before) {
                    ASTNode *call = create_node(AST_HTTP_CALL);
                    strncpy(call->http_call.method, http_method, sizeof(call->http_call.method) - 1);
                    call->http_call.data = parse_atom();   /* the { ... } map literal */
                    free_ast(node);
                    free_ast(key);
                    node = call;
                    continue;
                }
                report_error("http requests are written http.get { url: \"...\" } (options in braces)");
            }

            ASTNode *n = create_node(AST_INDEX);
            n->index.object = node;
            n->index.index = key;
            node = n;
        } else {
            break;
        }
    }
    return node;
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

    int negate = 0;
    if (current_token.type == TOK_NOT && peek_token_type(1) == TOK_IN) {
        negate = 1;            /* a not in b */
        next_token();
    }

    if (current_token.type == TOK_LT || current_token.type == TOK_GT ||
        current_token.type == TOK_LTE || current_token.type == TOK_GTE ||
        current_token.type == TOK_EQ || current_token.type == TOK_NEQ ||
        current_token.type == TOK_IN) {
        NaCTokenType op = current_token.type;
        next_token();
        ASTNode *right = parse_additive();

        ASTNode *node = create_node(AST_BINARY_OP);
        node->binary.op = op;
        node->binary.left = left;
        node->binary.right = right;
        left = node;

        if (negate) {
            ASTNode *n = create_node(AST_UNARY_OP);
            n->unary.op = TOK_NOT;
            n->unary.operand = left;
            left = n;
        }
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

/* Parses "(condition) { ... }" plus an optional else-tail. Assumes we're
 * already positioned right at the '(' of a condition (the leading 'if'
 * keyword, if any, has already been consumed by the caller). Handles its
 * own recursive ": else-if" chaining, so:
 *
 *   if (a) { ... } : (b) { ... } : (c) { ... } : { ... };
 *
 * works as a proper else-if chain. The 'if' keyword after ':' is entirely
 * optional -- "} : if (b) { ... }" and "} : (b) { ... }" parse identically,
 * since the '(' already makes it unambiguous that a condition follows. */
static ASTNode *parse_if_body(void) {
    ASTNode *node = create_node(AST_IF);
    node->if_stmt.condition = parse_expression();   /* parentheses are optional */
    node->if_stmt.then_block = parse_block();

    if (current_token.type == TOK_ELIF) {
        next_token();
        node->if_stmt.else_block = parse_if_body();
    } else if (current_token.type == TOK_ELSE) {
        next_token();
        if (current_token.type == TOK_IF) {
            next_token();
            node->if_stmt.else_block = parse_if_body();
        } else {
            node->if_stmt.else_block = parse_block();
        }
    } else {
        node->if_stmt.else_block = NULL;
    }

    return node;
}

static ASTNode *parse_if_tail(void) {
    next_token(); /* consume the leading 'if' */
    return parse_if_body();
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

        parse_param_list(func);
        func->body = parse_block();
        end_stmt();

        return NULL;
    }

    if (current_token.type == TOK_RN) {
        next_token();
        ASTNode *node = create_node(AST_RETURN);
        if (current_token.type == TOK_SEMI || current_token.type == TOK_RBRACE) {
            node->return_stmt.value = create_node(AST_NULL_LITERAL);
        } else {
            node->return_stmt.value = parse_expression();
        }
        end_stmt();
        return node;
    }

    if (current_token.type == TOK_TRY) {
        /* try { ... } catch e { ... }     (also: catch (e) { ... } or catch { ... }) */
        next_token();
        ASTNode *node = create_node(AST_TRY);
        node->try_stmt.try_block = parse_block();

        if (current_token.type != TOK_CATCH) {
            report_error("try needs a catch block: try { ... } catch e { ... }");
            return node;
        }
        next_token();

        int paren = 0;
        if (current_token.type == TOK_LPAREN) {
            paren = 1;
            next_token();
        }
        if (current_token.type == TOK_IDENT) {
            strncpy(node->try_stmt.var_name, current_token.ident, MAX_TOKEN_LEN - 1);
            next_token();
        }
        if (paren) expect(TOK_RPAREN);

        node->try_stmt.catch_block = parse_block();
        end_stmt();
        return node;
    }

    if (current_token.type == TOK_THROW) {
        next_token();
        ASTNode *node = create_node(AST_THROW);
        node->throw_stmt.value = parse_expression();
        end_stmt();
        return node;
    }

    if (current_token.type == TOK_BREAK) {
        next_token();
        end_stmt();
        return create_node(AST_BREAK);
    }

    if (current_token.type == TOK_CONTINUE) {
        next_token();
        end_stmt();
        return create_node(AST_CONTINUE);
    }

    if (current_token.type == TOK_OUT) {
        /* print(a, b, c) -- arguments are joined with single spaces */
        next_token();
        expect(TOK_LPAREN);
        ASTNode *node = create_node(AST_OUT);
        if (current_token.type == TOK_RPAREN) {
            node->out_stmt.value = create_node(AST_STRING_LITERAL);
        } else {
            ASTNode *value = parse_expression();
            while (current_token.type == TOK_COMMA) {
                next_token();
                ASTNode *space = create_node(AST_STRING_LITERAL);
                strcpy(space->str_val, " ");
                ASTNode *lhs = create_node(AST_BINARY_OP);
                lhs->binary.op = TOK_PLUS;
                lhs->binary.left = value;
                lhs->binary.right = space;
                ASTNode *rhs = parse_expression();
                ASTNode *sum = create_node(AST_BINARY_OP);
                sum->binary.op = TOK_PLUS;
                sum->binary.left = lhs;
                sum->binary.right = rhs;
                value = sum;
            }
            node->out_stmt.value = value;
        }
        expect(TOK_RPAREN);
        end_stmt();
        return node;
    }

    if (current_token.type == TOK_IF) {
        ASTNode *node = parse_if_tail();
        end_stmt();
        return node;
    }

    if (current_token.type == TOK_FOR) {
        /* for item in list { ... }      (also: for (item in list) { ... }) */
        next_token();
        int paren = 0;
        if (current_token.type == TOK_LPAREN) {
            paren = 1;
            next_token();
        }
        if (current_token.type != TOK_IDENT || peek_token_type(1) != TOK_IN) {
            report_error("Expected: for item in list { ... }");
            return NULL;
        }
        ASTNode *node = create_node(AST_FOREACH);
        strncpy(node->foreach_stmt.var_name, current_token.ident, MAX_TOKEN_LEN - 1);
        next_token();   /* name */
        next_token();   /* 'in' */
        node->foreach_stmt.iterable = parse_expression();
        if (paren) expect(TOK_RPAREN);
        node->foreach_stmt.body = parse_block();
        end_stmt();
        return node;
    }

    if (current_token.type == TOK_WHILE) {
        next_token();
        ASTNode *node = create_node(AST_WHILE);
        node->while_stmt.condition = parse_expression();   /* parentheses optional */
        node->while_stmt.body = parse_block();
        end_stmt();

        return node;
    }

    if (current_token.type == TOK_ROUTE) {
        /* route GET "/users/:id" { ... };  -- sugar for a handler function
         * with the fixed signature (method, path, query, headers, body,
         * params, cookie) that is registered in the router instead of
         * being called by name. */
        next_token();

        char method[16] = "";
        if (current_token.type == TOK_IDENT) {
            strncpy(method, current_token.ident, sizeof(method) - 1);
        } else if (current_token.type == TOK_STRING) {
            strncpy(method, current_token.str_val, sizeof(method) - 1);
        } else if (current_token.type == TOK_STAR) {
            strcpy(method, "*");
        } else {
            report_error("route expects an HTTP method (e.g. route GET \"/path\" { ... })");
            return NULL;
        }
        next_token();

        if (current_token.type != TOK_STRING) {
            report_error("route expects a path string (e.g. route GET \"/users/:id\" { ... })");
            return NULL;
        }
        char pattern[512];
        strncpy(pattern, current_token.str_val, sizeof(pattern) - 1);
        pattern[sizeof(pattern) - 1] = '\0';
        next_token();

        if (func_count >= MAX_FUNCS) {
            report_error("Too many functions/routes");
            return NULL;
        }

        static const char *route_params[] = {
            "method", "path", "query", "headers", "body", "params", "cookie"
        };
        Function *func = &functions[func_count++];
        snprintf(func->name, MAX_TOKEN_LEN, "__route_%d", func_count);
        func->param_count = 7;
        for (int i = 0; i < 7; i++) {
            strncpy(func->params[i], route_params[i], MAX_TOKEN_LEN - 1);
            func->params[i][MAX_TOKEN_LEN - 1] = '\0';
        }
        func->body = parse_block();
        end_stmt();

        if (router_add(method, pattern, func->name) != 0) {
            report_error("Too many routes");
        }
        return NULL;
    }

    if (current_token.type == TOK_IDENT) {
        /* A statement starting with an identifier is one of:
         *   f(x)            call
         *   x++ / x--       increment / decrement
         *   x = v, x += v   assignment
         *   a[i][j] = v, m.key = v, m["a"]["b"] += v   (any depth) */
        ASTNode *target = parse_primary();

        if (target->type == AST_CALL || target->type == AST_HTTP_CALL) {
            end_stmt();
            return target;
        }

        if (target->type == AST_VARIABLE &&
            (current_token.type == TOK_PLUSPLUS || current_token.type == TOK_MINUSMINUS)) {
            ASTNode *node = create_node(current_token.type == TOK_PLUSPLUS ? AST_INCREMENT : AST_DECREMENT);
            strncpy(node->inc_dec.var_name, target->var_name, MAX_TOKEN_LEN - 1);
            next_token();
            end_stmt();
            return node;
        }

        if (current_token.type == TOK_ASSIGN || is_compound_assign(current_token.type)) {
            NaCTokenType compound_op = is_compound_assign(current_token.type) ? compound_to_op(current_token.type) : TOK_EOF;
            next_token();
            ASTNode *value = parse_expression();

            if (target->type == AST_VARIABLE) {
                ASTNode *node = create_node(AST_ASSIGN);
                strncpy(node->assign.var_name, target->var_name, MAX_TOKEN_LEN - 1);
                node->assign.value = value;
                node->assign.compound_op = compound_op;
                end_stmt();
                return node;
            }
            if (target->type == AST_INDEX) {
                ASTNode *node = create_node(AST_INDEX_ASSIGN);
                node->index_assign.target = target;
                node->index_assign.value = value;
                node->index_assign.compound_op = compound_op;
                end_stmt();
                return node;
            }
            report_error("Invalid assignment target");
            return NULL;
        }

        report_error("Invalid statement");
        return NULL;
    }

    if (current_token.type == TOK_SEMI) {
        next_token();
        return NULL;
    }

    report_error("Invalid statement");
    next_token();
    return NULL;
}
