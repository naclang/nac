# NaC Language Interpreter v3.0.0

A lightweight, interpreted scripting language implemented in C. NaC (Not a C) is designed to be simple, expressive, and easy to embed for quick scripting tasks. This interpreter supports variables, arrays, functions, loops, conditionals, and basic I/O.

---

## Features

* **Data Types:** Integers, floats, strings, arrays.
* **Operators:** Arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical (`&&`, `||`, `!`).
* **Control Flow:** `if-else`, `for` loops, `break`, `continue`.
* **Functions:** User-defined with parameters and `rn` (return) statements.
* **I/O:** `in()` for input, `out()` for output.
* **Arrays:** Dynamic arrays using `array(n)` or literal `[1, 2, 3]`.
* **Time:** `time()` returns the current Unix timestamp.
* **Error Reporting:** Line and column-specific messages.
* **Increment/Decrement:** `++` and `--` operators.

---

## Installation

Requires a C compiler (tested with `gcc`):

```bash
gcc -o nac nac.c -lm
```

---

## Usage

Run a NaC script:

```bash
./nac program.nac
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

* Return values using `rn`:

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

### Loops

```nac
for (i = 0; i < 5; i++) {
    out(i);
};
```

* Use `break;` to exit loops and `continue;` to skip to next iteration.

### I/O

```nac
in(x);
out(x);
```

* Input supports integers, floats, and strings automatically.

### Time

```nac
current = time();
out(current);  // Prints current Unix timestamp
```

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