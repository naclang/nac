@echo off
setlocal enabledelayedexpansion

echo [INFO] Starting build process...

:: Script'in bulunduğu klasöre git (project/)
cd /d %~dp0

:: src içindeki tüm .c dosyalarını topla
set "SOURCES="
for /r src %%f in (*.c) do (
    set "SOURCES=!SOURCES! "%%f""
)

if "%SOURCES%"=="" (
    echo [ERROR] No .c files found in src\
    exit /b 1
)

:: Derle (çıktı project\ içine)
gcc %SOURCES% -Isrc -o nac.exe -lwinhttp

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] nac.exe created successfully.
) else (
    echo [ERROR] Compilation failed.
    exit /b 1
)

pause