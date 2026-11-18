# NaC Language Roadmap

NaC is a lightweight web-server language focused on building APIs, websites, and backend services with minimal code and a single deployable binary.

## Design Direction

NaC should remain a simple, lightweight web-server language rather than becoming a general-purpose object-oriented language.

Performance claims will be based on reproducible benchmarks, not assumptions.

## Non-goals

NaC stays a small, single-binary web-server language. These are deliberately out of scope:

- **Native TLS.** Certificates, renewal, and cipher policy belong in a reverse proxy or load balancer (nginx, Caddy, a cloud LB). NaC stays plain HTTP behind it; see [the deployment section](https://naclang.github.io/docs/server.html#deployment).
- **HTTP/2 and HTTP/3.** A reverse proxy already negotiates these with clients and speaks HTTP/1.1 to NaC. Implementing them here would add a large protocol state machine for no user-visible gain.
- **General-purpose object-oriented features.** No classes, inheritance, or a full type system. Maps and functions (and later lightweight records) cover web-server data; NaC is not trying to become a general-purpose language.
- **Unbounded concurrency.** Handler calls are serialized by design, so scripts never need locks. NaC keeps a bounded execution model (worker pools, and later narrower locking) instead of promising arbitrarily parallel user code; scale out with multiple processes.

## Roadmap

### 1. Performance

1. **Benchmark results:** Run the suite in `bench/` on reference hardware (NaC vs. Node.js, Python and Go) and publish the measured results with the commands and raw output.
2. **Efficient function calls:** Resolve functions at parse time and replace per-call 256-bucket variable tables with lightweight variable slots.
3. **HTTP connection management:** Port the Linux/macOS worker thread pool (`--workers`) with its `poll()` thread for idle connections to Windows (`server_win.c` still uses a thread per connection). Keep-alive is already implemented on both platforms.
4. **Interpreter concurrency:** Reduce the global handler lock while preserving safe shared-state access (request read timeouts are already implemented).
5. **Static file streaming:** Add Unix `sendfile` support so large files are sent without a full user-space copy. gzip is optional: most deployments compress at the reverse proxy.

### 2. Language Features

1. **Lightweight records:** Add `record` / `struct` syntax for simple named data objects without a full object-oriented system.
2. **Closures:** Allow functions to capture variables from their defining scope. Async callbacks (2.3) are much more useful with closures, so implement them together.
3. **Async:** Introduce an event loop, non-blocking HTTP and file I/O, timers, and `await`-style syntax. This is also how outgoing HTTP stops blocking the interpreter: a non-blocking HTTP client cannot be built independently of the event loop.
4. **Real-time communication:** Add Server-Sent Events (SSE) first, a cheap streaming option on top of the existing HTTP server, then WebSockets, followed by optional TCP and UDP. HTTP/2 is left to the reverse proxy (see Non-goals).
5. **Error handling:** Add stack traces and `finally` blocks (`try` / `catch` / `throw` and line/column error locations are implemented).
