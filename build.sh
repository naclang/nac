#!/bin/bash

echo "[INFO] Starting build process..."

cd "$(dirname "$0")"

SOURCES=$(find src -type f -name "*.c")

if [ -z "$SOURCES" ]; then
    echo -e "\033[0;31m[ERROR]\033[0m No .c files found in src/."
    exit 1
fi

gcc $SOURCES -Isrc -o nac -lcurl -lm -lpthread

if [ $? -eq 0 ]; then
    echo -e "\033[0;32m[SUCCESS]\033[0m nac binary created successfully."
    chmod +x nac
else
    echo -e "\033[0;31m[ERROR]\033[0m Compilation failed."
    exit 1
fi