### 1. Değişken Tanımlama ve Atama

- **Değişken Tanımlama (`var`):** Değişken tanımlamak için kullanılır.
    - Sözdizimi: `var degisken_adi = deger`
    - Örnek: `var x = 5`, `var sonuc = a + b`
- **Sabit Tanımlama (`const`):** Değeri sonradan değiştirilemeyecek sabitleri tanımlamak için kullanılır.
    - Sözdizimi: `const sabit_adi = deger`
- **Atama:** Daha önce tanımlanmış bir değişkene yeni bir değer atamak için kullanılır.
    - Sözdizimi: `degisken_adi = yeni_deger`

### 2. Veri Tipleri

Kod, sayılar (tam sayılar) ve dizeleri (string) desteklemektedir.

- **Sayı (Number):** Tam sayı değerleri.
- **Dize (String):** Metinsel değerler.

### 3. Girdi/Çıktı İşlemleri

- **Ekrana Yazdırma (`print`):** Değişkenlerin veya ifadelerin değerlerini ekrana yazar.
    - Sözdizimi: `print ifade_veya_degisken`
    - Varsayılan olarak, yazdıktan sonra yeni bir satıra geçer, ancak bir sonraki komut `input` ise yeni satıra geçmez.
    - Örnek: `print "Toplam: " + sonuc`
- **Kullanıcıdan Girdi Alma (`input`):** Kullanıcıdan bir değer okur ve belirtilen değişkene atar.
    - Sözdizimi: `input degisken_adi`
    - Okunan değer sayısalsa sayı, değilse dize olarak değişkene atanır.
    - Örnek: `input y`

### 4. Matematiksel ve İlişkisel İşlemler

İfadeler içinde matematiksel, ilişkisel ve mantıksal operatörler kullanılabilir.

- **Artırma/Azaltma:** Sayı değişkenlerinin değerini 1 artırma veya azaltma.
    - Artırma: `degisken_adi++`
    - Azaltma: `degisken_adi--`
- **Sayısal İfadeler:** `a + b` gibi ifadeler desteklenir.
- **Karşılaştırma (Koşullar):** `x < 10`, `y > 5` gibi karşılaştırmalar desteklenir.
- **Dize Birleştirme:** `+` operatörü ile dize birleştirme.
    - Örnek: `print "\nToplam: " + sonuc`

### 5. Mantıksal Operatörler

`if` ve `while` gibi kontrol yapılarında koşul değerlendirmek için kullanılır.

- **VE (`and`):** Mantıksal VE (AND) operatörü.
    - Örnek: `if x < 10 and y > 5`
- **VEYA (`or`):** Mantıksal VEYA (OR) operatörü.
    - Örnek: `if x > 10 or y > 5`
- **DEĞİL (`not`):** Mantıksal DEĞİL (NOT) operatörü.
    - Örnek: `if not x > 10`

### 6. Akış Kontrol Komutları

- **Koşullu İfadeler (`if`, `else`, `end`):** Koşula bağlı kod çalıştırma.
    - Sözdizimi:
        
        ```jsx
        if koşul
            // Koşul doğruysa çalışacak kod
        else
            // Koşul yanlışsa çalışacak kod (isteğe bağlı)
        end
        ```
        
    - Örnek:
        
        ```jsx
        if x < 10 and y > 5
            print "Her iki koşul da doğru!"
        end
        ```
        
- **`for` Döngüsü:** Belirli bir sayı aralığında yineleme yapmak için kullanılır.
    - Sözdizimi: `for degisken_adi in baslangic_degeri..bitis_degeri`
    - Döngü blokları `end` ile sonlandırılır.
- **`while` Döngüsü:** Bir koşul doğru olduğu sürece kod bloğunu çalıştırmak için kullanılır.
    - Sözdizimi: `while koşul`
    - Döngü blokları `end` ile sonlandırılır.

### 7. Diziler (Array)

- **Dizi Tanımlama:** Dizi tanımlama.
    - Örnek: `var arr = [1, 2, 3, 4, 5]`
- **Elemana Erişim/Atama:** Belirli bir indeksteki elemana erişmek veya atama yapmak.
    - Erişim Örneği: `print arr[0]`
    - Atama Örneği: `arr[index] = yeni_deger` (main.c'de destekleniyor)
- **Dizi Metotları:**
    - `.length`: Dizinin uzunluğunu döndürür.
        - Örnek: `print arr.length`
    - `.push(deger)`: Dizinin sonuna bir eleman ekler.
        - Örnek: `arr.push(8)`
    - `.pop()`: Dizinin sonundaki elemanı çıkarır.
        - Örnek: `arr.pop()`

### 8. Fonksiyonlar

- **Fonksiyon Tanımlama (`func`, `end`):** Tekrar kullanılabilir kod blokları tanımlamak için.
    - Sözdizimi:
        
        ```jsx
        func fonksiyon_adi(parametre1, parametre2, ...)
            // Fonksiyon gövdesi
        end
        ```
        
    - Örnek:
        
        ```jsx
        func topla(a, b)
            var sonuc = a + b
            print "\nToplam: " + sonuc
        end
        ```
        
- **Fonksiyon Çağırma:** Tanımlanan fonksiyonu çalıştırmak için.
    - Sözdizimi: `fonksiyon_adi(arguman1, arguman2, ...)`
    - Örnek: `topla(5, 3)`

### 9. Yorumlar

- **Tek Satırlık Yorum:** `#` karakteri ile başlar ve satırın sonuna kadar yorum olarak kabul edilir.
    - Örnek: `# Mantıksal operatörler testi`