# **NaC Language Roadmap**

## **Stage 1 – Basic Language Development**

Objective: To facilitate and improve the reliability of basic language use.

1. **String operations**

   * Concatenation (`+` veya `concat()` fonksiyonu)
   * Substring (`substr()`)
   * Length (`len()`)
   * Split (`split(delimiter)`)

2. **Better scoping**

   * True lexical scoping
   * Nested blocks
   * Local variable shadowing

3. **Debugging support**

   * Stack traces
   * Breakpoints
   * Variable inspection (`inspect(var)`)

4. **Standard library**

   * Math functions (`sin`, `cos`, `sqrt`, `pow`, etc.)
   * File I/O (`read_file()`, `write_file()`)
   * String utilities (`trim`, `replace`, `lower`, `upper`)

5. **Add arrays/maps**

   * Arrays (dynamic size)
   * Maps/dictionaries (`map[key] = value`)
   * Array methods (`push`, `pop`, `insert`, `remove`)

6. **Optimize lexer**

   * Build token stream once (instead of on-demand)
   * Improves performance for large programs

7. **Optional: Type annotations**

   * `int`, `float`, `string`, `array` (static hints, not enforced yet)

---

## **Stage 2 – Network and Web API Support**

Objective: To make NaC usable in web and network applications.

1. **HTTP requests**

   * HTTP GET
   * HTTP POST
   * Custom headers

2. **JSON support**

   * Parse JSON string → NaC objects
   * Stringify NaC objects → JSON string
   * Integration with HTTP responses

3. **Optional: Async / concurrent requests**

   * Non-blocking HTTP requests
   * Callbacks or promise-like structure

---

💡 **Forward-looking ideas (Stage 3)**

* Event loop / async I/O
* Networking sockets / WebSockets
* Package manager / module system
* REPL ve IDE support