# NaC Language Interpreter v3.3.0

A lightweight, interpreted scripting language implemented in C with HTTP and JSON/module support. NaC (Not a C) is designed to be simple, expressive, and practical for quick scripting tasks, API testing, and data processing.

---

## Features

* Data types: integers, floats, strings, arrays, maps/dictionaries.
* Operators: arithmetic, comparison, logical operators.
* Control flow: `if-else`, `for`, `while`, `break`, `continue`.
* Functions: user-defined with parameters and `rn` return.
* I/O: `in()` and `out()`.
* HTTP: `http()`, `httpRequest()`, `httpJson()`.
* JSON: `jsonParse()`, `jsonStringify()`.
* Modules: `moduleLoad()`, `moduleRequire()`, namespace registry APIs.
* Error reporting with line and column.
* Cross-platform: Windows, Linux, macOS.

---

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
./nac program.nac

# Windows
nac.exe program.nac
```

---

## HTTP + JSON Example

```nac
raw = httpRequest("GET", "https://api.ipify.org/?format=json");
data = jsonParse(raw);
out(data["ip"]);
```

## Module Example

`moduleRequire(name)` loads `modules/<name>.json` and caches it in namespace registry.

```nac
cfg = moduleRequire("config");
out(cfg["api_base"]);

ok = moduleRegister("runtimeCfg", cfg);
out(moduleNames());
```

---

## Built-in Functions

### JSON
- `jsonParse(json)`
- `jsonStringify(value)`

### HTTP
- `http(method, url, body?)`
- `httpRequest(method, url, body?)`
- `httpJson(method, url, body?)`

### Modules
- `moduleLoad(path)`
- `moduleRegister(name, module)`
- `moduleGet(name)`
- `moduleRequire(name)`
- `moduleNames()`

### Existing Core Functions
- Math: `sqrt`, `pow`, `sin`, `cos`, `tan`, `abs`, `floor`, `ceil`, `round`, `log`, `exp`
- String: `length`, `upper`, `lower`, `trim`, `replace`, `substr`, `indexOf`
- Array: `push`, `pop`, `first`, `last`, `reverse`, `slice`, `join`
- File: `read`, `write`, `append`
- Map: `map`

---

## Limitations

* Maximum functions: 100
* Maximum function parameters: 10
* Maximum call stack depth: 100
* Maximum array size: 10,000 elements
* Strings limited to 1024 characters

