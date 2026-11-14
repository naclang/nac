#ifndef NAC_CRYPTO_H
#define NAC_CRYPTO_H

#include <stddef.h>

#define NAC_SHA256_LEN 32

/* Fills `out` with `len` cryptographically secure random bytes.
 * Returns 1 on success, 0 on failure. */
int nac_random_bytes(unsigned char *out, size_t len);

/* One-shot SHA-256. Returns 1 on success, 0 on failure. */
int nac_sha256(const unsigned char *data, size_t len,
               unsigned char out[NAC_SHA256_LEN]);

/* HMAC-SHA-256. Returns 1 on success, 0 on failure. */
int nac_hmac_sha256(const unsigned char *key, size_t key_len,
                    const unsigned char *data, size_t data_len,
                    unsigned char out[NAC_SHA256_LEN]);

/* PBKDF2-HMAC-SHA-256 (the password-hash primitive behind passwordHash()).
 * `password` is NUL-terminated; `out` receives `out_len` derived bytes.
 * Returns 1 on success, 0 on failure. */
int nac_pbkdf2_sha256(const char *password,
                      const unsigned char *salt, size_t salt_len,
                      int iterations,
                      unsigned char *out, size_t out_len);

/* Constant-time comparison. Length mismatch returns 0, but the common
 * prefix is still compared so the result does not depend on its bytes. */
int nac_constant_time_equal(const unsigned char *a, size_t a_len,
                            const unsigned char *b, size_t b_len);

/* Standard or URL-safe ("-"/"_") base64. `padding` adds '=' when non-zero.
 * Returns 1 on success, 0 if `out_cap` is too small. `out` is NUL-terminated. */
int nac_base64_encode(const unsigned char *src, size_t len,
                      char *out, size_t out_cap,
                      int url_safe, int padding);

/* Decodes standard or URL-safe base64 (padding optional). Returns 1 on
 * success, 0 on invalid input or if `out_cap` is too small. `*out_len` is
 * the number of decoded bytes; `out` is NUL-terminated when room allows. */
int nac_base64_decode(const char *src, unsigned char *out, size_t out_cap,
                      size_t *out_len);

#endif
