#!/bin/bash

echo "[INFO] Starting build process..."

# Find all .c files recursively and store them in a variable
SOURCES=$(find . -name "*.c")

# Compile
gcc $SOURCES -I. -o nac -lcurl

if [ $? -eq 0 ]; then
    echo -e "\033[0;32m[SUCCESS]\033[0m nac binary created successfully."
    chmod +x nac
else
    echo -e "\033[0;31m[ERROR]\033[0m Compilation failed."
    exit 1
fi