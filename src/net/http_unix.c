#include "http.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32

#include <curl/curl.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
    char *data;
    size_t len;
    size_t cap;
} HttpBuffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    HttpBuffer *buffer = (HttpBuffer*)userp;

    if (!buffer) {
        return total_size;
    }

    if (buffer->len + total_size + 1 > buffer->cap) {
        size_t new_cap = (buffer->cap == 0) ? 1024 : buffer->cap;
        while (buffer->len + total_size + 1 > new_cap) {
            new_cap *= 2;
        }
        buffer->data = (char*)realloc(buffer->data, new_cap);
        buffer->cap = new_cap;
    }

    memcpy(buffer->data + buffer->len, contents, total_size);
    buffer->len += total_size;
    buffer->data[buffer->len] = '\0';

    return total_size;
}

static int has_header_named(const char **lines, int count, const char *name) {
    size_t n = strlen(name);
    for (int i = 0; i < count; i++) {
        if (strncasecmp(lines[i], name, n) == 0 && lines[i][n] == ':') return 1;
    }
    return 0;
}

int http_perform_unix(const char *method, const char *url, const char *body,
                      const char **header_lines, int header_count,
                      int timeout_sec, HttpResponse *out) {
    out->status = 0;
    out->error[0] = '\0';
    out->body = NULL;

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(out->error, sizeof(out->error), "could not initialize curl");
        out->body = (char*)calloc(1, 1);
        return 0;
    }

    HttpBuffer buffer = {0};
    struct curl_slist *headers = NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);

    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        /* always give curl a body source, otherwise it waits on stdin */
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)(body ? strlen(body) : 0));
    } else {
        /* PUT, PATCH, DELETE */
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
    }

    if (body && !has_header_named(header_lines, header_count, "Content-Type")) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }
    for (int i = 0; i < header_count; i++) {
        headers = curl_slist_append(headers, header_lines[i]);
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NaC/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if (timeout_sec > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_sec);
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    int ok = 1;
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        snprintf(out->error, sizeof(out->error), "%s", curl_easy_strerror(res));
        ok = 0;
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        out->status = (int)code;
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    out->body = buffer.data ? buffer.data : (char*)calloc(1, 1);
    return ok;
}

void http_response_free(HttpResponse *r) {
    if (r && r->body) {
        free(r->body);
        r->body = NULL;
    }
}

#else

int http_perform_unix(const char *method, const char *url, const char *body,
                      const char **header_lines, int header_count,
                      int timeout_sec, HttpResponse *out) {
    (void)method; (void)url; (void)body; (void)header_lines;
    (void)header_count; (void)timeout_sec;
    out->status = 0;
    snprintf(out->error, sizeof(out->error), "not available on this platform");
    out->body = (char*)calloc(1, 1);
    return 0;
}

void http_response_free(HttpResponse *r) {
    if (r && r->body) {
        free(r->body);
        r->body = NULL;
    }
}

#endif



