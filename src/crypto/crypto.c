#include "crypto.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

/* MSVC picks this up automatically; MinGW needs the -lbcrypt link flag. */
#pragma comment(lib, "bcrypt.lib")

static int nt_ok(NTSTATUS status) {
    return status >= 0;
}
#else
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#endif

int nac_random_bytes(unsigned char *out, size_t len) {
    if (len == 0) return 1;
    if (!out) return 0;

#ifdef _WIN32
    return nt_ok(BCryptGenRandom(NULL, out, (ULONG)len,
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
    return RAND_bytes(out, (int)len) == 1;
#endif
}

int nac_sha256(const unsigned char *data, size_t len,
               unsigned char out[NAC_SHA256_LEN]) {
    if (!out) return 0;

#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    int ok = 0;

    if (!nt_ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0))) {
        return 0;
    }
    if (!nt_ok(BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return 0;
    }

    ok = nt_ok(BCryptHashData(hash, (PUCHAR)data, (ULONG)len, 0)) &&
         nt_ok(BCryptFinishHash(hash, out, NAC_SHA256_LEN, 0));

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
#else
    unsigned int out_len = 0;
    return EVP_Digest(data, len, out, &out_len, EVP_sha256(), NULL) == 1 &&
           out_len == NAC_SHA256_LEN;
#endif
}

int nac_hmac_sha256(const unsigned char *key, size_t key_len,
                    const unsigned char *data, size_t data_len,
                    unsigned char out[NAC_SHA256_LEN]) {
    if (!out) return 0;

#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    int ok = 0;

    if (!nt_ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL,
                                           BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        return 0;
    }
    if (!nt_ok(BCryptCreateHash(alg, &hash, NULL, 0,
                                (PUCHAR)key, (ULONG)key_len, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return 0;
    }

    ok = nt_ok(BCryptHashData(hash, (PUCHAR)data, (ULONG)data_len, 0)) &&
         nt_ok(BCryptFinishHash(hash, out, NAC_SHA256_LEN, 0));

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
#else
    unsigned int out_len = 0;
    if (!HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out, &out_len)) {
        return 0;
    }
    return out_len == NAC_SHA256_LEN;
#endif
}

int nac_pbkdf2_sha256(const char *password,
                      const unsigned char *salt, size_t salt_len,
                      int iterations,
                      unsigned char *out, size_t out_len) {
    if (!password || !salt || !out || iterations <= 0 || out_len == 0) {
        return 0;
    }

#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg = NULL;
    int ok = 0;

    if (!nt_ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL,
                                           BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        return 0;
    }

    ok = nt_ok(BCryptDeriveKeyPBKDF2(alg,
                                     (PUCHAR)password, (ULONG)strlen(password),
                                     (PUCHAR)salt, (ULONG)salt_len,
                                     (ULONGLONG)iterations,
                                     out, (ULONG)out_len, 0));

    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
#else
    return PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                             salt, (int)salt_len, iterations,
                             EVP_sha256(), (int)out_len, out) == 1;
#endif
}

int nac_constant_time_equal(const unsigned char *a, size_t a_len,
                            const unsigned char *b, size_t b_len) {
    if (!a || !b) return 0;

    /* Compare the common prefix even when lengths differ, so the timing does
     * not depend on the byte values of the shorter string. */
    size_t n = a_len < b_len ? a_len : b_len;
    unsigned char diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    diff |= (unsigned char)(a_len == b_len ? 0 : 1);
    return diff == 0;
}

static const char B64_STD[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char B64_URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int nac_base64_encode(const unsigned char *src, size_t len,
                      char *out, size_t out_cap,
                      int url_safe, int padding) {
    if (!out || out_cap == 0) return 0;

    const char *alphabet = url_safe ? B64_URL : B64_STD;
    size_t groups = (len + 2) / 3;
    size_t needed = groups * 4;

    if (!padding) {
        if (len % 3 == 1) needed -= 2;
        else if (len % 3 == 2) needed -= 1;
    }

    if (needed + 1 > out_cap) return 0;

    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        unsigned int b0 = src[i];
        unsigned int b1 = (i + 1 < len) ? src[i + 1] : 0;
        unsigned int b2 = (i + 2 < len) ? src[i + 2] : 0;
        unsigned int triple = (b0 << 16) | (b1 << 8) | b2;

        out[o++] = alphabet[(triple >> 18) & 0x3f];
        out[o++] = alphabet[(triple >> 12) & 0x3f];
        if (i + 1 < len) out[o++] = alphabet[(triple >> 6) & 0x3f];
        else if (padding) out[o++] = '=';
        if (i + 2 < len) out[o++] = alphabet[triple & 0x3f];
        else if (padding) out[o++] = '=';
    }

    out[o] = '\0';
    return 1;
}

static int b64_value(char c, int *ok) {
    *ok = 1;
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    *ok = 0;
    return 0;
}

int nac_base64_decode(const char *src, unsigned char *out, size_t out_cap,
                      size_t *out_len) {
    if (!src || !out) return 0;

    unsigned int acc = 0;
    int sextets = 0;
    size_t o = 0;

    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        char c = (char)*p;

        if (c == '=') break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;

        int ok = 0;
        int v = b64_value(c, &ok);
        if (!ok) return 0;

        acc = (acc << 6) | (unsigned int)v;
        sextets++;

        if (sextets == 4) {
            if (o >= out_cap) return 0;
            out[o++] = (unsigned char)((acc >> 16) & 0xff);
            if (o >= out_cap) return 0;
            out[o++] = (unsigned char)((acc >> 8) & 0xff);
            if (o >= out_cap) return 0;
            out[o++] = (unsigned char)(acc & 0xff);
            acc = 0;
            sextets = 0;
        }
    }

    if (sextets == 1) return 0;
    if (sextets == 2) {
        if (o >= out_cap) return 0;
        out[o++] = (unsigned char)((acc >> 4) & 0xff);
    } else if (sextets == 3) {
        if (o >= out_cap) return 0;
        out[o++] = (unsigned char)((acc >> 10) & 0xff);
        if (o >= out_cap) return 0;
        out[o++] = (unsigned char)((acc >> 2) & 0xff);
    }

    if (o < out_cap) out[o] = '\0';
    if (out_len) *out_len = o;
    return 1;
}
