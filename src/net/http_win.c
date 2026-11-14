#include "http.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <winhttp.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void win_set_error(HttpResponse *out, const char *what, DWORD code) {
    snprintf(out->error, sizeof(out->error), "%s (error %lu)", what, (unsigned long)code);
}

static int win_has_header(const char **lines, int count, const char *name) {
    size_t n = strlen(name);
    for (int i = 0; i < count; i++) {
        if (_strnicmp(lines[i], name, n) == 0 && lines[i][n] == ':') return 1;
    }
    return 0;
}

int http_perform_win(const char *method, const char *url, const char *body,
                     const char **header_lines, int header_count,
                     int timeout_sec, HttpResponse *out) {
    out->status = 0;
    out->error[0] = '\0';
    out->body = NULL;

    int is_https = (strncmp(url, "https://", 8) == 0);
    int is_http = (strncmp(url, "http://", 7) == 0);
    if (!is_https && !is_http) {
        snprintf(out->error, sizeof(out->error), "URL must start with http:// or https://");
        out->body = (char*)calloc(1, 1);
        return 0;
    }

    const char *host_start = is_https ? (url + 8) : (url + 7);
    const char *path_start = strchr(host_start, '/');
    int authority_len = path_start ? (int)(path_start - host_start) : (int)strlen(host_start);

    /* split an optional ":port" off the authority */
    INTERNET_PORT port = is_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    int host_len = authority_len;
    for (int i = authority_len - 1; i >= 0; i--) {
        if (host_start[i] == ':') {
            port = (INTERNET_PORT)atoi(host_start + i + 1);
            host_len = i;
            break;
        }
        if (host_start[i] == ']') break;   /* IPv6 literal without port */
    }

    wchar_t host[256];
    wchar_t path[2048];
    wchar_t method_w[16];

    if (host_len <= 0 || host_len >= 256) {
        snprintf(out->error, sizeof(out->error), "invalid host in URL");
        out->body = (char*)calloc(1, 1);
        return 0;
    }
    MultiByteToWideChar(CP_UTF8, 0, host_start, host_len, host, 256);
    host[host_len] = 0;
    if (path_start) {
        MultiByteToWideChar(CP_UTF8, 0, path_start, -1, path, 2048);
    } else {
        wcscpy(path, L"/");
    }
    MultiByteToWideChar(CP_UTF8, 0, method, -1, method_w, 16);

    /* header block: "Name: value\r\n..." */
    size_t hdr_cap = 64;
    for (int i = 0; i < header_count; i++) hdr_cap += strlen(header_lines[i]) + 2;
    char *hdr_a = (char*)malloc(hdr_cap);
    hdr_a[0] = '\0';
    if (body && !win_has_header(header_lines, header_count, "Content-Type")) {
        strcat(hdr_a, "Content-Type: application/json\r\n");
    }
    for (int i = 0; i < header_count; i++) {
        strcat(hdr_a, header_lines[i]);
        strcat(hdr_a, "\r\n");
    }
    int hdr_wlen = MultiByteToWideChar(CP_UTF8, 0, hdr_a, -1, NULL, 0);
    wchar_t *hdr_w = (wchar_t*)malloc(sizeof(wchar_t) * (hdr_wlen + 1));
    MultiByteToWideChar(CP_UTF8, 0, hdr_a, -1, hdr_w, hdr_wlen);
    hdr_w[hdr_wlen] = 0;
    free(hdr_a);

    int ok = 0;
    char *response = NULL;
    size_t len = 0, cap = 0;
    HINTERNET session = NULL, connect = NULL, request = NULL;

    session = WinHttpOpen(L"NaC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { win_set_error(out, "could not open session", GetLastError()); goto done; }

    if (timeout_sec > 0) {
        int ms = timeout_sec * 1000;
        WinHttpSetTimeouts(session, ms, ms, ms, ms);
    }

    connect = WinHttpConnect(session, host, port, 0);
    if (!connect) { win_set_error(out, "could not connect", GetLastError()); goto done; }

    request = WinHttpOpenRequest(connect, method_w, path, NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!request) { win_set_error(out, "could not open request", GetLastError()); goto done; }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    DWORD body_len = body ? (DWORD)strlen(body) : 0;
    if (!WinHttpSendRequest(request, hdr_wlen > 1 ? hdr_w : WINHTTP_NO_ADDITIONAL_HEADERS,
                            hdr_wlen > 1 ? (DWORD)-1 : 0,
                            (LPVOID)body, body_len, body_len, 0)) {
        win_set_error(out, "request failed", GetLastError());
        goto done;
    }
    if (!WinHttpReceiveResponse(request, NULL)) {
        win_set_error(out, "no response", GetLastError());
        goto done;
    }

    DWORD status = 0, status_size = sizeof(status);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                            WINHTTP_NO_HEADER_INDEX)) {
        out->status = (int)status;
    }

    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            win_set_error(out, "could not read response", GetLastError());
            out->status = 0;
            goto done;
        }
        if (!avail) break;
        if (len + avail + 1 > cap) {
            size_t new_cap = cap ? cap : 1024;
            while (len + avail + 1 > new_cap) new_cap *= 2;
            response = (char*)realloc(response, new_cap);
            cap = new_cap;
        }
        if (!WinHttpReadData(request, response + len, avail, &got)) {
            win_set_error(out, "could not read response", GetLastError());
            out->status = 0;
            goto done;
        }
        len += got;
    }
    ok = 1;

done:
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    free(hdr_w);

    if (!response) response = (char*)malloc(1);
    response[len] = '\0';
    out->body = response;
    return ok;
}

#else

int http_perform_win(const char *method, const char *url, const char *body,
                     const char **header_lines, int header_count,
                     int timeout_sec, HttpResponse *out) {
    (void)method; (void)url; (void)body; (void)header_lines;
    (void)header_count; (void)timeout_sec;
    out->status = 0;
    snprintf(out->error, sizeof(out->error), "not available on this platform");
    out->body = (char*)calloc(1, 1);
    return 0;
}

#endif
