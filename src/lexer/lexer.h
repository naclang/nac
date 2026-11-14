#ifndef NAC_LEXER_H
#define NAC_LEXER_H

#include "token.h"

void init_lexer(void);
void free_lexer(void);
void next_token(void);

/* Type of the token n positions after current_token (1 = the next one). */
NaCTokenType peek_token_type(int n);

#endif
