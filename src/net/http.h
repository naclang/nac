#ifndef NAC_HTTP_H
#define NAC_HTTP_H

/* HTTP client backend for the `http.get { ... }` syntax.
 * It never reports an interpreter error: transport failures are returned in the result so scripts can handle
 * them (res.ok / res.error). HTTP 4xx/5xx are NOT failures here -- they come
 * back with their status code.
 *
 *   header_lines: array of "Name: value" strings (may be NULL)
 *   timeout_sec : <= 0 means no timeout
 * Returns 1 if a response was received, 0 on a transport error. */
typedef struct {
    int status;        /* HTTP status, 0 if no response */
    char *body;        /* malloc'd, never NULL after http_perform_*() */
    char error[256];   /* "" on success */
} HttpResponse;

int http_perform_unix(const char *method, const char *url, const char *body,
                      const char **header_lines, int header_count,
                      int timeout_sec, HttpResponse *out);
int http_perform_win(const char *method, const char *url, const char *body,
                     const char **header_lines, int header_count,
                     int timeout_sec, HttpResponse *out);
void http_response_free(HttpResponse *r);

#endif
