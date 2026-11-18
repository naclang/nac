# NaC examples

Run every command from the project root (the folder that contains `nac` / `nac.exe`).

## basics/ - language tour

Short scripts, meant to be read in order. They run to completion and print their results.

| File | Shows |
| ---- | ----- |
| `01_variables_and_strings.nac` | variables, string joining, template strings, `+=`, conversions |
| `02_control_flow.nac` | `if/elif/else`, `while`, `for`, `break`, nested loops, short-circuit |
| `03_functions.nac` | functions, recursion (factorial, Fibonacci), a primality check |
| `04_arrays.nac` | arrays: `push`, `removeAt`, sum/max, bubble sort, nested arrays |
| `05_maps.nac` | maps: literals, dot access, `keys`, `in`, `delete`, multi-key assignment |
| `06_errors.nac` | `try/catch`, `throw`, defaults on failure, network errors |
| `07_input.nac` | reading input with `input()` |
| `08_guess_game.nac` | a small interactive game |
| `09_cli_tool.nac` | `args()`, `env()` and `exit()`: `nac examples/basics/09_cli_tool.nac greet Ada` |
| `10_builtin_tour.nac` | math, string, array and file built-ins (creates `builtin_tour.tmp.txt`) |

## web/ - servers

These start a server and run until you stop them. Each file lists its `curl` commands at the top.

| File | Shows |
| ---- | ----- |
| `01_hello.nac` | routes, `:id` params, `?query`, JSON and 404 responses |
| `02_json_api.nac` | a REST API: POST/GET/PATCH/DELETE, validation, `try/catch` -> 400 |
| `03_guestbook.nac` | an HTML form, form parsing, HTML escaping, data kept in a file |
| `04_api_gateway.nac` | a route that calls another API, with a callback and a 502 fallback (needs internet) |
| `05_http_client.nac` | the HTTP client on its own: GET/POST/PUT/DELETE, headers, callbacks (needs internet) |
| `06_accounts.nac` | register/login/logout, `HttpOnly` sessions, protected routes, role checks |
| `07_request_helpers.nac` | URL-encoded forms, response headers, CORS and preflight |
| `08_templates.nac` | HTML templates with escaped placeholders |
| `09_config.nac` | `.env`, `env()` defaults and `serve()` configuration |
| `10_jwt.nac` | JWT login, a bearer-token route and the built-in `/health` |
| `11_middleware.nac` | `before`/`after` blocks: auth check, CORS preflight, request logging |
| `12_sqlite_todos.nac` | the todo API from `02_json_api.nac`, kept in a SQLite file (needs a SQLite build) |

## apps/ - complete programs

| Folder | What it is |
| ------ | ---------- |
| `todo/` | a full todo app on SQLite: JSON API + web UI, tags, priorities, due dates, search, paging, transactions, self test (`TODO_SELFTEST=1 TODO_DB=:memory: ./nac test examples/apps/todo/todo.nac`) |
| `todo_json/` | a smaller todo app that keeps its tasks in a JSON file |
| `site/` | a site made of static files plus routes and a redirect |

Current language limits are summarised in
[the docs overview](https://naclang.github.io/docs/index.html). The `assert()`-based checks live in
`examples/apps/todo/todo.nac` (run them with `TODO_SELFTEST=1 TODO_DB=:memory: nac test examples/apps/todo/todo.nac`).
