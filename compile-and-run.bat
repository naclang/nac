@echo off
chcp 65001 >nul
echo Derleniyor...
gcc -finput-charset=UTF-8 -fexec-charset=UTF-8 -o nac.exe main.c
if %errorlevel% neq 0 (
    echo Derleme hatası!
    pause
    exit /b 1
)
echo Derleme başarılı!
echo.
echo Çalıştırılıyor...
echo.
nac.exe test.nac
echo.
pause