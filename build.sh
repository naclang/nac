#!/bin/bash

echo "[INFO] Starting build process..."

cd "$(dirname "$0")"

SOURCES=$(find src -type f -name "*.c")

if [ -z "$SOURCES" ]; then
    echo -e "\033[0;31m[ERROR]\033[0m No .c files found in src/."
    exit 1
fi

# Crypto backend is OpenSSL. Prefer pkg-config so keg-only installs
# (notably Homebrew's openssl@3 on macOS) are found; fall back to -lcrypto.
CRYPTO_CFLAGS=""
CRYPTO_LIBS="-lcrypto"
if command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists libcrypto; then
        CRYPTO_CFLAGS=$(pkg-config --cflags libcrypto)
        CRYPTO_LIBS=$(pkg-config --libs libcrypto)
    elif pkg-config --exists openssl; then
        CRYPTO_CFLAGS=$(pkg-config --cflags openssl)
        CRYPTO_LIBS=$(pkg-config --libs openssl)
    fi
fi

gcc $SOURCES -Isrc $CRYPTO_CFLAGS -o nac -lcurl $CRYPTO_LIBS -lm -lpthread

if [ $? -eq 0 ]; then
    echo -e "\033[0;32m[SUCCESS]\033[0m nac binary created successfully."
    chmod +x nac
else
    echo -e "\033[0;31m[ERROR]\033[0m Compilation failed."
    exit 1
fi