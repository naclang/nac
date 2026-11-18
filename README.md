# NaC

NaC is a small scripting language for web servers, implemented in C. `route`, `static` and `serve` are part of the language; JSON, cookies, session primitives, an HTTP client, SHA-256 / HMAC / PBKDF2 / JWT and optional SQLite are built in. `.nac` files are interpreted directly, with no separate compile step. `nac build` packages a script together with the runtime into a single executable.

## Purpose

Publish a small backend without assembling a framework: routes, static files, JSON and data storage in one file. Size, time and rate limits plus security headers are enabled by default. TLS is a reverse proxy's job; NaC speaks plain HTTP.

## Installation

On Unix you need GCC, libcurl and OpenSSL headers (pkg-config is used when available):

```bash
git clone https://github.com/naclang/nac.git
cd nac
./build.sh
./nac examples/web/01_hello.nac
```

On Windows, run `build.bat` with MSVC Build Tools or MinGW GCC; the output is `nac.exe`. See the [installation guide](https://naclang.github.io/docs/install.html) for details.

To produce a single file, run `nac build app.nac` (or `-o name`): a C compiler and the `src/` directory next to the `nac` executable are required.

## Test

```bash
# language tour
for f in examples/basics/*.nac; do echo "=== $f ==="; ./nac "$f" < /dev/null; done

# todo app in-process self test
TODO_SELFTEST=1 TODO_DB=:memory: ./nac test examples/apps/todo/todo.nac
```

On Windows, define both environment variables in the shell. The `examples/web/*.nac` files start servers; the `curl` commands to try are at the top of each file.

## Limits

- Strings and dynamic response bodies grow with available memory; arrays are capped at 50,000,000 elements and static files at 32 MB.
- Request body: 1 MiB by default, up to 64 MiB with `--max-body-bytes`; header block 16 KiB; request read deadline 10 s; 120 requests per 60 s per IP; 64 concurrent connections.
- At most 128 routes, 8 static mounts, 32 middleware blocks, 256 function slots; 10 parameters per function and a call depth of 100.
- Handlers run one at a time, so a slow route delays the others. Windows has no worker pool and uses a thread per connection.
- No closures; `int` is 32-bit; session storage belongs to the application.
- No built-in TLS; HTTPS requires a reverse proxy. `nac build` is not a bytecode compiler — it bundles the script with the runtime.
