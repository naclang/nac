@echo off
echo NAC Yorumlayicisi derleniyor...
gcc -o nac.exe main.c variables.c expressions.c functions.c io.c utils.c
if %errorlevel% == 0 (
    echo Derleme basarili! nac.exe olusturuldu.
) else (
    echo Derleme hatasi!
)
pause
