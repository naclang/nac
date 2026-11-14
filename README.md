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
| Serve a folder of static files       | `static("public")` — MIME types, `index.html`, raw bytes up to 32 MB |
| Return JSON / HTML / a redirect      | `json(value, status)`, `html(text, status)`, `redirect(url)`   |
| Read a JSON request body             | `jsonParse(body)`                                              |
| Read and set cookies                 | `cookie.name` in a route, `setCookie(name, value, { ... })`, `clearCookie(name)` |
| Add login and sessions               | `passwordHash` / `passwordVerify`, `sessionId()`, `randomBytes(n)`, `uuid()` |
| Hash or sign data                    | `sha256(s)`, `hmac(key, message)`, constant-time `safeEqual`, `base64Encode/Decode` |
| Get safer defaults                   | header/body limits, request deadline, per-IP rate limit and security headers |
| Turn bad input into a 400            | `try { ... } catch e { return json({ error: e }, 400) }`       |
| Call another API from a route        | `res = http.get { url: "..." }` → `res.status`, `res.ok`, `res.data` (parsed JSON) |
| React when the upstream answers      | `http.get { url: "...", callback: fn(r) { ... } }`             |
| Keep shared state (counters, caches) | plain global variables — no locks, no imports                  |
| Ship it                              | `./nac build app.nac` → one native executable                  |

Scripts are plain files: no framework to choose, no config file, and no
`main()`. The whole language fits in [docs/SYNTAX.md](docs/SYNTAX.md).

Shared state is safe by construction: handler calls are serialized behind one
lock, so two requests can never interleave inside your script. That is a
deliberate simplicity-over-throughput trade-off (see *Honest limits* below).

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
`hmac`, `base64Encode`/`base64Decode`, constant-time `safeEqual` and
PBKDF2-HMAC-SHA256 `passwordHash`/`passwordVerify`. See
[`examples/web/06_accounts.nac`](examples/web/06_accounts.nac) for a complete
register/login/logout/role-check example, and
[section 13a of the syntax reference](docs/SYNTAX.md#13a-cookies-sessions-and-security).

The server itself is hardened by default: a 16 KiB header limit, an 8191-byte
body limit, a 10-second request deadline, 120 requests per minute per IP, and
`X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY` and
`Referrer-Policy: no-referrer` on every response. TLS is still the reverse
proxy's job.

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
| `examples/web/site/site.nac`          | a complete site: static files + routes + redirect             |
| `examples/lang/`                      | language tour: `syntax.nac`, `try.nac`, algorithms, a CLI tool |

```bash
./nac examples/web/02_json_api.nac
curl -X POST -d '{"title":"buy milk"}' http://localhost:8080/todos
```

## Installation

### Windows (MinGW)

```bash
./build.bat
```

### Unix

NaC needs libcurl (HTTP client) and OpenSSL's libcrypto (hashing,
`randomBytes`, PBKDF2). Windows uses CNG/BCrypt instead and needs no extra
packages.

```bash
# Ubuntu/Debian
sudo apt install libcurl4-openssl-dev libssl-dev pkg-config

# Fedora
sudo dnf install libcurl-devel openssl-devel pkg-config

# Arch
sudo pacman -Syu curl-compat openssl pkg-config

# macOS (Homebrew's OpenSSL is keg-only, so point pkg-config at it)
brew install curl pkg-config openssl@3
export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig:$PKG_CONFIG_PATH"

# Compile
chmod +x build.sh
./build.sh
```

---

## Usage

```bash
# Linux/macOS
./nac program.nac [arg1 arg2 ...]

# Windows
nac.exe program.nac [arg1 arg2 ...]
```

Anything after the script filename is passed through to the script and is
readable with `args()` — so the same language also writes small CLI tools.

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

There's no formal test runner yet — run the example scripts and check
the output:

```bash
for f in examples/lang/*.nac; do echo "=== $f ==="; ./nac "$f" < /dev/null; done
```

The `examples/web/` scripts start servers and block forever by design: run one,
then try it with `curl` (each file lists the commands at the top).
`05_http_client.nac` and `04_api_gateway.nac` need internet access.
`06_accounts.nac` creates `accounts_users.json` in the working directory;
delete that file to start again with no users.

---

## Honest limits

NaC is young. These are the things to know before you pick it for something real:

- **Throughput.** Handler calls are serialized behind one lock, each connection is
  one request (`Connection: close`, no keep-alive), and connections are handled by
  one thread each, capped at 64 in flight. A handler that blocks for a long time
  (`sleep()`, a slow `http.get`) makes other requests wait. NaC has not been
  benchmarked against other servers yet; making it fast is on the
  [Roadmap](Roadmap.md), and no performance claims are made until that exists.
- **No TLS.** Put it behind a reverse proxy (nginx, Caddy, a cloud load balancer)
  for HTTPS (set `secure: true` on cookies once you do).
- **Sizes.** Strings, request bodies and response bodies from `route`s are limited
  to 8192 characters (incoming bodies stop at 8191 bytes with `413`; static files
  are not limited: up to 32 MB each). Request headers are capped at 16 KiB and a
  request must finish within 10 seconds. Arrays hold at most 10,000 elements. Up
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
  `X-Forwarded-For`, so behind a proxy every request counts as the proxy.
- **Password hashing is CPU work.** A login blocks other requests while PBKDF2
  runs, because handlers are serialized. Keep that in mind for brute-force-heavy
  endpoints.
- **Callbacks are synchronous.** `callback:` runs when the response arrives, and
  the request itself still blocks. There are no closures: a function sees its own
  variables and globals only.
- **Windows HTTP client** is implemented but has had less testing than the Unix one.
