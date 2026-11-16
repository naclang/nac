# NaC — Syntax Reference

NaC reads like JavaScript's braces with Python's simplicity: `{}` blocks, no
semicolons, `and`/`or`/`not`, `for x in list`, `elif`, `print`.

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
y = 2; z = 3 // ';' only separates statements on one line
```

A `(` or `[` at the start of a new line is never glued to the previous line.

## 2) Keywords

```text
fn return
if elif else for in while break continue
and or not
print
try catch throw
route
true false null
```

Everything else (`input`, `len`, `range`, `serve`, `json`, ...) is an ordinary
builtin function, so it never collides with your variable names.

## 3) Values

```nac
42              // int (32-bit)
-7
3.14            // float
true  false     // bool
null            // null
"hello"         // string; escapes: \n \t \\ \"
`Hi ${name}`    // template string
[1, 2, 3]       // array
{ name: "Ada" } // map
fn(a, b) { return a + b }   // function value
```

`type(x)` returns `"int"`, `"float"`, `"bool"`, `"null"`, `"string"`,
`"array"`, `"map"` or `"function"`.

## 4) Variables

```nac
x = 5
x += 1          // also -= *= /=
x++             // x--
```

Assignment declares the variable; there is no `let`/`var`. Inside a function,
an existing local/global is updated, otherwise a new local is created.

## 5) Operators

```nac
a or b  a and b     // short-circuit; return an operand: x = q or "default"
not a               // also !
a == b  a != b
a < b   a > b   a <= b   a >= b       // no chaining: a < b and b < c
2 in [1, 2]         // "k" in map, "ab" in "cab"
a not in b
a + b               // "+" concatenates when either side is a string
a - b   a * b   a / b   a % b
-a
```

`pow(a, b)` instead of `**`. No bitwise operators, no ternary. `&&`, `||`, `!`
are accepted too.

## 6) Strings

```nac
"Hello " + name
`Hello ${name}, ${age + 1} next year`   // may span lines; ${ } takes any expression
len(s)  upper(s)  lower(s)  trim(s)
replace(s, "old", "new")
substr(s, start, length)  indexOf(s, "x")  contains(s, "x")
split("a,b", ",")  join(["a", "b"], ", ")
urlEncode(s)  urlDecode(s)
```

Plain `"..."` never interpolates, so braces in JSON are safe. Strings are
limited to 8192 characters. `\${` writes a literal `${`.

## 7) Arrays

```nac
a = [1, 2, 3]
a[0]  a[0] = 10  a[1] += 5
grid = [[1, 2], [3, 4]]
grid[1][0] = 30                 // nested read and write

len(a)  push(a, 99)  pop(a)  first(a)  last(a)
reverse(a)  slice(a, 1, 3)  join(a, ", ")
range(5)                        // [0, 1, 2, 3, 4]; range(a, b) -> a..b-1
array(5)                        // 5 zeros
```

`removeAt(a, i)` deletes the element at index `i`, shifts the rest down and
returns the removed value (an out-of-range index is an error you can `catch`).
Like `push` and `pop` it changes the variable you pass:

```nac
a = [10, 20, 30]
x = removeAt(a, 1)              // x is 20, a is [10, 30]
```

## 8) Maps

```nac
person = { name: "Ada", age: 36, tags: ["math"], address: { city: "London" } }

person.name                     // same as person["name"]
person.address.city = "Paris"
person.age += 1
"name" in person                // or has(person, "name")
keys(person)  values(person)  len(person)
delete(person, "age")           // true if the key existed, false otherwise
map()                           // empty map
m["name", "year", 3.14] = "Ada", 2026, "pi"  // assign several keys at once
```

Bare identifier keys are strings. Keys must be int, float, string or bool.
Multi-key assignment requires the same number of keys and values. No key
deletion yet.

## 9) if / elif / else

```nac
if x < 0 {
    print("negative")
} elif x == 0 {
    print("zero")
} else {
    print("positive")
}
```

Parentheses around a condition are optional; `else if` works too.

## 10) Loops

```nac
for item in [10, 20, 30] { print(item) }    // arrays
for key in person { print(key) }            // maps iterate keys
for ch in "abc" { print(ch) }               // string characters
for i in range(5) { }                       // 0..4

while x > 0 { x-- }

break
continue
```

## 11) Functions

```nac
fn add(a, b) {
    return a + b
}
print(add(3, 4))

fn noop() { return }            // bare return -> null

add = fn(a, b) { return a + b } // anonymous function as a value
print(add(2, 3))

fn double(x) { return x * 2 }
op = double                     // no parentheses: the function itself
print(op(21))                   // 42
```

- Up to 256 functions (routes count) and 10 parameters; call depth 100.
- No closures: a function sees its own locals/parameters and globals only.
- Named `fn name() { }` declarations cannot be nested.
- Callbacks invoked by the runtime are lenient (missing args become `null`).

## 12) Errors: try / catch / throw

```nac
try {
    data = jsonParse(text)
    print(data.name)
} catch e {                     // `e` is the error message (a string)
    print("bad input:", e)
}

try { risky() } catch { }       // catch e, catch (e), catch { } all work

fn divide(a, b) {
    if b == 0 { throw "division by zero" }
    return a / b
}
```

Runtime errors (invalid JSON, bad index, division by zero, undefined name,
wrong argument count, ...) are catchable. Syntax errors never are. `throw`
outside any `try` stops the program with `Uncaught: ...`.

## 13) HTTP server

```nac
static("public")                       // /app.css -> public/app.css, / -> index.html
static("/assets", "public/assets")     // mount a directory under a URL prefix

route GET "/hello/:name" {
    return html(`<h1>Hello, ${params.name}!</h1>`)
}

route POST "/api/users" {
    data = jsonParse(body)
    return json(data, 201)
}

route * "/fallback" {                  // any method
    return json({ error: "not found" }, 404)
}

serve(8080)                            // blocking; serve() uses PORT, else 8080
```

`route METHOD "pattern" { ... }`: `METHOD` is `GET`, `POST`, `PUT`, `PATCH`,
`DELETE`, ... or `*`. A route body sees:

```nac
method      // "GET"
path        // "/hello/Ada"
query       // ?name=Ada -> query.name (URL-decoded)
headers     // lowercase names: headers["content-type"]
body        // request body as a string
params      // :name -> params.name; trailing "*" -> params["*"]
cookie      // request cookies: cookie.sid
```

`:name` captures one path segment (URL-decoded); a trailing `*` captures the
rest into `params["*"]`. Routes are tried in declaration order (max 128).

`static(dir)` / `static(prefix, dir)` serves GET/HEAD, directory URLs serve
`index.html`, MIME type comes from the extension, and `..` is rejected.
Responses carry `ETag` and `Last-Modified`; `If-None-Match` and
`If-Modified-Since` are answered with `304 Not Modified`, and small files are
cached in memory (invalidated by path, mtime and size).
Resolution order: routes, then static, then `405` or `404`.

**Responses.** Return a string (200; `text/html` if it starts with `<`, else
`text/plain`), a map with `status`, `body`, `headers`, or one of:

```nac
html(body)        html(body, status)
json(value)       json(value, status)       // application/json
redirect(url)     redirect(url, status)     // 302 by default
```

**Request helpers.**

```nac
form(body)                       // urlencoded body -> map
header("X-Demo", "value")        // map with one header
cors(origin)                     // CORS response headers
cors(origin, "POST, OPTIONS", "Content-Type")
setHeader("X-Demo", "value")     // add a header to the current response
setHeader(cors(origin))          // or a whole map
render("page.html", { title: "Hi" })   // {{ title }}, HTML-escaped
```

`render(path, values)` fills `{{ name }}` placeholders of an HTML file and
returns an HTML response map.

## 13a) Cookies, sessions and security

Inside a route, `cookie` maps request cookie names to raw values
(`cookie.sid`). Each `setCookie` adds one `Set-Cookie` header (max 16):

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

`expires` (HTTP date string) and `domain` are also accepted.
`sameSite: "None"` turns on `Secure`. `clearCookie` sends `Max-Age=0` and a past
`Expires`; both functions return `false` on invalid names/values.

Security primitives (built in, no imports):

```nac
randomBytes(n)                    // 2*n lowercase hex chars, n = 1..4095
uuid()                            // random v4 UUID
sessionId()                       // 43-char base64url session id
sha256(s)                         // 64 lowercase hex chars
hmac(key, message)                // HMAC-SHA256, hex
base64Encode(s)  base64Decode(s)
safeEqual(a, b)                   // constant-time comparison
passwordHash(password)            // salted PBKDF2-HMAC-SHA256
passwordVerify(password, hash)
```

**JWT (HS256).**

```nac
token = jwtSign({ sub: "ada", exp: now() + 3600 }, "secret")
claims = jwtVerify(token, "secret")   // claims map, or null
```

`jwtVerify` returns `null` for a bad signature, wrong `alg`, expired `exp` or
not-yet-valid `nbf`. Times are in seconds, no leeway.

**Sessions** are yours to store: keep the ID in a global map and check expiry.

```nac
sessions = {}                     // sid -> { username, expires }

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

Server defaults: 16 KiB headers, 8191-byte body, 10 s request deadline,
120 requests/minute per IP, 64 connections, and `nosniff`,
`X-Frame-Options: DENY`, `Referrer-Policy: no-referrer` on every response.

## 13b) Middleware: before / after

```nac
ADMIN_TOKEN = env("ADMIN_TOKEN")           // read once at startup

before {                                   // every request
    if method == "OPTIONS" { return { status: 204, body: "" } }
}

before "/admin/*" {                        // only matching paths
    if (headers.authorization or "") != "Bearer " + ADMIN_TOKEN {
        return json({ error: "unauthorized" }, 401)
    }
}

after {
    setHeader(cors("*"))
    print(`${method} ${path} -> ${response.status}`)
}
```

A `before` block sees `method`, `path`, `query`, `headers`, `body`, `params`,
`cookie`; returning a response answers immediately. An `after` block also gets
`response` (`status`, `body`, `headers`) and may return a replacement. Blocks
run in declaration order, up to 32. They do not run for requests the server
rejects before the script (400/408/413/429/431) or the built-in `/health`.

## 14) HTTP client

```nac
res = http.get { url: "https://api.example.com/users/1" }
print(res.status, res.ok, res.data.name)

res = http.post {
    url: "https://api.example.com/users",
    body: { name: "Ada" },                  // map/array -> JSON; string -> as-is
    headers: { "Authorization": "Bearer abc" },
    timeout: 10                             // seconds, default 30
}

http.get {
    url: "https://api.example.com/users",
    callback: fn(r) { print(len(r.data), "users") }
}
```

`http.<method> { options }` supports `get`, `post`, `put`, `patch`, `delete`;
the `{ ... }` options map must follow the verb on the same line. The response:

```nac
res.status   // 0 when no response was received
res.ok       // true for 2xx
res.body     // response text (max 8192 chars)
res.data     // body parsed as JSON, else null
res.error    // null, or the failure message
```

HTTP 4xx/5xx and network failures do not throw: check `res.ok` and
`res.error`. Callbacks run before the call returns; the call still blocks.

## 15) JSON

```nac
data = jsonParse(text)            // maps/arrays/scalars; catch errors
text = jsonStringify(value)
```

```nac
try {
    data = jsonParse(body)
} catch e {
    return json({ error: "invalid JSON" }, 400)
}
```

## 16) Dates and times

```nac
now()                                    // seconds since epoch (int)
nowMs()                                  // milliseconds since epoch (float)
formatTime(now())                        // "2024-11-14T16:00:00Z"
formatTime(now(), "%F %T")               // "2024-11-14 16:00:00"
formatTime(now(), "%F %T", "+03:00")     // fixed offset; "local" uses the machine
parseTime("2024-11-14T16:00:00Z")        // seconds, or null
parseTime("14/Nov/2024 04:00 PM", "%d/%b/%Y %I:%M %p")
```

Specifiers: `%Y %y %m %d %e %H %M %S %I %p %j %a %A %b %B %u %w %L %s %z %Z
%F %T %%` (English names; `%L` = milliseconds).

## 17) SQLite

```nac
db = dbOpen("app.db")            // ":memory:" for scratch; null on failure
dbExec(db, "CREATE TABLE IF NOT EXISTS todos (id INTEGER PRIMARY KEY, title TEXT)")
dbExec(db, "INSERT INTO todos (title) VALUES (?)", [title])
dbQuery(db, "SELECT * FROM todos")                  // array of row maps
dbGet(db, "SELECT * FROM todos WHERE id = ?", [id]) // first row map, or null
dbClose(db)

dbQuery(db, "SELECT * FROM todos WHERE done = :done", { done: 0 })
```

`dbExec` returns `{ changes, lastId }`. Always pass values as parameters (`?`
with an array, `:name` with a map) — never paste them into SQL. Transactions
are plain SQL: `dbExec(db, "BEGIN")`, `"COMMIT"` / `"ROLLBACK"`.

## 18) Input / output / files

```nac
print("a", 1, true)          // arguments joined with spaces; print() = empty line
name = input("Name? ")       // one line as a string; null at end of input
n = int(input())

now()  sleep(ms)             // seconds since epoch; milliseconds

read(path)
write(path, text)
append(path, text)
```

## 19) Conversion and builtins

```nac
str(x)  int(x)  float(x)  type(x)  isNull(x)     // Python-style
toString  toInt  toFloat  typeOf                 // camelCase aliases

sqrt  pow  sin  cos  tan  abs  floor  ceil  round  log  exp

len  upper  lower  trim  replace  substr  indexOf  contains  split
first  last  reverse  slice  join  push  pop  removeAt  delete  keys  values  map
range  array  assert  urlEncode  urlDecode
```

## 20) CLI, configuration and testing

```nac
args()                           // ["greet", "Ada"]
env("USER")
env("PORT", "8080")              // default only when unset
exit(1)                          // exit(0)
```

At startup, `.env` in the working directory is loaded automatically; real
environment variables win. CLI flags override `.env`.

`.env` is a **configuration file**, not code — it only holds `KEY=value` lines.
`env("KEY")` is a **builtin function** your code calls; it reads the value at
runtime. So the secret/value is never written into your source, and `nac build`
does not embed `.env` into the executable: the built program reads it from the
working directory (or the real process environment) when it starts.

```dotenv
PORT=8081
ADMIN_TOKEN=change-me
```

```nac
ADMIN_TOKEN = env("ADMIN_TOKEN")   // reference only; value stays outside the code
serve(env("PORT", "8080"))
```

```bash
./nac --host 127.0.0.1 --port 9000 --health app.nac
./nac build app.nac            # one standalone executable
./nac dev app.nac              # restart on script/.env change
./nac test examples/lang/request_test.nac
```

```nac
assert(condition)                // error if false
assert(condition, "message")

response = request("GET", "/hello/Ada")
assert(response.status == 200)
data = jsonParse(response.body)
```

`request(method, path, [body, headers])` calls a declared route in-process
(no port). Header names are normalized to lowercase.

## 21) Limits

| Thing | Limit |
| --- | --- |
| String / dynamic response body | 8192 characters |
| Incoming request body | 8191 bytes |
| Array elements | 10,000 |
| Routes | 128 |
| `static` mounts | 8 |
| Functions (routes count) | 256 |
| Parameters per function | 10 |
| Call depth | 100 |
| Static file size | 32 MB |
| Open SQLite databases | 16 |

`int` is 32-bit; JSON integers outside that range parse as floats. There is no
TLS in NaC itself: put it behind a reverse proxy.
