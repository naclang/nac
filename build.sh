#!/bin/bash

echo "[INFO] Starting build process..."

# Script'in bulunduğu dizine git (project/)
cd "$(dirname "$0")"

# src içindeki tüm .c dosyalarını bul
SOURCES=$(find src -type f -name "*.c")

if [ -z "$SOURCES" ]; then
    echo -e "\033[0;31m[ERROR]\033[0m No .c files found in src/."
    exit 1
fi

# Derle (çıktı project/ içine)
gcc $SOURCES -Isrc -o nac -lcurl

if [ $? -eq 0 ]; then
    echo -e "\033[0;32m[SUCCESS]\033[0m nac binary created successfully."
    chmod +x nac
else
    echo -e "\033[0;31m[ERROR]\033[0m Compilation failed."
    exit 1
fi