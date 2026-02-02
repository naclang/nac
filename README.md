# 🚀 NaC Language (v2.0.2)

**NaC (Not a C)**, C dilinin gücünü sembolik bir minimalizmle birleştiren, yorumlanan (interpreted) bir betik dilidir.

## 📋 Genel Bakış

NaC, hızlı prototipleme ve temel programlama mantığını öğretmek için tasarlanmıştır. Güçlü bir tür desteği (int, float, string) ve fonksiyonel bir yapı sunar.

### 🛠 Derleme ve Çalıştırma

Yorumlayıcıyı derlemek için standart bir C derleyicisi yeterlidir:

```bash
gcc -o nac nac.c -lm
./nac program.nac

```

---

## 💎 Dilin Temel Özellikleri

### 1. Değişkenler ve Veri Türleri

NaC dilinde değişkenler `$` sembolü ile başlar ve tek karakterlidir (`$a`, `$b`, ..., `$z`). Üç ana veri türü desteklenir:

* **Integer:** `$a = 10;`
* **Float:** `$b = 3.14;`
* **String:** `$c = "Merhaba NaC";`

### 2. Operatörler

* **Aritmetik:** `+`, `-`, `*`, `/`, `%`
* **Karşılaştırma:** `==`, `!=`, `<`, `>`, `<=`, `>=`
* **Mantıksal:** `&&` (ve), `||` (veya), `!` (değil)
* **Artırma/Azaltma:** `++`, `--`

> [!TIP]
> **String Sihri:** NaC dilinde stringleri `+` ile birleştirebilir veya `*` ile çoğaltabilirsiniz.
> `$a = "Hey" * 3;` // Sonuç: "HeyHeyHey"

### 3. Kontrol Yapıları

#### If-Else (Eğer)

NaC'da `else` bloğu için `:` sembolü kullanılır:

```c
if ($a > 5) {
    out("Büyük");
} : {
    out("Küçük veya Eşit");
};

```

#### For Döngüsü

Klasik C yapısına benzer ancak sembolik dokunuşlar içerir:

```c
for ($i = 0; $i < 10; $i++) {
    out($i);
};

```

### 4. Fonksiyonlar

Fonksiyon tanımlamak için `fn`, değer döndürmek için `rn` anahtar kelimeleri kullanılır:

```c
fn $s($a, $b) {
    $c = $a + $b;
    rn $c;
};

$x = $s(5, 10);
out($x);

```

---

## 📥 Girdi ve Çıktı (I/O)

* **out(değer):** Ekrana çıktı verir.
* **in:** Kullanıcıdan veri alır. Sayısal veya metinsel girdiyi otomatik algılar.

```c
out("Adını yaz:");
$n = in;
out("Selam " + $n);

```

---

## 🧩 Dil Söz dizimi (Syntax) Tablosu

| Anahtar Kelime | Açıklama |
| --- | --- |
| `fn` | Fonksiyon Tanımlama (Function) |
| `rn` | Değer Döndürme (Return) |
| `in` | Girdi Alma (Input) |
| `out` | Çıktı Verme (Output) |
| `time` | Mevcut Unix zaman damgasını döndürür |
| `break` | Döngüyü kırar |
| `next` | Döngünün sonraki adımına geçer (Continue) |
| `:` | Else bloğunu ifade eder |

---

## 📜 Örnek Program: Faktoriyel Hesaplama

```c
fn $f($n) {
    if ($n <= 1) {
        rn 1;
    };
    rn $n * $f($n - 1);
};

out("Bir sayı girin:");
$sayi = in;
out("Sonuç:");
out($f($sayi));

```

---

**NaC** ile kodlama yaparken değişkenlerin kapsamına (scope) dikkat etmeyi unutmayın. Global değişkenler her yerden erişilebilirken, fonksiyon içindeki değişkenler o fonksiyona özeldir.