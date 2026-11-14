# NaC Language — Syntax Reference

NaC sits between JavaScript and Python: braces for blocks (JavaScript), but
no semicolons, `and`/`or`/`not`, `for x in list`, `print`, `input`, `elif`
(Python). This document describes what the parser actually supports.

```nac
fn greet(name) {
    return `Hello, ${name}!`
}

people = ["Ada", "Turing", { name: "Grace" }]
for p in people {
    if type(p) == "map" { print(greet(p.name)) } else { print(greet(p)) }
}
```

## 1) Comments and statements

```nac
// single-line comment
/* block comment,
   can span lines */

x = 1        // no semicolon needed
y = 2; z = 3 // ';' is only a separator for several statements on one line
```

A `(` or `[` at the start of a new line is never glued to the previous line.

## 2) Keywords

| Group     | Keywords                                       |
| --------- | ---------------------------------------------- |
| Functions | `fn`, `return`                                 |
| Control   | `if`, `elif`, `else`, `for`, `in`, `while`, `break`, `continue` |
| Logic     | `and`, `or`, `not`                             |
| Output    | `print`                                        |
| Errors    | `try`, `catch`, `throw`                        |
| Web       | `route`                                        |
| Literals  | `true`, `false`, `null`                        |

Everything else (`input`, `len`, `range`, `serve`, `static`, `html`, `json`, ...)
is an ordinary builtin function, so it never collides with your variable names.

## 3) Data types

| Type   | Example                          |
| ------ | -------------------------------- |
| int    | `42`, `-7`                       |
| float  | `3.14`                           |
| bool   | `true`, `false`                  |
| null   | `null`                           |
| string | `"hello"` (escapes `\n \t \\ \"`), `` `Hi ${name}` `` |
| array  | `[1, 2, 3]`, `[]`, `array(5)` (5 zeros) |
| map    | `{ name: "Ada", "age": 36 }`, `{}`, `map()` |
| function | `fn(a, b) { return a + b }`, or the bare name of a declared function |

`type(x)` returns `"int" "float" "bool" "null" "string" "array" "map" "function"`.
Strings are limited to 8192 characters, arrays to 10,000 elements.

### Template strings (interpolation)

Backtick strings embed any expression with `${ }` (like JavaScript). Plain
`"..."` strings never interpolate, so braces in JSON text are safe.

```nac
name = "Ada"
print(`Hello ${name}! ${age + 1} next year, ${user.tags[0]}, ${len(items)} items`)

html(`<h1>${title}</h1>
<p>${count > 1 and "many" or "one"}</p>`)   // may span several lines
```

Values are rendered like `print` does (`true`, `null`, ...). Escapes inside
backticks: `\n \t \\ \` \$` (use `\${` for a literal `${`). Templates nest:
`` `a ${ `<${x}>` } b` ``. Internally a template is just a concatenation, so the
result is still limited to 8192 characters.

## 4) Variables

Assignment is declaration — no `let`/`var`:

```nac
x = 5
x += 1          // also -= *= /=
x++             // x--
```

Inside a function, assigning to a name that already exists (local, then
global) updates it; otherwise a new local is created. Parameters are always
new locals. Reading an undefined variable, an out-of-range index or a missing
map key gives `null` (undefined variable and bad index also print an error).

## 5) Operators

```
or  and          logical, short-circuiting; return an operand (x = a or "default")
not              negation (also !)   (&& || ! are accepted too)
== != < > <= >=  comparison (no chaining: write a < b and b < c)
in  not in       membership: 2 in [1, 2], "k" in map, "ab" in "cab"
+ -              "+" concatenates when either side is a string
* / %
-x               unary minus
```

`pow(a, b)` instead of `**`; there are no bitwise operators or ternary.

## 6) if / elif / else

Parentheses around the condition are optional:

```nac
if x < 0 {
    print("negative")
} elif x == 0 {
    print("zero")
} else {
    print("positive")
}
```

(`else if` works too.)

## 7) Loops

```nac
for item in [10, 20, 30] { print(item) }    // arrays
for key in person { print(key) }            // maps iterate their keys
for ch in "abc" { print(ch) }               // string characters
for i in range(5) { }                       // 0..4  (range(a, b) -> a..b-1)

while x > 0 { x-- }

break
continue
```

## 7a) Errors: try / catch / throw

```nac
try {
    data = jsonParse(text)          // any runtime error inside the block ...
    print(data.name)
} catch e {                         // ... jumps here; `e` is the message (a string)
    print("bad input:", e)
}
```

- `catch e`, `catch (e)` and a bare `catch { }` are all accepted.
- Catches runtime errors: invalid JSON, division/modulo by zero, array index out
  of bounds, undefined variables or functions, wrong argument counts, bad
  `http.<method>` options, stack overflow, ... Syntax errors are found while
  parsing and are never caught.
- When an error happens the rest of the `try` block is skipped (a statement that
  failed does not print, assign or call anything), including in loops and in
  functions called from the block. Works with `return`, `break` and `continue`
  inside the block, and `try` blocks can be nested.
- `throw value` raises your own error; `e` receives the value rendered as text.
  A `throw` outside any `try` stops the program with `Uncaught: ...`.
- A failed HTTP request is **not** an error: it comes back as `res.ok == false`
  with `res.error` set (see section 14).

## 8) Functions

```nac
fn add(a, b) {
    return a + b
}
print(add(3, 4))
```

- Up to 256 functions (routes count) and 10 parameters; call depth 100.
- A bare `return` returns `null`. Recursion works.
- Functions are values: `fn(params) { ... }` (no name) is an expression, and the
  bare name of a declared function evaluates to that function. Store them in
  variables or maps, or pass them as callbacks:

```nac
add = fn(a, b) { return a + b }
print(add(2, 3))                      // 5

fn double(x) { return x * 2 }
op = double                           // no parentheses: the function itself
print(op(21))                         // 42
```

- Calling through a function value is lenient like JavaScript when a *callback*
  is invoked by the runtime: missing arguments become `null`, extra ones are
  dropped. Direct calls `f(x)` still check the argument count.
- Functions have no closures: a function body sees its own parameters/locals
  and globals, not the locals of the function that created it.
- Named `fn name() { }` declarations still can't be nested.

## 9) Arrays

```nac
a = [1, 2, 3]
a[0]  a[0] = 10  a[1] += 5
grid = [[1, 2], [3, 4]]
grid[1][0] = 30                    // nested indexing, read and write
len(a)  push(a, 99)  pop(a)  first(a)  last(a)
reverse(a)  slice(a, 1, 3)  join(a, ", ")  split("a,b", ",")
```

No removal at an arbitrary index (only `pop` from the end).

## 10) Maps

```nac
person = { name: "Ada", age: 36, tags: ["math"], address: { city: "London" } }

person.name                 // same as person["name"]
person.address.city = "Paris"
person.age += 1
"name" in person            // or has(person, "name")
keys(person)  values(person)  len(person)
```

Bare identifier keys are strings. Keys must be int, float, string or bool.
No key deletion yet.

## 11) Input / output

```nac
print("a", 1, true)          // arguments joined by spaces; print() = empty line
name = input("Name? ")       // reads a line (string); null at end of input
n = int(input())             // convert explicitly

now()  sleep(ms)
```

Concatenating with `+` renders values the way `print` does (`true`, `null`).

## 12) Files

```nac
read(path)   write(path, text)   append(path, text)
```

## 13) HTTP server and site hosting

```nac
static("public")                       // "/" -> public/index.html, /app.css -> public/app.css
static("/assets", "public/assets")     // mount a directory under a URL prefix

route GET "/hello/:name" {
    return html(`<h1>Hello, ${params.name}!</h1>`)
}

route POST "/api/users" {
    data = jsonParse(body)
    return json(data, 201)
}

serve(8080)                            // blocking
```

**`route METHOD "pattern" { ... }`** — `METHOD` is `GET`, `POST`, `PUT`,
`PATCH`, `DELETE`, ... or `*` for any. Inside the body you have `method`,
`path`, `query`, `headers`, `body`, `params` and `cookie` (see 13a). The
legacy `serve(port, "handler")` function still receives the original five
arguments (`method`, `path`, `query`, `headers`, `body`).

- `:name` captures a path segment into `params.name` (URL-decoded); a trailing
  `*` captures the rest into `params["*"]`. Routes are tried in declaration order.
- At most 128 routes.

**`static(dir)` / `static(prefix, dir)`** — serves GET/HEAD from a directory;
directory URLs serve `index.html`; MIME type from the extension; `..` is
rejected. Files are sent as raw bytes (up to 32 MB each, not limited to 8192
characters). Up to 8 mounts.

**Resolution order:** routes, then static mounts, then `405` (the path matches
a route for another method) or `404`.

**Responses.** A route returns a string (200; `text/html` if it starts with
`<`, else `text/plain`) or a map with `status`, `body` and `headers`, or one of:

```nac
html(body)        html(body, status)
json(value)       json(value, status)       // application/json
redirect(url)     redirect(url, status)     // 302 by default
```

Dynamic bodies and header values are limited to 8192 characters; handler
calls are serialized behind one lock (a slow route delays the others).
Request size, time and rate limits, plus default security headers, are
described in 13a.

## 13a) Cookies, sessions and security

Inside a `route`, `cookie` is a map of request cookie names to values, so
`Cookie: sid=abc; theme=dark` becomes `cookie.sid` and `cookie.theme`.
Values are kept raw (not URL-decoded); call `urlDecode()` when needed.

Set cookies from inside a route. Each call appends one `Set-Cookie` header to
the current response (at most 16 per response):

```nac
route POST "/login" {
    sid = sessionId()                    // 32 random bytes, base64url (43 chars)
    setCookie("sid", sid, {
        path: "/",
        httpOnly: true,                  // default true
        secure: false,                   // true behind HTTPS
        sameSite: "Lax",                 // "Lax" (default), "Strict", "None", or false
        maxAge: 86400                    // seconds; omitted by default
    })
    return json({ ok: true })
}

route POST "/logout" {
    clearCookie("sid", { path: "/" })
    return json({ ok: true })
}
```

Cookie defaults: `path: "/"`, `httpOnly: true`, `secure: false`,
`sameSite: "Lax"`, and no `Max-Age`/`Expires`. `sameSite: "None"`
automatically turns on `Secure`. `clearCookie` emits `Max-Age=0` and an
`Expires` date in the past. Names must be HTTP tokens, values may not contain
`;` or control characters, and both functions return `false` on invalid
input. `expires` (an HTTP date string) and `domain` are also accepted.

Security primitives (built in, no imports):

| Function | Result |
| --- | --- |
| `randomBytes(n)` | `2*n` lowercase hex characters from a CSPRNG; `n` is 1..4095 |
| `uuid()` | random (v4) UUID string |
| `sessionId()` | 32 random bytes as 43-character base64url text |
| `sha256(s)` | SHA-256 of `s` as 64 lowercase hex characters |
| `hmac(key, message)` | HMAC-SHA256 as 64 lowercase hex characters |
| `base64Encode(s)` / `base64Decode(s)` | standard base64 with `=` padding |
| `safeEqual(a, b)` | constant-time string comparison, returns a bool |
| `passwordHash(password)` | salted PBKDF2-HMAC-SHA256 string |
| `passwordVerify(password, hash)` | constant-time check against a `passwordHash` value |

`passwordHash` uses a random 16-byte salt, 210,000 PBKDF2 iterations and a
32-byte key, stored as `pbkdf2-sha256$<iterations>$<salt_b64>$<hash_b64>`.
The format is self-describing, so the cost can be raised later without
invalidating existing hashes (`passwordVerify` accepts 1,000..1,000,000
iterations). On Unix the backend is OpenSSL; on Windows it is CNG/BCrypt.

**Sessions.** There is no hidden session store: generate the ID on the server,
keep it in a global map, and carry it in an `HttpOnly` cookie. The cookie value
alone is not trusted; look it up and check expiry on every request.

```nac
sessions = {}                            // sid -> { username, expires }
SESSION_TTL = 86400

route POST "/login" {
    // ... verify the password with passwordVerify(...)
    sid = sessionId()
    sessions[sid] = { username: user.username, expires: now() + SESSION_TTL }
    setCookie("sid", sid, { maxAge: SESSION_TTL, httpOnly: true, sameSite: "Lax" })
    return json({ ok: true })
}

route GET "/me" {
    sid = cookie.sid
    if isNull(sid) or type(sid) != "string" or sid == "" {
        return json({ error: "authentication required" }, 401)
    }
    s = sessions[sid]
    if isNull(s) or s.expires <= now() {
        return json({ error: "authentication required" }, 401)
    }
    return json({ username: s.username })
}
```

See `examples/web/06_accounts.nac` for register/login/logout, protected
routes, role checks and a JSON-file user store.

**Server hardening (always on).**

| Limit | Default |
| --- | --- |
| Request header block | 16 KiB, else `431` |
| Request body | 8191 bytes, else `413` |
| Whole-request read deadline | 10 seconds, else `408` |
| Requests per client IP | 120 per 60-second fixed window, else `429` + `Retry-After` |
| Concurrent connections | 64 |

Every dynamic and static response also carries
`X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY` and
`Referrer-Policy: no-referrer`. Response header names or values containing
CR/LF are dropped, so a script-supplied value cannot inject extra headers.
The rate limiter keeps up to 1024 client IPs in memory and uses the socket
address, not `X-Forwarded-For`; behind a reverse proxy all requests look like
they come from the proxy unless the proxy is configured accordingly.

## 14) HTTP client and JSON

```nac
res = http.get { url: "https://api.example.com/users/1" }
print(res.status, res.ok, res.data.name)

res = http.post {
    url: "https://api.example.com/users",
    body: { name: "Ada" },                  // map/array -> sent as JSON; string -> sent as-is
    headers: { "Authorization": "Bearer abc" },
    timeout: 10                             // seconds, default 30
}

http.get {
    url: "https://api.example.com/users",
    callback: fn(r) { print(len(r.data), "users") }
}

jsonParse(text)   jsonStringify(value)
```

**`http.<method> { options }`** -- `<method>` is `get`, `post`, `put`, `patch` or
`delete`; the options map must follow the method on the same line. Options:

| Key        | Meaning                                                              |
| ---------- | -------------------------------------------------------------------- |
| `url`      | required, `http://` or `https://`                                    |
| `body`     | string (sent as-is) or map/array (sent as JSON); not allowed for `get` |
| `headers`  | map of header name -> value (`Content-Type: application/json` is added when there is a body, unless you set it) |
| `timeout`  | seconds, default 30                                                  |
| `callback` | function called with the response when it arrives                    |

**The response** is a map, so it can be assigned to a variable:

| Field    | Meaning                                                         |
| -------- | --------------------------------------------------------------- |
| `status` | HTTP status code, `0` if no response was received               |
| `ok`     | `true` for 2xx                                                  |
| `body`   | response text (limited to 8192 characters)                      |
| `data`   | the body parsed as JSON when it is JSON, otherwise `null`       |
| `error`  | `null`, or a message when the request itself failed (no connection, timeout, ...) |

HTTP 4xx/5xx and network failures do **not** stop the script: check `res.ok`
(and `res.error`).

**Callbacks** are called once the response is in, before the `http.<method>`
expression finishes (the call is still blocking -- true async is on the
roadmap). The response is also the value of the expression, so you can use a
callback and assign the result at the same time. A callback can be an inline
`fn(r) { ... }` or the name of a declared function (`callback: onDone`).

## 15) Conversion and builtins

```nac
str(x) int(x) float(x) type(x) isNull(x)         // Python-style
toString toInt toFloat typeOf                     // camelCase aliases
urlEncode(s) urlDecode(s)
args()  env("NAME")  exit(code)                   // CLI tools
sqrt pow sin cos tan abs floor ceil round log exp
len upper lower trim replace substr indexOf contains split
```

## 16) Removed in v1.1 (old syntax)

`rn` (use `return`), `out(...)` (use `print`), `in(x)` (use `input()`),
`: { }` as else (use `else`/`elif`), C-style `for (i = 0; ...; i++)` (use
`for i in range(...)` or `while`), `http("GET", url)` (use `http.get { url: ... }`),
`time()` (use `now()`), `serve(port, "handler")` (use `route`), mandatory `;`.

## 17) Removed: the old HTTP client functions

`httpGet(url)`, `httpPost(url, body)`, `httpPut(url, body)`, `httpPatch(url, body)`,
`httpDelete(url)`, `httpRequest(method, url)` and `httpJson(method, url)` are gone.
Use `http.get { url: ... }`, `http.post { url: ..., body: ... }` etc. -- the parsed
JSON that `httpJson` returned is `res.data`, the raw text is `res.body`.
