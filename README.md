# NaC Language Interpreter v3.1.0

A lightweight, interpreted scripting language implemented in C with HTTP support. NaC (Not a C) is designed to be simple, expressive, and easy to embed for quick scripting tasks, web automation, and API testing.

---

## Features

* **Data Types:** Integers, floats, strings, arrays.
* **Operators:** Arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical (`&&`, `||`, `!`).
* **Control Flow:** `if-else`, `for` loops, **`while` loops**, `break`, `continue`.
* **Functions:** User-defined with parameters and `rn` (return) statements.
* **I/O:** `in()` for input, `out()` for output.
* **Arrays:** Dynamic arrays using `array(n)` or literal `[1, 2, 3]`.
* **HTTP Requests:** **NEW!** Built-in HTTP client (GET, POST, PUT, DELETE).
* **Time:** `time()` returns the current Unix timestamp.
* **Error Reporting:** Line and column-specific messages.
* **Increment/Decrement:** `++` and `--` operators.
* **Cross-Platform:** Works on Windows, Linux, and macOS.

---

## Installation

### Windows (MinGW)

```bash
gcc -o nac.exe nac.c -lm -lwinhttp
```

### Linux

```bash
# Install libcurl (Ubuntu/Debian)
sudo apt-get install libcurl4-openssl-dev

# Compile
gcc -o nac nac.c -lm -lcurl
```

Or use the build script:

```bash
chmod +x build_linux.sh
./build_linux.sh
```

### macOS

```bash
# libcurl is usually pre-installed
gcc -o nac nac.c -lm -lcurl
```

---

## Usage

Run a NaC script:

```bash
# Linux/macOS
./nac program.nac

# Windows
nac.exe program.nac
```

Example `program.nac`:

```nac
fn add(a, b) {
    rn a + b;
};

x = 10;
y = 20;
z = add(x, y);
out(z);  // Output: 30
```

---

## Syntax Overview

### Variables

```nac
x = 5;
y = 3.14;
name = "NaC";
```

### Arrays

```nac
arr = array(5);  // Creates [0, 0, 0, 0, 0]
nums = [1, 2, 3, 4];
arr[0] = 42;
out(arr[0]);
```

### Functions

```nac
fn greet(name) {
    out("Hello, " + name + "!");
};

greet("Alice");  // Output: Hello, Alice!
```

Return values using `rn`:

```nac
fn square(x) {
    rn x * x;
};
out(square(5));  // Output: 25
```

### Conditionals

```nac
if (x > 10) {
    out("Greater than 10");
} : {
    out("10 or less");
};
```

### For Loops

```nac
for (i = 0; i < 5; i++) {
    out(i);
};
```

### While Loops (NEW!)

```nac
x = 0;
while (x < 5) {
    out(x);
    x++;
};
```

With break and continue:

```nac
n = 0;
while (n < 10) {
    n++;
    if (n == 3) {
        continue;  // Skip 3
    };
    if (n == 7) {
        break;     // Stop at 7
    };
    out(n);
};
```

### HTTP Requests (NEW!)

```nac
// GET request
http("GET", "https://api.ipify.org/?format=json");

// POST request with JSON body
http("POST", "https://httpbin.org/post", "{\"name\":\"John\",\"age\":30}");

// PUT request
http("PUT", "https://api.example.com/users/1", "{\"status\":\"active\"}");

// DELETE request
http("DELETE", "https://api.example.com/users/1");
```

HTTP with loops:

```nac
i = 0;
while (i < 3) {
    out("Request #" + i);
    http("GET", "https://api.ipify.org/?format=json");
    i++;
};
```

**Platform Support:**
- **Windows**: Uses WinHTTP API
- **Linux/macOS**: Uses libcurl
- Supports HTTP and HTTPS
- Automatic redirect following
- Response printed to stdout

### I/O

```nac
in(x);
out(x);
```

Input supports integers, floats, and strings automatically.

### Time

```nac
current = time();
out(current);  // Prints current Unix timestamp
```

---

## Built-in Functions

### Math Functions
- `sqrt(x)` - Square root
- `pow(x, y)` - Power
- `sin(x)`, `cos(x)`, `tan(x)` - Trigonometry
- `abs(x)` - Absolute value
- `floor(x)`, `ceil(x)`, `round(x)` - Rounding
- `log(x)`, `exp(x)` - Logarithm and exponential

### String Functions
- `length(str)` - String/array length
- `upper(str)`, `lower(str)` - Case conversion
- `trim(str)` - Remove whitespace
- `substr(str, start, len)` - Substring
- `indexOf(str, search)` - Find substring
- `replace(str, old, new)` - Replace text

### Array Functions
- `push(arr, val)` - Add element
- `pop(arr)` - Remove last element
- `first(arr)`, `last(arr)` - Get first/last
- `reverse(arr)` - Reverse array
- `slice(arr, start, end)` - Get slice
- `join(arr, sep)` - Join to string

### File Functions
- `read(filename)` - Read file
- `write(filename, content)` - Write file
- `append(filename, content)` - Append to file

---

## Error Handling

NaC reports errors with line and column numbers:

```
Error (Line 3, Column 7): Undefined variable: x
```

Execution stops after 10 errors to prevent excessive runtime issues.

---

## Limitations

* Maximum functions: 100
* Maximum function parameters: 10
* Maximum call stack depth: 100
* Maximum array size: 10,000 elements
* Strings limited to 1024 characters
* HTTP timeout: Platform default (WinHTTP or libcurl)