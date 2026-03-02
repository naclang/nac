#ifndef NAC_HTTP_H
#define NAC_HTTP_H

void http_request_win(const char *method, const char *url, const char *body);
void http_request_unix(const char *method, const char *url, const char *body);

#endif
