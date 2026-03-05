#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../core/interpreter.h"
#include "../util/error.h"

static Token *tokens = NULL;
static int token_count = 0;
static int token_capacity = 0;
static int token_pos = 0;

static int current_line = 1;
static int current_col = 1;

static void advance(void) {
    if (pos < code_len) {
        if (code[pos] == '\n') {
            current_line++;
            current_col = 1;
        } else {
            current_col++;
        }
        pos++;
    }
}

static void skip_whitespace_and_comments(void) {
    while (pos < code_len) {
        while (pos < code_len && isspace(code[pos])) {
            advance();
        }
        if (pos < code_len - 1 && code[pos] == '/' && code[pos + 1] == '/') {
            while (pos < code_len && code[pos] != '\n') {
                advance();
            }
            continue;
        }
        break;
    }
}

static void scan_token(void) {
    skip_whitespace_and_comments();

    current_token.line = current_line;
    current_token.col = current_col;

    if (pos >= code_len) {
        current_token.type = TOK_EOF;
        return;
    }

    char c = code[pos];

    if (isdigit(c) || (c == '-' && pos + 1 < code_len && isdigit(code[pos + 1]))) {
        int sign = 1;
        if (c == '-') {
            sign = -1;
            advance();
        }

        double value = 0;
        while (pos < code_len && isdigit(code[pos])) {
            value = value * 10 + (code[pos] - '0');
            advance();
        }

        if (pos < code_len && code[pos] == '.') {
            advance();
            double decimal = 0.1;
            while (pos < code_len && isdigit(code[pos])) {
                value += (code[pos] - '0') * decimal;
                decimal *= 0.1;
                advance();
            }
            current_token.type = TOK_FLOAT;
            current_token.float_val = sign * value;
        } else {
            current_token.type = TOK_INT;
            current_token.int_val = sign * (int)value;
        }
        return;
    }

    if (c == '"') {
        advance();
        char str[MAX_STRING_LEN];
        int len = 0;
        while (pos < code_len && code[pos] != '"' && len < MAX_STRING_LEN - 1) {
            if (code[pos] == '\\' && pos + 1 < code_len) {
                advance();
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
            advance();
        }
        str[len] = '\0';
        if (pos < code_len && code[pos] == '"') advance();
        current_token.type = TOK_STRING;
        strncpy(current_token.str_val, str, MAX_STRING_LEN - 1);
        return;
    }

    if (isalpha(c) || c == '_' || c == '$') {
        int start = pos;
        while (pos < code_len && (isalnum(code[pos]) || code[pos] == '_')) {
            advance();
        }
        int len = pos - start;
        char ident[MAX_TOKEN_LEN];
        strncpy(ident, &code[start], len);
        ident[len] = '\0';

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

    if (c == '+') {
        if (pos + 1 < code_len && code[pos + 1] == '+') {
            advance(); advance(); current_token.type = TOK_PLUSPLUS; return;
        }
        advance(); current_token.type = TOK_PLUS; return;
    }
    if (c == '-') {
        if (pos + 1 < code_len && code[pos + 1] == '-') {
            advance(); advance(); current_token.type = TOK_MINUSMINUS; return;
        }
        advance(); current_token.type = TOK_MINUS; return;
    }
    if (c == '*') { advance(); current_token.type = TOK_STAR; return; }
    if (c == '/') { advance(); current_token.type = TOK_SLASH; return; }
    if (c == '%') { advance(); current_token.type = TOK_PERCENT; return; }
    if (c == '=') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            advance(); advance(); current_token.type = TOK_EQ; return;
        }
        advance(); current_token.type = TOK_ASSIGN; return;
    }
    if (c == '!') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            advance(); advance(); current_token.type = TOK_NEQ; return;
        }
        advance(); current_token.type = TOK_NOT; return;
    }
    if (c == '<') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            advance(); advance(); current_token.type = TOK_LTE; return;
        }
        advance(); current_token.type = TOK_LT; return;
    }
    if (c == '>') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            advance(); advance(); current_token.type = TOK_GTE; return;
        }
        advance(); current_token.type = TOK_GT; return;
    }
    if (c == '&' && pos + 1 < code_len && code[pos + 1] == '&') {
        advance(); advance(); current_token.type = TOK_AND; return;
    }
    if (c == '|' && pos + 1 < code_len && code[pos + 1] == '|') {
        advance(); advance(); current_token.type = TOK_OR; return;
    }
    if (c == ';') { advance(); current_token.type = TOK_SEMI; return; }
    if (c == ',') { advance(); current_token.type = TOK_COMMA; return; }
    if (c == '(') { advance(); current_token.type = TOK_LPAREN; return; }
    if (c == ')') { advance(); current_token.type = TOK_RPAREN; return; }
    if (c == '{') { advance(); current_token.type = TOK_LBRACE; return; }
    if (c == '}') { advance(); current_token.type = TOK_RBRACE; return; }
    if (c == '[') { advance(); current_token.type = TOK_LBRACKET; return; }
    if (c == ']') { advance(); current_token.type = TOK_RBRACKET; return; }
    if (c == ':') { advance(); current_token.type = TOK_COLON; return; }

    report_error("Unknown character");
    advance();
    scan_token();
}

void init_lexer(void) {
    if (tokens) {
        free(tokens);
    }
    token_capacity = 1024;
    tokens = malloc(sizeof(Token) * token_capacity);
    token_count = 0;
    token_pos = 0;
    current_line = 1;
    current_col = 1;

    // Build token array
    do {
        scan_token();
        if (token_count >= token_capacity) {
            token_capacity *= 2;
            tokens = realloc(tokens, sizeof(Token) * token_capacity);
        }
        tokens[token_count++] = current_token;
    } while (current_token.type != TOK_EOF);
}

void free_lexer(void) {
    if (tokens) {
        free(tokens);
        tokens = NULL;
    }
}

void next_token(void) {
    if (token_pos < token_count) {
        current_token = tokens[token_pos++];
    } else {
        current_token.type = TOK_EOF;
    }
}
