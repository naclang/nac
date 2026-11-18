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
        while (pos < code_len && isspace((unsigned char)code[pos])) {
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

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int sb_reserve(StrBuf *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return 1;
    size_t cap = sb->cap ? sb->cap : 64;
    while (cap < need) cap *= 2;
    char *next = (char*)realloc(sb->data, cap);
    if (!next) return 0;
    sb->data = next;
    sb->cap = cap;
    return 1;
}

static int sb_append_char(StrBuf *sb, char c) {
    if (!sb_reserve(sb, 1)) return 0;
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 1;
}

static char *sb_take(StrBuf *sb) {
    if (!sb->data) {
        char *empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    sb->data[sb->len] = '\0';
    char *out = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}

static void scan_token(void);

static void push_token(Token t) {
    if (token_count >= token_capacity) {
        int new_cap = token_capacity ? token_capacity * 2 : 1024;
        Token *next = (Token*)realloc(tokens, sizeof(Token) * (size_t)new_cap);
        if (!next) {
            if (t.type == TOK_STRING) free(t.str_val);
            return;
        }
        tokens = next;
        token_capacity = new_cap;
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

static void push_string_token_owned(char *text, int line, int col) {
    Token t = synth_token(TOK_STRING, line, col, 0);
    t.str_val = text ? text : (char*)calloc(1, 1);
    push_token(t);
}

static void push_string_token(const char *text, int line, int col) {
    size_t len = text ? strlen(text) : 0;
    char *copy = (char*)malloc(len + 1);
    if (!copy) {
        push_string_token_owned(NULL, line, col);
        return;
    }
    if (len) memcpy(copy, text, len);
    copy[len] = '\0';
    push_string_token_owned(copy, line, col);
}

/* Backslash-u (four hex digits, UTF-16 surrogate pairs allowed) and
 * backslash-x (two hex digits, one raw byte) escapes in string literals. */
static int lex_hex(const char *s, int avail, int digits, unsigned *out) {
    if (avail < digits) return 0;
    unsigned v = 0;
    for (int i = 0; i < digits; i++) {
        char h = s[i];
        v <<= 4;
        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
        else if (h >= 'a' && h <= 'f') v |= (unsigned)(10 + h - 'a');
        else if (h >= 'A' && h <= 'F') v |= (unsigned)(10 + h - 'A');
        else return 0;
    }
    *out = v;
    return 1;
}

/* Called with pos on the 'u' or 'x' that follows a backslash. Returns 0 (and
 * consumes nothing) when the hex digits are missing; otherwise appends the
 * character, leaves pos after the escape and returns 1. */
static int lex_unicode_escape(StrBuf *sb) {
    int digits = (code[pos] == 'u') ? 4 : 2;
    unsigned cp = 0;
    if (!lex_hex(&code[pos + 1], code_len - pos - 1, digits, &cp)) return 0;
    for (int i = 0; i <= digits; i++) advance();

    if (cp == 0) {
        report_error("A string cannot contain a NUL character (zero escape)");
        return 1;
    }
    if (digits == 2) {
        sb_append_char(sb, (char)cp);
        return 1;
    }
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        unsigned low = 0;
        if (pos + 1 < code_len && code[pos] == '\\' && code[pos + 1] == 'u' &&
            lex_hex(&code[pos + 2], code_len - pos - 2, 4, &low) && low >= 0xDC00 && low <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            for (int i = 0; i < 6; i++) advance();
        } else {
            cp = 0xFFFD;
        }
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        cp = 0xFFFD;
    }

    if (cp <= 0x7F) {
        sb_append_char(sb, (char)cp);
    } else if (cp <= 0x7FF) {
        sb_append_char(sb, (char)(0xC0 | (cp >> 6)));
        sb_append_char(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        sb_append_char(sb, (char)(0xE0 | (cp >> 12)));
        sb_append_char(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_append_char(sb, (char)(0x80 | (cp & 0x3F)));
    } else {
        sb_append_char(sb, (char)(0xF0 | (cp >> 18)));
        sb_append_char(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_append_char(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_append_char(sb, (char)(0x80 | (cp & 0x3F)));
    }
    return 1;
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

    StrBuf lit;
    sb_init(&lit);
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
            if ((code[pos] == 'u' || code[pos] == 'x') && lex_unicode_escape(&lit)) {
                continue;
            }
            char e = code[pos];
            if (e == 'n') ch = '\n';
            else if (e == 't') ch = '\t';
            else if (e == 'r') ch = '\r';
            else ch = e;
            if (!sb_append_char(&lit, ch)) { report_error("Out of memory"); break; }
            advance();
            continue;
        }

        if (ch == '$' && pos + 1 < code_len && code[pos + 1] == '{') {
            if (lit.len > 0) {
                push_token(synth_token(TOK_PLUS, line, col, 0));
                push_string_token_owned(sb_take(&lit), line, col);
                sb_init(&lit);
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
                /* Transfer ownership of any string token into the stream. */
                push_token(t);
                if (t.type == TOK_STRING) current_token.str_val = NULL;
            }
            push_token(synth_token(TOK_RPAREN, line, col, 0));
            continue;
        }

        if (!sb_append_char(&lit, ch)) { report_error("Out of memory"); break; }
        advance();
    }

    if (!closed) report_error("Unterminated template string");
    if (lit.len > 0) {
        push_token(synth_token(TOK_PLUS, line, col, 0));
        push_string_token_owned(sb_take(&lit), line, col);
    } else {
        free(lit.data);
    }

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

        /* Scan the whole literal, then let strtod() convert it, so 0.1, 1e3
         * and 2.5E-2 are exact and an integer literal that does not fit in
         * 32 bits becomes a float instead of silently wrapping around. */
        int num_start = pos;
        int is_float = 0;
        while (pos < code_len && isdigit(code[pos])) advance();
        if (pos < code_len && code[pos] == '.') {
            is_float = 1;
            advance();
            while (pos < code_len && isdigit(code[pos])) advance();
        }
        if (pos < code_len && (code[pos] == 'e' || code[pos] == 'E')) {
            int q = pos + 1;
            if (q < code_len && (code[q] == '+' || code[q] == '-')) q++;
            if (q < code_len && isdigit(code[q])) {
                is_float = 1;
                while (pos < q) advance();
                while (pos < code_len && isdigit(code[pos])) advance();
            }
        }
        char numbuf[128];
        int num_len = pos - num_start;
        if (num_len > (int)sizeof(numbuf) - 1) num_len = (int)sizeof(numbuf) - 1;
        memcpy(numbuf, code + num_start, (size_t)num_len);
        numbuf[num_len] = '\0';
        double value = strtod(numbuf, NULL);

        if (!is_float && value <= 2147483647.0 + (sign < 0 ? 1.0 : 0.0)) {
            current_token.type = TOK_INT;
            current_token.int_val = (sign < 0) ? (int)(-value) : (int)value;
        } else {
            current_token.type = TOK_FLOAT;
            current_token.float_val = sign * value;
        }
        return;
    }

    if (c == '"') {
        advance();
        StrBuf sb;
        sb_init(&sb);
        while (pos < code_len && code[pos] != '"') {
            char ch = code[pos];
            if (ch == '\\' && pos + 1 < code_len) {
                advance();
                if ((code[pos] == 'u' || code[pos] == 'x') && lex_unicode_escape(&sb)) {
                    continue;
                }
                switch (code[pos]) {
                    case 'n': ch = '\n'; break;
                    case 't': ch = '\t'; break;
                    case 'r': ch = '\r'; break;
                    case '\\': ch = '\\'; break;
                    case '"': ch = '"'; break;
                    default: ch = code[pos]; break;
                }
            } else {
                ch = code[pos];
            }
            if (!sb_append_char(&sb, ch)) { report_error("Out of memory"); break; }
            advance();
        }
        if (pos < code_len && code[pos] == '"') {
            advance();
        } else {
            report_error("Unterminated string");
        }
        current_token.type = TOK_STRING;
        current_token.str_val = sb_take(&sb);
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
        for (int i = 0; i < token_count; i++) {
            if (tokens[i].type == TOK_STRING && tokens[i].str_val) {
                free(tokens[i].str_val);
                tokens[i].str_val = NULL;
            }
        }
        free(tokens);
        tokens = NULL;
    }
    token_capacity = 1024;
    tokens = (Token*)calloc((size_t)token_capacity, sizeof(Token));
    token_count = 0;
    token_pos = 0;
    current_line = 1;
    current_col = 1;
    saw_newline = 0;
    prev_type = TOK_EOF;
    memset(&current_token, 0, sizeof(current_token));

    // Build token array
    do {
        memset(&current_token, 0, sizeof(current_token));
        scan_token();
        prev_type = current_token.type;
        push_token(current_token);
        /* Ownership of string data transferred to tokens[token_count-1]. */
    } while (current_token.type != TOK_EOF);
}

void free_lexer(void) {
    if (tokens) {
        for (int i = 0; i < token_count; i++) {
            if (tokens[i].type == TOK_STRING && tokens[i].str_val) {
                free(tokens[i].str_val);
                tokens[i].str_val = NULL;
            }
        }
        free(tokens);
        tokens = NULL;
    }
    token_count = 0;
    token_capacity = 0;
    token_pos = 0;
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
