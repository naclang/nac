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

# SQLite is optional. With it, dbOpen()/dbQuery()/... work; without it they
# exist but report that this build has no SQLite.
SQLITE_CFLAGS=""
SQLITE_LIBS=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sqlite3; then
    SQLITE_CFLAGS="-DNAC_WITH_SQLITE $(pkg-config --cflags sqlite3)"
    SQLITE_LIBS=$(pkg-config --libs sqlite3)
elif echo '#include <sqlite3.h>' | gcc -E -x c - >/dev/null 2>&1; then
    SQLITE_CFLAGS="-DNAC_WITH_SQLITE"
    SQLITE_LIBS="-lsqlite3"
fi
if [ -n "$SQLITE_CFLAGS" ]; then
    echo "[INFO] SQLite support: enabled"
else
    echo -e "\033[0;33m[WARN]\033[0m SQLite development files not found: building WITHOUT SQLite support."
    echo "       Install libsqlite3-dev (Debian/Ubuntu), sqlite-devel (Fedora), sqlite (Arch, Homebrew) and rebuild."
fi

gcc $SOURCES -Isrc $CRYPTO_CFLAGS $SQLITE_CFLAGS -o nac -lcurl $CRYPTO_LIBS $SQLITE_LIBS -lm -lpthread

if [ $? -eq 0 ]; then
    echo -e "\033[0;32m[SUCCESS]\033[0m nac binary created successfully."
    chmod +x nac
else
    echo -e "\033[0;31m[ERROR]\033[0m Compilation failed."
    exit 1
fi