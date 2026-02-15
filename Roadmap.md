# **NaC Language Roadmap**

## **Stage 1 – Basic Language Development** (v3) - IN PROGRESS

Objective: To facilitate and improve the reliability of basic language use.

**Remaining Tasks:**

1. **Better language**
   * Modular compilation
   * Add Structures (structs) 
   * Better scoping
      * True lexical scoping
      * Nested blocksI
   * Debugging support
      * Stack traces
      * Breakpoints

2. **Add maps/dictionaries**
   * Maps/dictionaries (`map[key] = value`)

3. **Optimize lexer**
   * Build token stream once (instead of on-demand)
   * Improves performance for large programs

4. **Optional: Type annotations**
   * `int`, `float`, `string`, `array` (static hints, not enforced yet)

---

## **Stage 2 – Network and Web API Support** (v4)

Objective: To make NaC usable in web and network applications.

2. **JSON support**

   * Parse JSON string → NaC objects
   * Stringify NaC objects → JSON string
   * Integration with HTTP responses

3. **Optional: Async / concurrent requests**

   * Non-blocking HTTP requests
   * Callbacks or promise-like structure

---

## **Stage 3: Forward-looking ideas**

* Event loop / async I/O
* Networking sockets / WebSockets
* Package manager / module system