#include "http.h"

#ifndef _WIN32

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

#include "../util/error.h"

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    printf("%.*s", (int)total_size, (char*)contents);
    (void)userp;
    return total_size;
}

void http_request_unix(const char *method, const char *url, const char *body) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        report_error("HTTP: Failed to initialize curl");
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);

            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
    } else if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NaC/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "HTTP: %s", curl_easy_strerror(res));
        report_error(error_msg);
    } else {
        printf("\n");
    }

    curl_easy_cleanup(curl);
}

#else

void http_request_unix(const char *method, const char *url, const char *body) {
    (void)method;
    (void)url;
    (void)body;
}

#endif
