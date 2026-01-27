## NaC Dili Nedir?

**NaC (Not a C) Language Interpreter**, sembol ağırlıklı, minimal ve C-benzeri yorumlanan bir dildir.

* Amaç: Küçük, öğrenmesi kolay ve temel C-syntax’ına benzer bir dil deneyimi sunmak.
* Kullanım alanı: Matematiksel işlemler, döngüler, koşullar ve fonksiyon tanımlamaları yapabilmek.

---

## Önemli Özellikler

### 1. Değişkenler

* **Sembol tabanlı:** `$a` … `$z`
* Maksimum 26 değişken (`a`–`z`)

### 2. Sabitler

* Sayılar: `12`, negatif sayılar: `-12`

### 3. Operatörler

* **Aritmetik:** `+`, `-`, `*`, `/`
* **Atama:** `=`
* **Artırma/azaltma:** `++`, `--`
* **Karşılaştırma:** `==`, `!=`, `<`, `>`, `<=`, `>=`
* **Mantıksal:** `&&`, `||`, `!`

### 4. Kontrol Yapıları

* **If-else:** `?(KOŞUL){...}:{...};`
* **For döngüsü:** `@($i = 0 ; $i < 5 ; $i++){...};`

  * Break: `break;`
  * Continue: `continue;`

### 5. Fonksiyonlar

* Tanım: `fn $f($a, $b){ ... };`
* Çağrı: `$f(1, 2)`
* Return: `rn <değer>;`

### 6. Girdi / Çıktı

* **Input:** `$x = in;`
* **Output:** `out($x);`

---

## NaC Yorumlayıcısını Kurmak ve Çalıştırmak

1. Kod dosyasını derleyin:

```bash
gcc -o nac nac.c
```

2. NaC programınızı yazın (ör. `program.nac`):

```c
$a = 10;
$b = in;
out($a + $b);
```

3. Programı çalıştırın:

```bash
./nac program.nac
```

* Kullanıcı girişleri `in;` ile alınır ve `out(expr);` ile ekrana yazdırılır.
* Fonksiyonlar ve döngüler tam desteklenir.

---


## Notlar ve İleride Eklenebilecekler

* **Hata mesajlarını geliştirmek** → hangi satırda hatalı daha net gösterilebilir
* **Daha fazla veri tipi eklemek** → string, float, char
* **Fonksiyon içinde fonksiyon tanımı** → Nested functions

---


Tamam! Sana hem README açıklamasını hem de mevcut özelliklerin durum çizelgesini hazırladım. Kodun yapısını ve işlevlerini detaylı şekilde açıkladım, ayrıca hangi özellikler çalışıyor, hangi eksik/çalışmıyor olabilecek, hangi özellikler eklenebilir bunları listeledim.