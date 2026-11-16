# NaC — a language for writing web servers

NaC (Not a C) is a small scripting language whose job is **serving**: declare
`route`s and `static()` folders, call `serve(port)`, and you are hosting a site
or a JSON API. HTTP, JSON, an HTTP client, files and error handling are part of
the language, not libraries you install. The interpreter is written in C and is
a single ~140 KB binary.

```nac
users = ["Ada", "Turing", "Grace"]

static("public")                              // host a folder of HTML/CSS/JS

route GET "/api/users/:id" {
    id = toInt(params.id)
    if id < 0 or id >= len(users) {
        return json({ error: "not found" }, 404)
    }
    return json({ id: id, name: users[id] })
}

route POST "/api/users" {
    try {
        data = jsonParse(body)
    } catch e {
        return json({ error: "invalid JSON" }, 400)
    }
    push(users, data.name)
    return json(users, 201)
}

serve(8080)
```

## What NaC makes easy

| You want to...                       | In NaC                                                         |
| ------------------------------------ | -------------------------------------------------------------- |
| Add an endpoint                      | `route GET "/users/:id" { ... }` — path params arrive in `params`, `?query=` in `query`, plus `headers` and `body` |
| Run code before/after every request  | `before { ... }` / `after { ... }` blocks for auth, logging and CORS (see [Middleware](#middleware-before-and-after)) |
| Serve a folder of static files       | `static("public")` — MIME types, `index.html`, raw bytes up to 32 MB |
| Return JSON / HTML / a redirect      | `json(value, status)`, `html(text, status)`, `redirect(url)`   |
| Render an HTML template               | `render("page.html", { title: ... })` — `{{ title }}` is HTML-escaped |
| Read a JSON request body             | `jsonParse(body)`                                              |
| Read and set cookies                 | `cookie.name` in a route, `setCookie(name, value, { ... })`, `clearCookie(name)` |
| Add login and sessions               | `passwordHash` / `passwordVerify`, `sessionId()`, `randomBytes(n)`, `uuid()` |
| Hash or sign data                    | `sha256(s)`, `hmac(key, message)`, constant-time `safeEqual`, `base64Encode/Decode` |
| Issue and check API tokens           | `jwtSign({ sub: id, exp: now() + 3600 }, secret)`, `jwtVerify(token, secret)` → claims, or `null` |
| Format and parse dates               | `formatTime(ts, "%F %T")`, `parseTime("2024-11-14T16:00:00Z")`, `nowMs()` |
| Get safer defaults                   | header/body limits, request deadline, per-IP rate limit and security headers |
| Configure the server per deployment  | `nac --host 127.0.0.1 --port 9000 --health app.nac` (see [Running a server](#running-a-server)) |
| Turn bad input into a 400            | `try { ... } catch e { return json({ error: e }, 400) }`       |
| Call another API from a route        | `res = http.get { url: "..." }` → `res.status`, `res.ok`, `res.data` (parsed JSON) |
| React when the upstream answers      | `http.get { url: "...", callback: fn(r) { ... } }`             |
| Store data in a database             | `db = dbOpen("app.db")`, `dbQuery(db, "SELECT ... WHERE id = ?", [id])` → rows as maps (see [SQLite](#sqlite)) |
| Keep shared state (counters, caches) | plain global variables — no locks, no imports                  |
| Ship it                              | `./nac build app.nac` → one native executable                  |

Scripts are plain files: no framework to choose, no required config file, and
no `main()`. An optional `.env` file is loaded automatically. The whole
language fits in [docs/SYNTAX.md](docs/SYNTAX.md).

Shared state is safe by construction: handler calls are serialized behind one
lock, so two requests can never interleave inside your script. That is a
deliberate simplicity-over-throughput trade-off (see *Honest limits* below).

## Middleware: before and after

`before` and `after` blocks run code around every request, so authentication,
logging and CORS are written once instead of in every route:

```nac
currentUser = null

before {                                   // every request, 404s included
    if method == "OPTIONS" { return { status: 204, body: "" } }   // CORS preflight
}

before "/admin/*" {                        // only paths matching the pattern
    if (headers.authorization or "") != "Bearer " + env("ADMIN_TOKEN") {
        return json({ error: "unauthorized" }, 401)
    }
    currentUser = "admin"                  // still set when the route runs
}

after {
    setHeader(cors("*"))                   // CORS headers on every response
    print(`${method} ${path} -> ${response.status}`)
}
```

- **Arguments.** A `before` block sees the same variables as a `route`: `method`,
  `path`, `query`, `headers`, `body`, `params` (empty when no route matched) and
  `cookie`. An `after` block also gets `response`, a map of `status`, `body` and
  `headers`.
- **Answering early.** If a `before` block `return`s a response (a map, `json(...)`,
  a string...), that is the answer: the route and the remaining `before` blocks are
  skipped. Ending without `return` continues to the next block and then the route.
- **Changing the response.** `setHeader("Name", "value")` (or `setHeader(map)`,
  e.g. `setHeader(cors("*"))`) adds a header to the response, replacing one with the
  same name. It works in `before`, routes and `after`, and on static files too. An
  `after` block that `return`s a response replaces the current one (for example
  `response.status = 500; return response`). Headers from `setHeader` are
  kept even then.
- **Scope and order.** `before "/admin/*" { ... }` takes the same patterns as `route`
  (`:name` segments, a trailing `*`). Blocks run in the order they are written, for
  `before` and for `after`. `before` and `after` are not reserved words: only
  `before {` and `before "pattern" {` start a block.
- **Global state.** All of one request's `before` blocks, its route and its `after`
  blocks run under one lock, so a global set in `before` (like `currentUser`) is
  still there in the route and in `after`, and no other request can change it in
  between. A static file is read without that lock; `response.body` is `""` for it.
- **What they do not cover.** Requests the server refuses before your script runs
  (`400`, `408`, `413`, `431`, the rate limiter's `429`) and the built-in `/health`
  do not pass through `before`/`after`. `nac test` and `request()` do run them.
- A script can have up to 32 blocks. See
  [`examples/web/11_middleware.nac`](examples/web/11_middleware.nac) and
  [`examples/lang/middleware_test.nac`](examples/lang/middleware_test.nac).

## SQLite

SQLite is built in: a database is one file, there is no server to run, and the
whole thing ships next to your single binary.

```nac
db = dbOpen("app.db")                      // creates the file if needed; ":memory:" for a scratch one
dbExec(db, "CREATE TABLE IF NOT EXISTS todos (id INTEGER PRIMARY KEY, title TEXT NOT NULL, done INTEGER NOT NULL DEFAULT 0)")

route POST "/todos" {
    data = jsonParse(body)
    r = dbExec(db, "INSERT INTO todos (title) VALUES (?)", [data.title])
    return json(dbGet(db, "SELECT * FROM todos WHERE id = ?", [r.lastId]), 201)
}

route GET "/todos" {
    return json(dbQuery(db, "SELECT * FROM todos WHERE done = :done", { done: 0 }))
}
```

| Function                         | Returns                                                        |
| -------------------------------- | -------------------------------------------------------------- |
| `dbOpen(path)`                   | a database handle (an int), or `null` if the file cannot be opened |
| `dbExec(db, sql [, params])`     | `{ changes, lastId }`: rows inserted/updated/deleted and the last inserted row id |
| `dbQuery(db, sql [, params])`    | an array with one map per row, keyed by column name (`[]` if none) |
| `dbGet(db, sql [, params])`      | the first row as a map, or `null` when nothing matches          |
| `dbClose(db)`                    | `0`; databases are also closed when the program ends           |

- **Always pass values as parameters.** `?` placeholders take an array, `:name`
  placeholders take a map. Values are bound by SQLite, never pasted into the SQL
  text, so there is no SQL injection. Text, numbers, booleans (stored as 0/1) and
  `null` can be bound.
- **Types.** `INTEGER` becomes an int (a float when it does not fit 32 bits),
  `REAL` a float, `TEXT` a string, `NULL` null. Rows are plain maps, so they go
  straight into `json(...)`.
- **Errors can be caught.** A failed statement (constraint, syntax error, locked
  database) reports an error you handle with `try { ... } catch e { ... }`.
- **Several statements.** `dbExec` without parameters runs a whole script
  (`"CREATE TABLE a(x); CREATE TABLE b(y);"`). With parameters, and for
  `dbQuery`/`dbGet`, the SQL must be a single statement.
- **Transactions are plain SQL:** `dbExec(db, "BEGIN")`, then `"COMMIT"` or
  `"ROLLBACK"`. Handler calls are already serialized, so a transaction opened and
  closed inside one route cannot be interleaved with another request.
- **Defaults.** Foreign keys are enforced (`PRAGMA foreign_keys = ON`) and a
  locked database is retried for 5 seconds. Other pragmas, such as
  `dbExec(db, "PRAGMA journal_mode = WAL")`, are up to you.
- **Limits.** NaC strings hold 8191 bytes and arrays 10,000 elements, so a `TEXT`
  value longer than that, a `BLOB` column, or a result with more than 10,000 rows is
  reported as an error instead of being cut off silently: select `substr(col, 1, 8000)`,
  `hex(col)` or add `LIMIT`/`OFFSET`. Up to 16 databases can be open at once.
- **Deploying.** The database is a file in the working directory (or wherever the
  path points). In Docker, mount a volume for it; back it up with the `sqlite3`
  CLI (`.backup`) rather than by copying a file that is being written.

A build without SQLite support (see [Installation](#installation)) still has these
functions; they report an error explaining that. See
[`examples/web/12_sqlite_todos.nac`](examples/web/12_sqlite_todos.nac) for a
persistent REST API and [`examples/lang/sqlite_test.nac`](examples/lang/sqlite_test.nac)
for every behavior above as assertions.

## Security and sessions

Cookies, a CSPRNG and password hashing are part of the language:

```nac
route POST "/login" {
    data = jsonParse(body)
    user = findUser(data.username)       // your own lookup
    if isNull(user) or not passwordVerify(data.password, user.password) {
        return json({ error: "invalid credentials" }, 401)
    }
    sid = sessionId()                    // 32 random bytes, base64url
    sessions[sid] = { username: user.username, expires: now() + 86400 }
    setCookie("sid", sid, { httpOnly: true, sameSite: "Lax", maxAge: 86400 })
    return json({ ok: true })
}
```

`cookie.name` reads request cookies; `setCookie`/`clearCookie` emit one
`Set-Cookie` header each. Also included: `randomBytes`, `uuid`, `sha256`,
`hmac`, `base64Encode`/`base64Decode`, constant-time `safeEqual`,
PBKDF2-HMAC-SHA256 `passwordHash`/`passwordVerify` and HS256 JSON Web Tokens
(see below). See
[`examples/web/06_accounts.nac`](examples/web/06_accounts.nac) for a complete
register/login/logout/role-check example, and
[section 13a of the syntax reference](docs/SYNTAX.md#13a-cookies-sessions-and-security).

The server itself is hardened by default: a 16 KiB header limit, an 8191-byte
body limit, a 10-second request deadline, 120 requests per minute per IP, and
`X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY` and
`Referrer-Policy: no-referrer` on every response. Most of those limits can be
changed per deployment, see [Running a server](#running-a-server). TLS is the
reverse proxy's job: [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) has Caddy and nginx
setups.

### JSON Web Tokens

```nac
SECRET = env("JWT_SECRET")

route POST "/login" {
    // ... check the password, then:
    return json({ token: jwtSign({ sub: user.username, exp: now() + 3600 }, SECRET) })
}

route GET "/me" {
    token = replace(headers["authorization"], "Bearer ", "")
    claims = jwtVerify(token, SECRET)
    if isNull(claims) {
        return json({ error: "unauthorized" }, 401)
    }
    return json({ user: claims.sub })
}
```

`jwtSign(payload, secret)` signs a map of claims with HS256. `jwtVerify(token,
secret)` returns the claims map, or `null` when the token is malformed, signed
with another key or algorithm (`alg` must be exactly `HS256`), expired (`exp`) or
not yet valid (`nbf`). Times are in seconds with no leeway. An empty secret is an
error. Tokens are signed, not encrypted: do not put secrets in the claims. A
complete login example is [`examples/web/10_jwt.nac`](examples/web/10_jwt.nac).

### Dates and times

```nac
formatTime(now())                              // "2024-11-14T16:00:00Z"
formatTime(now(), "%A, %d %B %Y %H:%M")        // "Thursday, 14 November 2024 16:00"
formatTime(now(), "%F %T", "+03:00")           // fixed offset; "local" uses the machine's zone
formatTime(nowMs() / 1000, "%T.%L")            // "16:00:00.123"
parseTime("2024-11-14T16:00:00+03:00")         // 1731589200, or null if it is not a time
parseTime("14/Nov/2024 04:00 PM", "%d/%b/%Y %I:%M %p")
```

`now()` stays whole seconds. `nowMs()` returns milliseconds since 1970 as a float,
because that number does not fit NaC's 32-bit `int`; use `nowMs() - start` for
durations. Specifiers: `%Y %y %m %d %e %H %M %S %I %p %j %a %A %b %B %u %w %L %s
%z %Z %F %T %%` (English names; `%L` is milliseconds). `parseTime` reads ISO
8601 by default, treats a time without a zone as UTC, and accepts the same
specifiers except `%j %u %w`. Years 0001 to 9999.

## Examples

Start with the web examples (run from the project root):

| File                                  | Shows                                                         |
| ------------------------------------- | ------------------------------------------------------------- |
| `examples/web/01_hello.nac`           | routes, `:id` params, `?query`, JSON and 404 responses        |
| `examples/web/02_json_api.nac`        | a REST API: POST/GET/PATCH/DELETE, validation, `try/catch` → 400 |
| `examples/web/03_guestbook.nac`       | an HTML form, form parsing, HTML escaping, data kept in a file |
| `examples/web/04_api_gateway.nac`     | a route that calls another API, with a callback and a 502 fallback |
| `examples/web/05_http_client.nac`     | the HTTP client on its own: GET/POST/PUT/DELETE, headers, callbacks |
| `examples/web/06_accounts.nac`        | register/login/logout, `HttpOnly` sessions, protected routes and role checks |
| `examples/web/07_request_helpers.nac` | URL-encoded forms, response headers, CORS and preflight |
| `examples/web/08_templates.nac`       | HTML templates with escaped placeholders                 |
| `examples/web/09_config.nac`          | `.env`, `env()` defaults and `serve()` configuration      |
| `examples/web/10_jwt.nac`             | JWT login, a bearer-token route and the built-in `/health` |
| `examples/web/11_middleware.nac`      | `before`/`after` blocks: an auth check, CORS preflight and request logging |
| `examples/web/12_sqlite_todos.nac`    | the todo API from `02_json_api.nac`, kept in a SQLite file |
| `examples/web/site/site.nac`          | a complete site: static files + routes + redirect             |
| `examples/lang/`                      | language tour: `syntax.nac`, `try.nac`, algorithms, a CLI tool |

```bash
./nac examples/web/02_json_api.nac
curl -X POST -d '{"title":"buy milk"}' http://localhost:8080/todos
```

For automatic restarts when the script or `.env` changes, run
`nac dev examples/web/08_templates.nac`.
Run language and in-process route checks with `nac test examples/lang/request_test.nac`.
Check configuration defaults with `nac test examples/lang/config_test.nac`.
Check JWT and date/time functions with `nac test examples/lang/jwt_time_test.nac`.
Check `before`/`after` blocks with `nac test examples/lang/middleware_test.nac`.
Check SQLite with `nac test examples/lang/sqlite_test.nac` (skipped on a build without SQLite).

At startup, NaC loads `.env` from the working directory if present. Existing
process environment variables take precedence. `env("PORT", "8080")` returns
the default only when `PORT` is unset; `serve()` uses `PORT` or falls back to
8080. An explicit `serve(port)` overrides the configured value.

```dotenv
PORT=8081
```

## Installation

### Windows (MinGW)

```bash
./build.bat
```

SQLite is optional on Windows because there is no system package: download the
"amalgamation" from [sqlite.org/download](https://www.sqlite.org/download.html)
and copy `sqlite3.c` and `sqlite3.h` into `third_party/sqlite/` before building.
`build.bat` then compiles SQLite into `nac.exe` and tells you so. Without those
files NaC builds fine, and the `db...` functions report that there is no SQLite.

### Unix

NaC needs libcurl (HTTP client) and OpenSSL's libcrypto (hashing,
`randomBytes`, PBKDF2), and uses SQLite when its development files are installed
(`build.sh` prints whether SQLite support is enabled; it is optional). Windows
uses CNG/BCrypt instead and needs no extra packages for the first two.

```bash
# Ubuntu/Debian
sudo apt install libcurl4-openssl-dev libssl-dev libsqlite3-dev pkg-config

# Fedora
sudo dnf install libcurl-devel openssl-devel sqlite-devel pkg-config

# Arch
sudo pacman -Syu curl-compat openssl sqlite pkg-config

# macOS (Homebrew's OpenSSL is keg-only, so point pkg-config at it)
brew install curl pkg-config openssl@3 sqlite
export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig:$PKG_CONFIG_PATH"

# Compile
chmod +x build.sh
./build.sh
```

---

## Usage

```bash
# Linux/macOS
./nac [flags] program.nac [arg1 arg2 ...]

# Windows
nac.exe [flags] program.nac [arg1 arg2 ...]
```

Anything after the script filename is passed through to the script and is
readable with `args()` — so the same language also writes small CLI tools.
NaC's own flags (`--port`, `--host`, ...) can go anywhere and are not passed to
the script; put `--` before anything that must reach the script unchanged.

### Running a server

| Flag                   | Environment variable        | Default   | Meaning |
| ---------------------- | --------------------------- | --------- | ------- |
| `--port N`             | `PORT`                      | `8080`    | Listen port; overrides `serve(port)` |
| `--host ADDR`          | `NAC_HOST`                  | `0.0.0.0` | Listen address; `127.0.0.1` for loopback only |
| `--max-connections N`  | `NAC_MAX_CONNECTIONS`       | `64`      | Connections open at once |
| `--workers N`          | `NAC_WORKERS`               | `8`       | Worker threads running requests (Linux/macOS) |
| `--read-timeout-ms N`  | `NAC_READ_TIMEOUT_MS`       | `10000`   | Deadline for a whole request |
| `--max-header-bytes N` | `NAC_MAX_HEADER_BYTES`      | `16384`   | Header limit, at most 32768 |
| `--rate-limit N`       | `NAC_RATE_LIMIT_MAX`        | `120`     | Requests per IP per window; `0` = off |
| `--rate-window-sec N`  | `NAC_RATE_LIMIT_WINDOW_SEC` | `60`      | Rate-limit window |
| `--health`             | `NAC_HEALTH`                | off       | Built-in `GET /health` → `{"status":"ok"}` |
| `--shutdown-timeout-ms N` | `NAC_SHUTDOWN_TIMEOUT_MS` | `10000`   | How long a graceful shutdown waits for requests in flight; `0` = don't wait |

```bash
./nac --host 127.0.0.1 --port 9000 --health app.nac
```

A flag beats the environment (and `.env`), which beats the default. `serve(port,
host)` also accepts a host. `/health` yields to a `route GET "/health"` of your
own and is exempt from the rate limit. `nac dev` and `nac test` take the same
flags, and so does a program built with `nac build`. The order does not matter:
`nac app.nac --port 9000` equals `nac --port 9000 app.nac`. NaC's flags are
removed from `args()`; after `--` nothing is touched
(`nac app.nac -- --port x` gives the script `["--port", "x"]`).
**Graceful shutdown.** On `SIGINT` / `SIGTERM` (Ctrl+C, `kill`, `systemctl stop`,
`docker stop`; Ctrl+C, Ctrl+Break or closing the console on Windows) the server
stops accepting connections and lets the requests already running finish, for up
to `--shutdown-timeout-ms`. Then `serve()` returns and the script ends normally
(exit status 0). If requests are still running when the timeout ends, NaC exits
with status 1 and cuts them off. A second signal exits immediately (status 130).
The request body limit stays 8191 bytes. Details, proxy setups and an explanation
of rate limiting behind a proxy: [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md).

### Packaging a script as a standalone executable

```bash
./nac build program.nac                # writes ./program (or program.exe on Windows)
./nac build program.nac -o my_tool     # custom output name
```

This bundles your script's source together with the interpreter runtime
into a single native executable — the result runs on its own, with no
`.nac` file or `nac` install needed:

```bash
./program arg1 arg2
```

This isn't a from-scratch compiler (NaC has no bytecode/codegen target);
it works by embedding your script as a string and generating a small
stub that runs it, compiled alongside the same interpreter sources
`build.sh`/`build.bat` already build. Because of that, `nac build` needs:
- a C compiler (`gcc`) available on the machine running it, same as
  building `nac` itself does, and
- nac's own `src/` directory present next to the `nac`/`nac.exe`
  executable (the layout `build.sh`/`build.bat` leave behind) — so run it
  from within the project you built `nac` in.

---

## Testing

Use `assert()` for failing checks and `request(method, path, [body, headers])`
to exercise registered routes without opening a network port:

```bash
nac test examples/lang/request_test.nac
for f in examples/lang/*.nac; do echo "=== $f ==="; ./nac "$f" < /dev/null; done
```

The `examples/web/` scripts start servers and block forever by design: run one,
then try it with `curl` (each file lists the commands at the top).
`05_http_client.nac` and `04_api_gateway.nac` need internet access.
`06_accounts.nac` creates `accounts_users.json` in the working directory;
delete that file to start again with no users.

For throughput measurements, see [bench/README.md](bench/README.md): the runner
needs only Node.js (`node bench/run.js --url ...`), and the suite includes NaC
apps plus Node.js, Python and Go reference servers for the same endpoints.

---

## Honest limits

NaC is young. These are the things to know before you pick it for something real:

- **Throughput.** Handler calls are serialized behind one lock. On Linux and
  macOS requests run on a fixed pool of worker threads (`--workers`, default 8)
  and idle keep-alive connections are watched by a single `poll()` thread, so
  they cost no thread; at most 64 connections are open at once. A slow client
  can hold a worker for up to `--read-timeout-ms` while its request arrives.
  On Windows each connection still has its own thread. HTTP/1.1 connections
  are kept alive (idle ones close after a few seconds, and each connection
  serves at most 1000 requests). A handler that blocks for a long time
  (`sleep()`, a slow `http.get`) makes other requests wait. A reproducible
  benchmark suite lives in [bench/README.md](bench/README.md) (hello, JSON,
  static files, outgoing HTTP, with Node.js/Python/Go reference servers);
  published numbers come only from recorded runs, and making NaC faster is on
  the [Roadmap](Roadmap.md).
- **No TLS.** Put it behind a reverse proxy (nginx, Caddy, a cloud load balancer)
  for HTTPS (set `secure: true` on cookies once you do). Ready-made Caddy and nginx
  configurations: [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md).
- **Floats and big numbers.** `int` is 32-bit. JSON integers outside that range
  parse as floats. Whole floats print in full up to 1e15; other floats print with
  six significant digits.
- **Sizes.** Strings, request bodies and response bodies from `route`s are limited
  to 8192 characters (incoming bodies stop at 8191 bytes with `413`; static files
  are not limited: up to 32 MB each). Request headers are capped at 16 KiB and a
  request must finish within 10 seconds (both adjustable, see above). Arrays hold at most 10,000 elements. Up
  to 128 routes, 8 `static` mounts, 256 functions, 10 parameters per function,
  call depth 100.
- **Sessions are yours to store.** `sessionId()` only generates an ID; the
  `sessions` map in `06_accounts.nac` lives in memory and is lost on restart.
  Its users are kept in a plain JSON file, and passwords use PBKDF2-HMAC-SHA256
  with 210,000 iterations (roughly tens of milliseconds per login). For
  production, put users and sessions in a database and run a single process or
  share the session store.
- **Rate limiting is per process.** 120 requests per 60-second fixed window per
  client IP, tracked for up to 1024 IPs. It uses the socket address, not
  `X-Forwarded-For`, so behind a proxy every request counts as the proxy and the
  limit is shared by all visitors: raise it or switch it off (`--rate-limit 0`) and
  limit in the proxy.
- **Password hashing is CPU work.** A login blocks other requests while PBKDF2
  runs, because handlers are serialized. Keep that in mind for brute-force-heavy
  endpoints.
- **Callbacks are synchronous.** `callback:` runs when the response arrives, and
  the request itself still blocks. There are no closures: a function sees its own
  variables and globals only.
- **Windows HTTP client** is implemented but has had less testing than the Unix one.
