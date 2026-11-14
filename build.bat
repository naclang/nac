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

:: 1. Adim: Ortamda halihazirda cl.exe var mi kontrol et
where cl.exe >nul 2>nul
if %ERRORLEVEL% EQU 0 goto :BUILD_MSVC

:: 2. Adim: cl.exe PATH'te degilse Visual Studio kurulumunu ara ve yukle
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_PATH=%%i"
    )
    if defined VS_PATH (
        if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
            echo [INFO] Initializing MSVC x64 environment...
            call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
            goto :BUILD_MSVC
        )
    )
)

:: 3. Adim: MSVC bulunamadiysa GCC kontrol et
where gcc >nul 2>nul
if %ERRORLEVEL% EQU 0 goto :BUILD_GCC

:: Hicbiri bulunamadiysa hata ver
echo [ERROR] Neither MSVC (cl.exe) nor GCC was found on this system.
exit /b 1

:BUILD_MSVC
echo [INFO] Compiler selected: MSVC (cl.exe)
if not exist build mkdir build
cl.exe /nologo /W3 /O2 /I"src" /Fo"build\\" %SOURCES% /Fe:nac.exe /link winhttp.lib ws2_32.lib bcrypt.lib /STACK:134217728
goto :CHECK_RESULT

:BUILD_GCC
echo [INFO] Compiler selected: GCC
gcc %SOURCES% -Isrc -o nac.exe -lwinhttp -lws2_32 -lbcrypt -lm -Wl,--stack,134217728
goto :CHECK_RESULT

:CHECK_RESULT
if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] nac.exe created successfully.
) else (
    echo [ERROR] Compilation failed.
    exit /b 1
)
