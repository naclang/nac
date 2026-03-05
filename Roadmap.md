# **NaC Language Roadmap**

## **Stage 1 - Basic Language Development** (v3) - COMPLETED

Objective: To facilitate and improve the reliability of basic language use.

**Completed in this update:**

1. **Maps/dictionaries added**
   * Create maps with `map()`
   * Read/write with index syntax (`m[key] = value`, `m[key]`)
   * `length(map)` support

---

## **Stage 2 - Network and Web API Support** (v4)

Objective: To make NaC usable in web and network applications.

1. **JSON support**
   * Parse JSON string -> NaC objects
   * Stringify NaC objects -> JSON string
   * Integration with HTTP responses

2. **Optional: Async / concurrent requests**
   * Non-blocking HTTP requests
   * Callbacks or promise-like structure

---

## **Stage 3: Forward-looking ideas**

* Event loop / async I/O
* Networking sockets / WebSockets
* Package manager / module system
