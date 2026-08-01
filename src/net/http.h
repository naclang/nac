#ifndef NAC_HTTP_H
#define NAC_HTTP_H

char *http_request_win_response(const char *method, const char *url, const char *body);
char *http_request_unix_response(const char *method, const char *url, const char *body);

void http_request_win(const char *method, const char *url, const char *body);
void http_request_unix(const char *method, const char *url, const char *body);

#endif
