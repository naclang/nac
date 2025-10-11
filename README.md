# NaturalizedC Interpreter

This project is a simple scripting language interpreter named **NaturalizedC (NAC)**. It is written in C and primarily supports **variable assignment**, **print** statements, **if-else** structures, and **concatenation of string/number expressions**.

---

## Features

* Variable definition with `var`:

  * Numerical (`int`) or string (`"..."`) values.
  * String concatenation using `+` is supported in expressions:

    ```nac
    var x = 5
    var y = "This number: " + x
    print y
    ```

    Output:

    ```
    This number: 5
    ```

* Printing values or expressions with `print`:

  * Supports variables or string concatenations.
  * Escape sequences like `\n` (newline), `\t` (tab), `\\`, `\"` are supported inside strings.
  * **Improved input handling:** if a `print` statement is immediately followed by an `input`, the cursor stays on the same line:

    ```nac
    print "Enter your age: "
    input age
    print "You entered: " + age
    ```

    Output:

    ```
    Enter your age: 30
    You entered: 30
    ```

* Simple conditional statements (`if`, `else`):

  * Only supports numerical comparisons.
  * Comparison operators: `==`, `!=`, `>`, `<`, `>=`, `<=`.

    ```nac
    if x > 3
        print "x is greater than 3"
    else
        print "x is less than or equal to 3"
    ```

* UTF-8 character support:

  * File and console output are handled as UTF-8.
  * Supports Turkish characters and emojis.

---

## Usage

1. **Compilation:**

   ```bash
   gcc -o nac.exe main.c
   ```

2. **Running:**

   ```bash
   ./nac.exe test.nac
   ```

3. **Example `test.nac` file:**

   ```nac
   var x = 10
   var y = "Value: " + x
   print y

   print "Enter a number: "
   input num
   print "You typed: " + num

   if num > 5
       print "The number is greater than 5"
   else
       print "The number is less than or equal to 5"
   ```

---

## Limitations

* Maximum 100 variables are supported.
* Maximum string length is 1024 characters.
* Only numerical comparisons work within `if` statements.
* Operator precedence or parentheses are not supported in complex expressions; operations are executed from left to right.

---

## Windows Support

* Console supports UTF-8 output.
* Virtual terminal and binary stdout settings have been configured.
* Input can now appear on the same line as a preceding `print`.
