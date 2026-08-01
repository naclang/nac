@echo off
setlocal enabledelayedexpansion

echo [INFO] Starting build process...

cd /d %~dp0

set "SOURCES="
for /r src %%f in (*.c) do (
    set "SOURCES=!SOURCES! "%%f""
)

if "%SOURCES%"=="" (
    echo [ERROR] No .c files found in src\
    exit /b 1
)

gcc %SOURCES% -Isrc -o nac.exe -lwinhttp -lws2_32 -lm -Wl,--stack,134217728

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] nac.exe created successfully.
) else (
    echo [ERROR] Compilation failed.
    exit /b 1
)