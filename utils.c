#include "utils.h"
#include <string.h>
#include <ctype.h>

int utf8_strlen(const char* s) {
    int count = 0;
    while (*s) {
        if ((*s & 0xC0) != 0x80) count++;
        s++;
    }
    return count;
}

int find_unquoted_char_index(const char *s, char target) {
    int in_quote = 0;
    for (int i = 0; s[i]; ++i) {
        if (s[i] == '"' && (i == 0 || s[i-1] != '\\')) {
            in_quote = !in_quote;
        } else if (!in_quote && s[i] == target) {
            return i;
        }
    }
    return -1;
}

int is_valid_utf8(const char* s) {
    while (*s) {
        if ((*s & 0x80) == 0) s++;
        else if ((*s & 0xE0) == 0xC0) { if ((s[1] & 0xC0) != 0x80) return 0; s+=2; }
        else if ((*s & 0xF0) == 0xE0) { if ((s[1]&0xC0)!=0x80 || (s[2]&0xC0)!=0x80) return 0; s+=3; }
        else if ((*s & 0xF8) == 0xF0) { if ((s[1]&0xC0)!=0x80 || (s[2]&0xC0)!=0x80 || (s[3]&0xC0)!=0x80) return 0; s+=4; }
        else return 0;
    }
    return 1;
}

void trim_whitespace(char* s) {
    while (*s && isspace((unsigned char)*s)) {
        memmove(s, s+1, strlen(s));
    }
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = 0;
        end--;
    }
}

void stripQuotes(char* s) {
    int len = strlen(s);
    if (len >= 2 && s[0]=='"' && s[len-1]=='"') {
        memmove(s, s+1, len-2);
        s[len-2]=0;
    }
}
