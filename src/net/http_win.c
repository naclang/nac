#include "http.h"
#include <stddef.h>

#ifdef _WIN32

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <winhttp.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/error.h"

static int parse_url(const char *url, int *is_https, wchar_t *host, int host_cap, wchar_t *path, int path_cap) {
    *is_https = (strncmp(url, "https://", 8) == 0);
    int is_http = (strncmp(url, "http://", 7) == 0);

    if (!*is_https && !is_http) {
        report_error("URL must start with http:// or https://");
        return 0;
    }

    const char *host_start = *is_https ? (url + 8) : (url + 7);
    const char *path_start = strchr(host_start, '/');

    int host_len = path_start ? (int)(path_start - host_start) : (int)strlen(host_start);
    if (host_len <= 0 || host_len >= host_cap) {
        report_error("Invalid host in URL");
        return 0;
    }

    MultiByteToWideChar(CP_UTF8, 0, host_start, host_len, host, host_cap);
    host[host_len] = 0;

    if (path_start) {
        MultiByteToWideChar(CP_UTF8, 0, path_start, -1, path, path_cap);
    } else {
        wcscpy(path, L"/");
    }

    return 1;
}

char *http_request_win_response(const char *method, const char *url, const char *body) {
    int is_https = 0;
    wchar_t host[256];
    wchar_t path[1024];
    wchar_t method_w[16];

    if (!parse_url(url, &is_https, host, 256, path, 1024)) {
        return NULL;
    }

    MultiByteToWideChar(CP_UTF8, 0, method, -1, method_w, 16);

    HINTERNET session = WinHttpOpen(
        L"NaC/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!session) {
        report_error("HTTP: Failed to open session");
        return NULL;
    }

    INTERNET_PORT port = is_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET connect = WinHttpConnect(session, host, port, 0);
    if (!connect) {
        report_error("HTTP: Failed to connect");
        WinHttpCloseHandle(session);
        return NULL;
    }

    DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connect,
        method_w,
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!request) {
        report_error("HTTP: Failed to open request");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return NULL;
    }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    const wchar_t *headers = body ? L"Content-Type: application/json\r\n" : NULL;
    BOOL result = WinHttpSendRequest(
        request,
        headers,
        headers ? -1 : 0,
        (LPVOID)body,
        body ? (DWORD)strlen(body) : 0,
        body ? (DWORD)strlen(body) : 0,
        0
    );

    if (!result) {
        report_error("HTTP: Failed to send request");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return NULL;
    }

    if (!WinHttpReceiveResponse(request, NULL)) {
        report_error("HTTP: Failed to receive response");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return NULL;
    }

    char *response = NULL;
    size_t len = 0;
    size_t cap = 0;

    DWORD size = 0;
    do {
        DWORD downloaded = 0;
        if (!WinHttpQueryDataAvailable(request, &size)) {
            report_error("HTTP: Failed to query response size");
            free(response);
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return NULL;
        }

        if (!size) {
            break;
        }

        if (len + size + 1 > cap) {
            size_t new_cap = (cap == 0) ? 1024 : cap;
            while (len + size + 1 > new_cap) {
                new_cap *= 2;
            }
            response = (char*)realloc(response, new_cap);
            cap = new_cap;
        }

        if (!WinHttpReadData(request, response + len, size, &downloaded)) {
            report_error("HTTP: Failed to read response");
            free(response);
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return NULL;
        }

        len += downloaded;
    } while (size > 0);

    if (!response) {
        response = (char*)malloc(1);
        response[0] = '\0';
    } else {
        response[len] = '\0';
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    return response;
}

void http_request_win(const char *method, const char *url, const char *body) {
    char *response = http_request_win_response(method, url, body);
    if (response) {
        printf("%s\n", response);
        free(response);
    }
}

#else

char *http_request_win_response(const char *method, const char *url, const char *body) {
    (void)method;
    (void)url;
    (void)body;
    return NULL;
}

void http_request_win(const char *method, const char *url, const char *body) {
    (void)method;
    (void)url;
    (void)body;
}

#endif
