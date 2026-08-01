#include "http.h"
#include <stddef.h>

#ifndef _WIN32

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/error.h"

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

char *http_request_unix_response(const char *method, const char *url, const char *body) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        report_error("HTTP: Failed to initialize curl");
        return NULL;
    }

    HttpBuffer buffer = {0};
    struct curl_slist *headers = NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            headers = curl_slist_append(headers, "Content-Type: application/json");
        }
    } else if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            headers = curl_slist_append(headers, "Content-Type: application/json");
        }
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NaC/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "HTTP: %s", curl_easy_strerror(res));
        report_error(error_msg);
        free(buffer.data);
        buffer.data = NULL;
    }

    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);

    if (!buffer.data) {
        return NULL;
    }

    return buffer.data;
}

void http_request_unix(const char *method, const char *url, const char *body) {
    char *response = http_request_unix_response(method, url, body);
    if (response) {
        printf("%s\n", response);
        free(response);
    }
}

#else

char *http_request_unix_response(const char *method, const char *url, const char *body) {
    (void)method;
    (void)url;
    (void)body;
    return NULL;
}

void http_request_unix(const char *method, const char *url, const char *body) {
    (void)method;
    (void)url;
    (void)body;
}

#endif



