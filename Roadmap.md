# NaC Language Roadmap

NaC is a lightweight web-server language focused on building APIs, websites, and backend services with minimal code and a single deployable binary.

## Design Direction

NaC should remain a simple, lightweight web-server language rather than becoming a general-purpose object-oriented language.

Performance claims will be based on reproducible benchmarks, not assumptions.

## Roadmap

### 1. Core Web Features

1. **Middleware:** Add `before { ... }` and `after { ... }` blocks for authentication, logging, and CORS.
2. **Backend primitives:** Add JWT signing and verification, date/time formatting, and SQLite support.
3. **Operations:** Add graceful shutdown, health-check endpoints, `--port` / `--host` flags, and configurable runtime limits.
4. **TLS:** Provide native TLS support or a documented reverse-proxy setup.

### 2. Performance

5. **Benchmark suite:** Create a `bench/` directory covering hello-world, JSON echo, static files, and outgoing HTTP requests. Compare NaC against Node.js, Python, and Go using `wrk` and publish reproducible results.
6. **Heap-backed strings:** Replace large inline string buffers with heap allocation, reduce `Value` size, and remove the 8192-character body limitation.
7. **Efficient function calls:** Resolve functions at parse time and replace per-call 256-bucket variable tables with lightweight variable slots.
8. **HTTP connection management:** Implement HTTP keep-alive and a thread pool.
9. **Interpreter concurrency:** Reduce the global handler lock while preserving safe shared-state access. Add request timeouts.
10. **Static file optimizations:** Add in-memory caching, ETag, If-Modified-Since, Unix `sendfile`, and gzip support.
11. **Non-blocking HTTP:** Allow outgoing HTTP requests without blocking the interpreter.

### 3. Language Features

12. **Lightweight records:** Add `record` / `struct` syntax for simple named data objects without a full object-oriented system.
13. **Closures:** Allow functions to capture variables from their defining scope.
14. **Async:** Introduce an event loop, non-blocking HTTP and file I/O, timers, and `await`-style syntax.
15. **Real-time communication:** Add WebSockets and Server-Sent Events, followed by optional TCP, UDP, and HTTP/2 support.
16. **Error handling:** Add line numbers, stack traces, and `finally` blocks.
17. **Data utilities:** Add map-key deletion, array-index removal, date/formatting utilities, and larger or unbounded arrays.
