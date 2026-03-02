#include "http.h"

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

void http_request_win(const char *method, const char *url, const char *body) {
    bool is_https = (strncmp(url, "https://", 8) == 0);
    bool is_http = (strncmp(url, "http://", 7) == 0);

    if (!is_https && !is_http) {
        report_error("URL must start with http:// or https://");
        return;
    }

    const char *host_start = is_https ? (url + 8) : (url + 7);
    const char *path_start = strchr(host_start, '/');

    wchar_t host[256];
    wchar_t path[1024];
    wchar_t method_w[16];

    int host_len = path_start ? (path_start - host_start) : strlen(host_start);
    MultiByteToWideChar(CP_UTF8, 0, host_start, host_len, host, 256);
    host[host_len] = 0;

    if (path_start) {
        MultiByteToWideChar(CP_UTF8, 0, path_start, -1, path, 1024);
    } else {
        wcscpy(path, L"/");
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
        return;
    }

    INTERNET_PORT port = is_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET connect = WinHttpConnect(session, host, port, 0);

    if (!connect) {
        report_error("HTTP: Failed to connect");
        WinHttpCloseHandle(session);
        return;
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
        return;
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
        return;
    }

    WinHttpReceiveResponse(request, NULL);

    DWORD size = 0;
    do {
        DWORD downloaded = 0;
        WinHttpQueryDataAvailable(request, &size);
        if (!size) break;

        char *buffer = (char*)malloc(size + 1);
        WinHttpReadData(request, buffer, size, &downloaded);
        buffer[downloaded] = 0;
        printf("%s", buffer);
        free(buffer);
    } while (size > 0);

    printf("\n");

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

#else

void http_request_win(const char *method, const char *url, const char *body) {
    (void)method;
    (void)url;
    (void)body;
}

#endif
