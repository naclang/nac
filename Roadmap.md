# **NaC Language Roadmap**

## **Stage 3: Advanced Core & Asynchrony (post-1.0, not yet started)**

These are meaningfully larger undertakings than compound assignment was —
each touches the interpreter's execution model, the network layer, or both
— so they're deliberately scoped for a v1.1/v2 cycle rather than v1.0.

1. **Further concurrency work**

- A way to opt a specific handler out of the shared execution lock for pure computation with no shared-state access (advanced/optional).
- Basic request timeouts (a slow handler currently blocks other requests for as long as it runs).

2. **Async / Concurrent Architecture**

- Non-blocking HTTP requests: Ability to fire requests without freezing the main execution thread.
- Promise-like structure / Await: High-level syntax for handling asynchronous operations.
- Event Loop: A centralized manager to handle I/O events, timers, and callbacks efficiently.
- Async I/O: Support for non-blocking file system operations and input/output streams.

3. **Networking & Real-time Communication**

- Low-level Sockets: TCP/UDP support for building custom protocols or servers.
- WebSockets: Full-duplex communication channels over a single TCP connection for real-time apps.
- HTTP/2 Support: (Optional) Support for modern web performance standards.

4. **Web backend ergonomics**

- Path parameters / lightweight router helper (e.g. matching `/users/:id`) instead of manual `if` chains.
- Static file serving helper.
- Dynamic (heap-growable) strings, removing the fixed 8192-character
  ceiling on bodies/headers. This is also the root cause of a stack-usage
  issue fixed in v1.0.0 (see the "shipped" section above): `Value`
  embeds that 8192-byte buffer inline, making `sizeof(Value)` ~8.2KB, so
  every stack-allocated `Value` in the recursive eval/call-function path
  is expensive in raw C stack. v1.0.0 worked around the symptom (silent
  crashes) by requesting a much bigger stack at process startup; making
  strings heap-backed would fix the actual inefficiency and shrink
  `Value` by roughly two orders of magnitude, likely worth doing before
  this stack-size workaround is leaned on much further.

5. **Further language ergonomics**

- Map literal syntax (e.g. `{"a": 1}`) instead of `map()` + one assignment per key.
- Multi-level indexing (`arr[i][j]`, `obj["a"]["b"]`) — needs `array_access`/
  `array_assign` to hold a chain of indices (or a general indexable target
  expression) instead of a single `var_name` + one index, which is a parser
  and evaluator change, not just a lexer addition like compound assignment
  was.
- First-class functions / callbacks (passing a function as a value, not just by name string).

