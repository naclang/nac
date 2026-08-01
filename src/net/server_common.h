#ifndef NAC_SERVER_COMMON_H
#define NAC_SERVER_COMMON_H

#include "../runtime/value.h"

typedef struct {
    char method[16];
    char path[1024];
    char query_raw[2048];
    Value query;    /* map: string -> string */
    Value headers;  /* map: string -> string (lowercased keys) */
    char body[MAX_STRING_LEN];
    int content_length;
} NacHttpRequest;

/* Parses everything up to and including the blank line ("\r\n\r\n") that
 * terminates the HTTP header block. `raw` must be NUL-terminated and
 * contain at least the full header block (header_end is its length,
 * i.e. the offset of the first body byte). Fills every field of `req`
 * except `body`, which the caller fills in once enough bytes have been
 * read from the socket. */
void nac_http_parse_request(const char *raw, int header_end, NacHttpRequest *req);

/* Frees the `query` and `headers` maps allocated by nac_http_parse_request. */
void nac_http_free_request(NacHttpRequest *req);

/* Invokes the handler (already looked up via call_nac_function by the
 * caller) result processing: builds a full HTTP response (status line,
 * headers, blank line, body) into `out` (capacity out_cap) and returns
 * the number of bytes written, or -1 if it didn't fit. `found_handler`
 * indicates whether the NaC function was actually found and called;
 * `result` is only read when it is non-zero. */
int nac_http_build_response(int found_handler, const char *handler_name, Value result,
                             char *out, int out_cap, int *out_status);

#endif
