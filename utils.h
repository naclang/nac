#ifndef UTILS_H
#define UTILS_H

int utf8_strlen(const char* s);
int find_unquoted_char_index(const char *s, char target);
int is_valid_utf8(const char* s);
void trim_whitespace(char* s);
void stripQuotes(char* s);

#endif
