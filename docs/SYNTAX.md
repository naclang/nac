# NaC Language — Full Syntax Reference

This document lists everything the parser actually supports today — not an
aspirational spec. Treat it as ground truth for what NaC currently does, so
you can decide what to simplify or extend next.

---

## 1) Comments

```nac
// a single-line comment (runs to end of line)
```

There is no block comment (`/* ... */`).

---

## 2) Data types

| Type   | Example                            |
| ------ | ---------------------------------- |
| int    | `42`, `-7`                         |
| float  | `3.14`, `-0.5`                     |
| bool   | `true`, `false`                    |
| null   | `null`                             |
| string | `"hello"` (escapes: `\n \t \\ \"`) |
| array  | `[1, 2, 3]` or `array(SIZE)`       |
| map    | `map()`                            |

Notes:

- Strings are limited to **8192 characters** (`MAX_STRING_LEN`).
- Arrays are limited to **10,000 elements** (`MAX_ARRAY_SIZE`).
- `bool` is a real, distinct type (added in v3.5.0) — `typeOf()` reports
  `"bool"`, `out()` prints `true`/`false`, and JSON round-trips it as a JSON
  boolean rather than `0`/`1`.
- `null` is a real, distinct type (added in v1.0.0) — `typeOf()` reports
  `"null"`, `isNull(x)` checks for it, and JSON's `null` round-trips
  correctly (previously it silently became the int `0`). `==`/`!=` are
  null-aware: `null == 0`, `null == ""`, and `null == false` are all
  `false` — `null` only equals `null`.

---

Referencing an undefined variable, or indexing an array out of bounds,
reports an error to stderr and evaluates to `null` (not `0` — this
changed in v1.0.0 specifically so a bug like a typo'd variable name
doesn't get silently masked by a plausible-looking `0`). Execution
continues after the error, so check your script for a stray error
message if something looks off.

## 3) Variables

```nac
x = 5;              // declaration = assignment (no separate var/let keyword)
x = x + 1;
x++;                // only for a plain variable (arr[i]++ is NOT supported)
x--;
```

Variables are dynamically typed — the same name can be reassigned to any
type at any time.

There's no explicit `local`/`global` keyword. Instead:

- A **function parameter** always binds as a new local variable, even if
  a global of the same name exists (so a handler's `path` parameter, for
  example, can never accidentally alias an unrelated global `path`).
- A plain **assignment** (`x = ...;`) inside a function updates an
  existing variable if one is visible by that name (checking the
  function's own locals first, then the globals), and only creates a
  brand-new local variable if the name doesn't exist anywhere yet. This
  is what lets a function mutate shared global state:

```nac
counter = 0;

fn increment() {
    counter = counter + 1;   // updates the global `counter`, not a shadow
    rn counter;
};

out(increment());   // 1
out(increment());   // 2
out(counter);        // 2
```

(Before v3.6.0 this was a bug — the assignment always created a
function-local shadow, so the global `counter` was silently never
updated and the last line above would have printed `0`.)

---

## 4) Operators (lowest to highest precedence)

```
Logical     : &&   ||
Comparison  : ==  !=  <  >  <=  >=      (strcmp for strings, numeric otherwise)
Additive    : +  -
Multiplicative: *  /  %
Unary       : -x   !x
```

- `+` does automatic string concatenation if either side is a string
  (e.g. `"a" + 5` → `"a5"`); otherwise it's numeric (int/float) addition.
- Parentheses can be used to control precedence: `(a + b) * c`.
- **Comparisons do not chain.** `a < b < c` is a **parse error** (fixed in
  v3.5.0 — previously it silently parsed as `(a < b) < c`, a meaningless
  comparison that gave a wrong answer with no warning). Write
  `(a < b) && (b < c)` instead.
- No exponent operator (`^`) — use `pow(a, b)`.
- No bitwise operators (`& | ^ << >>`).
- No ternary operator (`a ? b : c`).

### Compound assignment (`+= -= *= /=`)

`x OP= y` desugars to `x = x OP y`, reusing the exact same semantics as the
matching binary operator (so `+=` does string concatenation when either
side is a string, just like `+`):

```nac
x = 10;
x += 5;    // 15
x -= 3;    // 12
x *= 2;    // 24
x /= 4;    // 6

s = "Hello";
s += ", world!";   // "Hello, world!"
```

It also works on array elements, map values, and inside a `for` loop's
increment clause:

```nac
a = [1, 2, 3];
a[0] += 100;        // a[0] is now 101

m = map();
m["count"] = 0;
m["count"] += 1;

for (i = 0; i < 10; i += 2) { out(i); };
```

The target must already exist — `y += 5;` on an undefined `y` is an error
(same "Undefined variable" behavior as reading `y` anywhere else), it does
not silently treat the missing variable as `0`.

---

## 5) Statement terminators

**Every** statement ends with `;` — including right after the closing `}`
of an `if` / `for` / `while` / `fn` block:

```nac
if (x > 0) {
    out("positive");
};              // <- required

for (i = 0; i < 10; i++) {
    out(i);
};              // <- required

while (x > 0) {
    x--;
};              // <- required

fn add(a, b) {
    rn a + b;
};              // <- required
```

Forgetting this `;` is the single most common source of parse errors.

---

## 6) if / else if / else

There is no `else` **keyword** — `:` is used instead. As of v3.5.0, `:`
followed by another condition chains properly (an actual else-if chain,
not a nested block). As of v3.6.0, the `if` keyword after `:` is
**optional** — since `(` already makes it unambiguous that a condition
follows, `} : if (b) { ... }` and `} : (b) { ... }` parse identically:

```nac
if (x < 0) {
    out("negative");
} : (x == 0) {
    out("zero");
} : (x < 10) {
    out("small positive");
} : {
    out("large positive");
};
```

You can chain as many `: (condition) { ... }` links as you like (the `if`
is optional on each one, and you can mix and match); the final
`: { ... }` (no condition) is the plain "else" branch and is optional.

---

## 7) Loops

```nac
for (i = 0; i < 10; i++) { ... };
for (i = 0; i < 10; i--) { ... };
for (i = 0; i < 10; i = i + 2) { ... };
for (;true;) { ... };          // init/increment are optional, the two ';' are not

while (condition) { ... };

break;      // exit the loop
continue;   // skip to the next iteration
```

The `for` loop's increment clause only accepts `i++`, `i--`, or
`i = <expression>` for a plain identifier — there's no indexed increment
like `arr[i]++`.

---

## 8) Functions

```nac
fn add(a, b) {
    rn a + b;
};

result = add(3, 4);
```

Rules and limits:

- Up to **100 functions** (`MAX_FUNCS`).
- Up to **10 parameters** (`MAX_PARAMS`).
- `rn` = return.
- Functions cannot be defined inside other functions (no nested `fn`).
- Functions are called **by name** — you cannot assign a function to a
  variable and call it through that variable (no first-class functions /
  callbacks). Where a function needs to be referenced dynamically (e.g.
  `serve()`), you pass its name as a **string**:
  `serve(8080, "handler");`
- Recursion works (see `factorial`, `fibonacci` examples).
- Max call stack depth: **100** (`MAX_CALL_DEPTH`) — reaching this now
  reliably produces a normal `"Stack overflow"` error on
  both Unix and Windows builds. Before that fix, recursive scripts could
  crash the OS process outright, with no error message at all, well
  before hitting this limit — as low as depth ~5-10 on Windows, since
  `Value`'s 8192-byte inline string buffer makes each level of recursion
  costly in raw C stack. If you see a crash with no NaC error message on
  an older build, rebuild first (`build.sh` / `build.bat`) before
  assuming it's a language bug.

### Bare function-call statements

A function call can now be its own statement, without assigning the
result to anything:

```nac
push(arr, 5);
serve(8080, "handler");
```

Previously this only worked inside an expression (assignment or `out()`).

---

## 9) Arrays

```nac
a = [1, 2, 3];             // literal
b = array(5);              // 5 elements, all zero
c = [];                    // empty array (same as array(0))

a[0]                       // read
a[0] = 10;                 // write (statement form, plain variable only)
length(a)                  // element count
push(a, 99);               // append to the end (fixed in v3.4.0 — now actually works)
pop(a)                      // remove and return the last element
first(a) / last(a)
reverse(a)
slice(a, start, end)
join(a, ", ")               // array -> string
split(s, ",")                // string -> array (new)
contains(s, sub)              // substring search on a STRING (not array membership)
```

Limitations:

- No multi-level indexing — `arr[i][j]` is not supported (array access
  only takes a single index; the result of an index can't be indexed
  again in the same expression).
- No element removal at an arbitrary index (only `pop()` from the end).
- No `for each` / `foreach` — use `for (i = 0; i < length(a); i++)`.

---

## 10) Maps (dictionaries)

```nac
m = map();
m["name"] = "Ada";
m["age"] = 30;

m["name"]                    // read -- null if the key doesn't exist (v1.0.0+)
has(m, "name")                 // key exists? (new, returns bool)
length(m)                      // number of keys
```

Limitations:

- Keys must be `int`, `float`, `string`, or `bool`.
- No map literal syntax (`{"a": 1}` is not valid — build it with `map()`
  plus one assignment per key).
- No key iteration (`for key in map`) — you access keys you already know.
- No key deletion.
- Reading a key that doesn't exist returns `null` (no error) — check with
  `has()` or `isNull()` if you need to tell "missing" apart from a value
  that's legitimately `null`.

---

## 11) Input / output

```nac
out(expr);                 // print to stdout + newline
in(var);                    // read from stdin into var
in(arr[i]);                  // read from stdin into an array slot

time()                       // current Unix timestamp (int) — legacy
now()                        // same thing, new builtin (v3.4.0)
sleep(ms)                    // sleep for `ms` milliseconds (new)
```

---

## 12) File operations

```nac
read(path)                     // read file contents as a string
write(path, contents)           // overwrite the file
append(path, contents)           // append to the file
```

---

## 13) HTTP

### Client

**Recommended: method-named functions.** The verb is the
function name instead of a separate string argument, and the body — if
any — can be a `map()` instead of a hand-escaped JSON string:

```nac
httpGet(url)                    // no body
httpDelete(url)                 // no body
httpPost(url, body)             // body optional
httpPut(url, body)              // body optional
httpPatch(url, body)            // body optional
```

All five return the response body as a string (pass it to `jsonParse()`,
or use `httpJson()` below, if you want it parsed). `body` may be a string
(sent as-is) or any other value such as a `map()`, which is automatically
JSON-serialized:

```nac
out(httpGet("https://example.com"));

payload = map();
payload["a"] = 1;
out(httpPost("https://example.com", payload));   // sends {"a":1}, no manual escaping
```

**Lower-level alternatives**, if you need the method as a runtime value
(e.g. it comes from a variable) rather than picking the function by name:

```nac
raw = httpRequest("GET", "https://example.com");
data = httpJson("GET", "https://example.com");   // auto jsonParse
```

**Statement form** — the original `http(method, url, body)` keyword also
still works, but only prints the response; it can't be assigned to a
variable, so prefer the functions above for anything beyond quick
one-off debugging:

```nac
http("GET", "https://example.com");
http("POST", "https://example.com", "{\"a\":1}");
```

### Server

```nac
fn handler(method, path, query, headers, body) {
    rn "hello";                       // plain string -> 200, body = that string
    // or:
    resp = map();
    resp["status"] = 404;
    resp["body"] = "not found";
    resp["headers"] = map();          // optional extra response headers
    rn resp;
};
serve(8080, "handler");                 // blocking, runs forever
```

Constraints:

- As of v3.6.0, `serve()` handles connections concurrently — one thread
  per connection, capped at 64 in flight (further connections simply wait
  for a slot). Accepting connections, reading requests, and writing
  responses run fully in parallel. The actual call into your handler
  function is still serialized behind an internal lock, since the
  interpreter's variables and call stack are shared global state — this
  is what makes it safe for two concurrent requests to read/write the
  same global variable. In practice this is rarely a bottleneck, but a
  handler that blocks for a long time (e.g. calls `sleep()`) will make
  other requests wait for the duration of that call.
- `method` / `path` / `body` are strings; `query` / `headers` are
  `map<string, string>`.
- `body` / `path` / header values are capped at 8192 characters
  (`MAX_STRING_LEN`), same as the rest of the language.

---

## 14) JSON

```nac
jsonParse(jsonString)          // string -> array/map/int/float/string/bool
jsonStringify(value)            // array/map/... -> string
```

`true` / `false` in JSON round-trip through NaC's `bool` type, not `1`/`0`.

---

## 15) Type conversion / introspection

```nac
typeOf(x)        // "int" | "float" | "bool" | "null" | "string" | "array" | "map"
isNull(x)
toInt(x)
toFloat(x)
toString(x)
urlEncode(s)
urlDecode(s)
```

---

## 16) CLI support

```nac
args()           // array of arguments after the script name
env("NAME")      // read an environment variable ("" if unset)
exit(code)       // terminate the program immediately
```

---

## 17) Math functions

```
sqrt pow sin cos tan abs floor ceil round log exp
```

---

## 18) String functions

```
length upper lower trim replace substr indexOf split contains
toString urlEncode urlDecode
```

---

## 19) Function-call syntax notes

- A call is always `name(arg1, arg2, ...)`.
- Whitespace between the name and `(` is fine (`name (arg)` works) since
  the lexer skips whitespace.
- No method-call / dot syntax (`object.method()`).
- No anonymous functions / lambdas.

---

## 20) Example combining most of the above

```nac
fn classify(x) {
    if (x < 0) {
        rn "negative";
    } : if (x == 0) {
        rn "zero";
    } : {
        rn "positive";
    };
};

numbers = [1, 2, 3, 4, 5];
total = 0;
for (i = 0; i < length(numbers); i++) {
    total = total + numbers[i];
};

out("Total: " + total);
out(classify(-5));

person = map();
person["name"] = "Ada";
person["age"] = 36;
person["active"] = true;
out(person["name"] + " is " + person["age"] + " years old");
out(typeOf(person["active"]));   // "bool"
```

---

## 21) Known limitations / possible simplification candidates

These are the roughest edges today — useful to keep in mind if you want to
simplify the language further:

- The mandatory `;` after every block's closing `}` (`if`/`for`/`while`/`fn`)
  is the most common source of parse errors; most C-like languages don't
  require this.
- `:` instead of the `else` keyword reads less naturally to most people
  coming from other languages.
- No map literal syntax (`{"a": 1}`) — verbose for JSON-heavy backend code.
- No first-class functions / callbacks — you can't write a generic
  `map(array, fn)` helper because a function name can't be stored in a
  variable and called through it (only referenced by string, as `serve()`
  does).
- No multi-level indexing (`arr[i][j]`, `obj["a"]["b"]`) — awkward for
  nested JSON-like data.
- `serve()` parallelizes I/O across connections, but each handler call
  is still serialized behind a lock (the interpreter's variables/call
  stack are shared global state) — a slow handler (e.g. one that calls
  `sleep()` or does heavy computation) will delay other requests for its
  duration, rather than truly running alongside them.
