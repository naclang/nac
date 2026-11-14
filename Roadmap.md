# **NaC Language Roadmap**

NaC is a **web-server language**: the thing it should be best at is turning
"I need an endpoint / a site / a small API" into a few lines of code that run as
one small binary. Everything below is judged against that.

## **Shipped after v1.1.0**

- Security primitives: `randomBytes`, `uuid()`, `sessionId()`, `sha256`, `hmac`, `base64Encode`/`base64Decode`, constant-time `safeEqual`, and salted PBKDF2-HMAC-SHA256 `passwordHash`/`passwordVerify` (OpenSSL on Unix, CNG/BCrypt on Windows).
- Cookies and sessions: `cookie.name` in a route, `setCookie`/`clearCookie` with `HttpOnly`, `Secure`, `SameSite` and `Max-Age`, and multiple `Set-Cookie` headers per response. `examples/web/06_accounts.nac` shows register/login/logout, protected routes and role checks.
- Server hardening, on by default: 16 KiB header limit (`431`), 8191-byte body limit (`413`), 10-second whole-request deadline (`408`), per-IP rate limiting (120 requests/60 s, `429` + `Retry-After`), `nosniff`/`DENY`/`no-referrer` security headers, and rejection of CR/LF in response headers.
- New HTTP client syntax: `res = http.get { url: "..." }`, `http.post { url, body, headers, timeout, callback }`, `put`, `patch`, `delete`. The response is a map (`status`, `ok`, `body`, `data`, `error`); network failures no longer abort the script.
- First-class functions: `fn(x) { ... }` expressions, functions stored in variables/maps, declared functions passed by name, and `callback:` support (called synchronously when the response arrives).
- `try { } catch e { }` and `throw`: runtime errors can be caught; `e` is the error message.
- Removed the old `httpGet/httpPost/httpPut/httpPatch/httpDelete/httpRequest/httpJson` builtins.
- Examples reorganized around the web server use case (`examples/web/`, language tour in `examples/lang/`).

## **Shipped in v1.1.0**

- `route METHOD "/path/:param" { ... };` declarations and `serve(port)` without a handler function.
- `static("dir")` / `static("/prefix", "dir")` static file hosting (raw bytes, MIME types, `index.html`).
- `html()`, `json()`, `redirect()` response helpers; 404/405 handling.
- JavaScript/Python-style syntax: no semicolons, `elif`, `for x in ...`, `and/or/not/in`, `return`/`print`/`input`, map literals, `obj.field`, nested indexing, block comments, template strings (`` `Hi ${name}` ``); old syntax (`rn`, `out`, `in()`, `:` else, C-style `for`, `serve(port, "handler")`) removed.
- Keyword cleanup: `else` and `route` added; `time`, `array`, `http` are no longer keywords.

## **Where NaC should be easier than general-purpose languages**

Ease is the part NaC already delivers; these principles guide what gets added:

1. **The common web task is one statement.** Endpoint, static folder, JSON in/out,
   redirect, outgoing HTTP call: each is a keyword or a single builtin, not a
   framework plus imports plus configuration.
2. **Safe by default.** Handlers cannot race on shared state, `..` is rejected in
   static paths, user input does not crash the server (`try/catch`, errors become
   responses), and a failed outgoing request is a value (`res.ok`), not a crash.
3. **One small artifact.** The interpreter is one ~140 KB binary and `nac build`
   turns a script into a single executable: deployment is "copy a file".
4. **Nothing to choose.** No framework, router library, JSON library or HTTP client
   to pick between; one way to do each thing, documented in one file.

### Next, in priority order (web ergonomics)

- **Request/response helpers:** a parsed `form` for `application/x-www-form-urlencoded`
  and multipart bodies (today `examples/web/03_guestbook.nac` parses by hand),
  a `header()` helper, CORS in one line.
- **Middleware:** a `before { ... }` / `after { ... }` block for logging, auth checks
  and CORS without repeating them in every route.
- **Templates:** `render("page.html", { title: ... })` with escaping on by default
  (removes the hand-written `esc()` in the guestbook example).
- **Config and secrets:** `.env` loading, `env("PORT")` defaults, `serve()` reading the port.
- **Built-in building blocks every backend needs:** JWT sign/verify, `now()`
  formatting, a SQLite binding.
- **Developer loop:** `nac dev app.nac` (reload on save), `nac test` with `assert`
  and an in-process request helper (call a route without opening a port), request logging.
- **Operations:** graceful shutdown on SIGTERM, a `/health`-style one-liner,
  `--port` / `--host` flags, configurable limits (the defaults are baked in).
- **TLS** (or a documented reverse-proxy recipe) and **HTTP keep-alive**.

## **Where NaC should be faster — and the rule for claiming it**

NaC does **not** claim to be faster than other languages today. A tree-walking
interpreter with a global handler lock is not, and no benchmark of NaC against
other servers has been published. An informal local run of a trivial
`return json({ ok: true })` route gave roughly 2,400 requests/second, but the
load generator (a Python client) was probably the limit, so that number says
nothing about other languages.

The goal is to make *this specific workload* — many small requests that parse a
request, touch some shared data and return JSON/HTML/static bytes — fast, and to
publish numbers only after they are measured. Work, ordered by expected impact:

1. **Benchmark suite in the repo first** (`bench/`): hello, JSON echo, static file,
   route-with-outgoing-call; driven by `wrk`; compared against Node, Python and Go
   servers on the same machine, with results and methodology published, including
   where NaC loses. Nothing below is "done" until it moves these numbers.
2. **Heap-backed strings and a small `Value`.** `Value` embeds an 8192-byte
   buffer (`sizeof(Value)` ~8.2 KB), so every expression result, argument and
   return value is an 8 KB copy and every recursive eval frame is huge. Making
   strings heap-allocated (this also removes the 8192-character limit on bodies)
   should shrink `Value` by about two orders of magnitude; expected to be the
   single largest win, and it also removes the 128 MB stack workaround in `main.c`.
3. **Cheaper function calls.** Every call allocates a fresh 256-bucket variable
   table (about 2 KB) and finds the callee by scanning the function list; resolve
   functions once at parse time and use small per-call variable slots.
4. **HTTP keep-alive** (today every request opens a new connection) and a **thread
   pool** instead of one thread per connection.
5. **Concurrency inside the interpreter.** Today one lock serializes all handler
   calls. Options: per-thread interpreter state with shared data behind explicit
   primitives, or an opt-out for pure handlers; request timeouts so one slow
   handler cannot stall the others.
6. **Static files:** in-memory cache with `ETag` / `If-Modified-Since`, `sendfile`
   on Unix, gzip for text types.
7. **Non-blocking outgoing HTTP** (see Async below) so a route waiting on an
   upstream API does not hold the global lock.

## **Language and runtime (longer term)**

- **Closures:** a function value capturing the local variables of the function that
  created it (function values and callbacks themselves have shipped).
- **Async / concurrency:** non-blocking `http.get` with an event loop and
  `await`-style syntax; timers; non-blocking file I/O. Callbacks today run
  synchronously after the response arrives.
- **Real-time:** WebSockets and server-sent events (chat, live dashboards), then
  optional low-level TCP/UDP sockets and HTTP/2.
- **Errors:** error values with line numbers and a stack trace in `catch`; `finally`.
- **Data:** key deletion for maps, removal at an index for arrays, a `fmt`/`date`
  library, larger or unbounded arrays.

