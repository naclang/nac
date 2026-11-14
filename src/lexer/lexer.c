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
static int saw_newline = 0;
static NaCTokenType prev_type = TOK_EOF;

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
            if (code[pos] == '\n') saw_newline = 1;
            advance();
        }
        if (pos < code_len - 1 && code[pos] == '/' && code[pos + 1] == '*') {
            advance(); advance();
            while (pos < code_len && !(code[pos] == '*' && pos + 1 < code_len && code[pos + 1] == '/')) {
                if (code[pos] == '\n') saw_newline = 1;
                advance();
            }
            if (pos < code_len) { advance(); advance(); }
            continue;
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

static void scan_token(void);

static void push_token(Token t) {
    if (token_count >= token_capacity) {
        token_capacity *= 2;
        tokens = realloc(tokens, sizeof(Token) * token_capacity);
    }
    tokens[token_count++] = t;
}

static Token synth_token(NaCTokenType type, int line, int col, int nl) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.type = type;
    t.line = line;
    t.col = col;
    t.nl_before = nl;
    return t;
}

static void push_string_token(const char *text, int line, int col) {
    Token t = synth_token(TOK_STRING, line, col, 0);
    strncpy(t.str_val, text, MAX_STRING_LEN - 1);
    push_token(t);
}

/* Template string:  `Hello ${name}, you are ${age + 1}!`
 * Expanded right here into the token stream of
 *   ("" + "Hello " + (name) + ", you are " + (age + 1) + "!")
 * so the parser needs no special support. Text may span lines; escapes:
 * \n \t \r \\ \` \$ . */
static void scan_template(void) {
    int line = current_token.line, col = current_token.col, nl = current_token.nl_before;
    advance(); /* opening backtick */

    push_token(synth_token(TOK_LPAREN, line, col, nl));
    push_string_token("", line, col);

    char *lit = (char*)malloc(MAX_STRING_LEN);
    int len = 0;
    int closed = 0;

    while (pos < code_len) {
        char ch = code[pos];

        if (ch == '`') {
            advance();
            closed = 1;
            break;
        }

        if (ch == '\\' && pos + 1 < code_len) {
            advance();
            char e = code[pos];
            if (e == 'n') ch = '\n';
            else if (e == 't') ch = '\t';
            else if (e == 'r') ch = '\r';
            else ch = e;
            if (len < MAX_STRING_LEN - 1) lit[len++] = ch;
            advance();
            continue;
        }

        if (ch == '$' && pos + 1 < code_len && code[pos + 1] == '{') {
            if (len > 0) {
                lit[len] = '\0';
                push_token(synth_token(TOK_PLUS, line, col, 0));
                push_string_token(lit, line, col);
                len = 0;
            }
            advance();
            advance();
            push_token(synth_token(TOK_PLUS, line, col, 0));
            push_token(synth_token(TOK_LPAREN, line, col, 0));
            prev_type = TOK_LPAREN;

            int depth = 0;
            for (;;) {
                scan_token();
                Token t = current_token;
                if (t.type == TOK_EOF) {
                    report_error("Unterminated ${ } in template string");
                    break;
                }
                if (t.type == TOK_LBRACE) {
                    depth++;
                } else if (t.type == TOK_RBRACE) {
                    if (depth == 0) break;
                    depth--;
                }
                prev_type = t.type;
                push_token(t);
            }
            push_token(synth_token(TOK_RPAREN, line, col, 0));
            continue;
        }

        if (len < MAX_STRING_LEN - 1) lit[len++] = ch;
        advance();
    }

    if (!closed) report_error("Unterminated template string");
    if (len > 0) {
        lit[len] = '\0';
        push_token(synth_token(TOK_PLUS, line, col, 0));
        push_string_token(lit, line, col);
    }
    free(lit);

    current_token = synth_token(TOK_RPAREN, line, col, 0);
}

static void scan_token(void) {
    saw_newline = 0;
    skip_whitespace_and_comments();

    current_token.nl_before = saw_newline;
    current_token.line = current_line;
    current_token.col = current_col;

    if (pos >= code_len) {
        current_token.type = TOK_EOF;
        return;
    }

    char c = code[pos];

    if (c == '`') {
        scan_template();
        return;
    }

    int prev_is_operand = (prev_type == TOK_INT || prev_type == TOK_FLOAT || prev_type == TOK_STRING ||
                           prev_type == TOK_IDENT || prev_type == TOK_RPAREN || prev_type == TOK_RBRACKET ||
                           prev_type == TOK_BOOL || prev_type == TOK_NULL);

    if (isdigit(c) || (c == '-' && !prev_is_operand && pos + 1 < code_len && isdigit(code[pos + 1]))) {
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
        if (strcmp(ident, "return") == 0) { current_token.type = TOK_RN; return; }
        if (strcmp(ident, "print") == 0) { current_token.type = TOK_OUT; return; }
        if (strcmp(ident, "and") == 0) { current_token.type = TOK_AND; return; }
        if (strcmp(ident, "or") == 0) { current_token.type = TOK_OR; return; }
        if (strcmp(ident, "not") == 0) { current_token.type = TOK_NOT; return; }
        if (strcmp(ident, "if") == 0) { current_token.type = TOK_IF; return; }
        if (strcmp(ident, "for") == 0) { current_token.type = TOK_FOR; return; }
        if (strcmp(ident, "while") == 0) { current_token.type = TOK_WHILE; return; }
        if (strcmp(ident, "else") == 0) { current_token.type = TOK_ELSE; return; }
        if (strcmp(ident, "elif") == 0) { current_token.type = TOK_ELIF; return; }
        if (strcmp(ident, "route") == 0) { current_token.type = TOK_ROUTE; return; }
        if (strcmp(ident, "try") == 0) { current_token.type = TOK_TRY; return; }
        if (strcmp(ident, "catch") == 0) { current_token.type = TOK_CATCH; return; }
        if (strcmp(ident, "throw") == 0) { current_token.type = TOK_THROW; return; }
        if (strcmp(ident, "in") == 0) { current_token.type = TOK_IN; return; }
        if (strcmp(ident, "break") == 0) { current_token.type = TOK_BREAK; return; }
        if (strcmp(ident, "continue") == 0) { current_token.type = TOK_CONTINUE; return; }
        if (strcmp(ident, "true") == 0) { current_token.type = TOK_BOOL; current_token.int_val = 1; return; }
        if (strcmp(ident, "false") == 0) { current_token.type = TOK_BOOL; current_token.int_val = 0; return; }
        if (strcmp(ident, "null") == 0) { current_token.type = TOK_NULL; return; }
        
        current_token.type = TOK_IDENT;
        strncpy(current_token.ident, ident, MAX_TOKEN_LEN - 1);
        return;
    }

    if (c == '+') {
        if (pos + 1 < code_len && code[pos + 1] == '+') {
            advance(); advance(); current_token.type = TOK_PLUSPLUS; return;
        }
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            advance(); advance(); current_token.type = TOK_PLUS_ASSIGN; return;
        }
        advance(); current_token.type = TOK_PLUS; return;
    }
    if (c == '-') {
        if (pos + 1 < code_len && code[pos + 1] == '-') {
            advance(); advance(); current_token.type = TOK_MINUSMINUS; return;
        }
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            advance(); advance(); current_token.type = TOK_MINUS_ASSIGN; return;
        }
        advance(); current_token.type = TOK_MINUS; return;
    }
    if (c == '*') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            advance(); advance(); current_token.type = TOK_STAR_ASSIGN; return;
        }
        advance(); current_token.type = TOK_STAR; return;
    }
    if (c == '/') {
        if (pos + 1 < code_len && code[pos + 1] == '=') {
            advance(); advance(); current_token.type = TOK_SLASH_ASSIGN; return;
        }
        advance(); current_token.type = TOK_SLASH; return;
    }
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
    if (c == '.') { advance(); current_token.type = TOK_DOT; return; }

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
    saw_newline = 0;
    prev_type = TOK_EOF;

    // Build token array
    do {
        scan_token();
        prev_type = current_token.type;
        push_token(current_token);
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

NaCTokenType peek_token_type(int n) {
    int idx = token_pos + n - 1;
    if (idx >= 0 && idx < token_count) return tokens[idx].type;
    return TOK_EOF;
}
