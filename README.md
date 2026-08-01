# NaC Language Interpreter v1.0.0

A lightweight, interpreted scripting language implemented in C with HTTP, JSON, a built-in web server, and CLI-tool support. NaC (Not a C) is designed to be simple, expressive, and practical for quick scripting, small JSON API backends, and command-line tools.

## Installation

### Windows (MinGW)

```bash
./build.bat
```

### Unix

```bash
# Ubuntu/Debian
sudo apt install libcurl4-openssl-dev

# Fedora
sudo dnf install libcurl-devel

# Arch
sudo pacman -Syu curl-compat

# macOS
brew install curl

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

Anything after the script filename is passed through to the script and is readable with `args()`.

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
for f in examples/*.nac; do echo "=== $f ==="; ./nac "$f"; done
```

(`examples/http.nac` needs network access to an allowlisted host, and
`examples/webserver.nac` blocks forever by design — both are fine to
skip.)

---

## Limitations

- Maximum functions: 100
- Maximum function parameters: 10
- Maximum call stack depth: 100 (as of v1.0.0 this is a real, safely-reachable limit on both Unix and Windows builds — see `CHANGELOG.md` for a fix that closed a silent-crash issue on deep recursion)
- Maximum array size: 10,000 elements
- Strings limited to 8192 characters (this bounds HTTP request/response bodies too)
- `serve()` handles connections concurrently (one thread per connection, capped at 64 in flight), but each handler _call_ is still serialized behind an internal lock since the interpreter's variables/call-stack are shared state — fine for typical route logic, but a handler that blocks for a long time (e.g. `sleep()`) will make other requests wait too