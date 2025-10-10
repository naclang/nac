# UTF-8 Destekli NAC Yorumlayıcı

Bu C programı, tamamen UTF-8 uyumlu özel bir betik dili yorumlayıcısıdır.

## Özellikler

- ✅ **Tam UTF-8 Desteği**: Türkçe, Çince, Arapça, Rusça ve tüm Unicode karakterler
- ✅ **Emoji Desteği**: 🌍 🎉 ❤️ gibi emojiler kullanılabilir
- ✅ **UTF-8 Doğrulama**: Geçersiz UTF-8 dizilerini tespit eder
- ✅ **Büyük Buffer'lar**: Çok baytlı karakterler için optimize edilmiş
- ✅ **Platform Desteği**: Linux, macOS ve Windows

## Derleme

\`\`\`bash
gcc -o nac main.c
\`\`\`

## Kullanım

\`\`\`bash
./nac test.nac
\`\`\`

## Dil Özellikleri

### Değişkenler
\`\`\`
var isim : "Değer"
var sayı : 42
\`\`\`

### Yazdırma
\`\`\`
print "Merhaba Dünya! 🌍"
print değişken
print sayı + 10
\`\`\`

### Koşullar
\`\`\`
if sayı > 40
    print "Büyük"
else
    print "Küçük"
\`\`\`