#include "lexer.h"

#include <ctype.h>
#include <string.h>

#include "../core/interpreter.h"
#include "../util/error.h"

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

void next_token(void) {
    skip_whitespace_and_comments();

    if (pos >= code_len) {
        current_token.type = TOK_EOF;
        return;
    }

    char c = code[pos];

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

    if (isalpha(c) || c == '_' || c == '$') {
        int start = pos;
        while (pos < code_len && (isalnum(code[pos]) || code[pos] == '_')) {
            pos++;
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
