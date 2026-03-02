@echo off
setlocal enabledelayedexpansion

echo [INFO] Starting build process...

:: Collect all .c files into a variable
set "SOURCES="
for /r %%f in (*.c) do (
    set "SOURCES=!SOURCES! %%f"
)

:: Compile
gcc %SOURCES% -I. -o nac.exe -lwinhttp

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] nac.exe created successfully.
) else (
    echo [ERROR] Compilation failed.
)

pause