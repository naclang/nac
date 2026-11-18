# Benchmarks

Reproducible HTTP benchmarks for NaC. The suite covers the scenarios from the
roadmap: hello-world, JSON, static files, and outgoing HTTP requests. Reference
servers for Node.js, Python, and Go are included so results can be compared on
the same machine.

Claims in the README or website should point at a published run: hardware, OS,
compiler/runtime versions, exact commands, and the JSON output of `run.js`.

Run all commands from the repository root. Examples below use POSIX shell
environment syntax (`PORT=8080 nac ...`); in PowerShell use
`$env:PORT=8080; nac ...` and clear the variables afterwards
(`Remove-Item Env:PORT`).

## Disable the rate limiter

NaC rate-limits each client IP by default (120 requests per window), so a
single-machine benchmark would mostly measure `429 Too Many Requests`
responses. Start every NaC server under test with the limiter off, and note
that `run.js` counts any status >= 400 as an error:

```text
NAC_RATE_LIMIT_MAX=0 PORT=8080 nac bench/nac/hello.nac
```

## Scenarios

| Scenario | NaC app | URL | Reference server |
| --- | --- | --- | --- |
| Hello world | `bench/nac/hello.nac` | `/` | `MODE=hello` |
| JSON | `bench/nac/json.nac` | `/json` | `MODE=json` |
| JSON echo | `bench/nac/json.nac` | `POST /json` | `MODE=json` |
| Static file | `bench/nac/static.nac` | `/` | `MODE=static` |
| Outgoing HTTP | `bench/nac/http_client.nac` | `/` | n/a |

## Runner

`bench/run.js` needs only Node.js (no npm packages, no `wrk`). By default it
sends `Connection: close`, measuring connection setup plus one request. Add
`--keep-alive` to reuse connections the way browsers and production clients do.
Use the same mode for every server in a comparison.

```text
node bench/run.js --url http://127.0.0.1:8080/
node bench/run.js --url http://127.0.0.1:8080/ --keep-alive
node bench/run.js --url http://127.0.0.1:8080/json \
  --method POST --body '{"message":"Hello"}'
node bench/run.js --url http://127.0.0.1:8080/ --duration 10 --concurrency 100
node bench/run.js --url http://127.0.0.1:8080/ --json > result.json
```

Options: `--url`, `--method`, `--body`, `--header "Name: value"` (repeatable),
`--duration`, `--warmup`, `--concurrency`, `--keep-alive`, `--json`. See the
comment at the top of `bench/run.js`.

If `wrk` is installed, an equivalent keep-alive run is:

```text
wrk -t4 -c100 -d10s http://127.0.0.1:8080/
```

## Hello world

Start one server at a time, then run `run.js`. Run from the repository root.

```text
# NaC
PORT=8080 nac bench/nac/hello.nac

# Node.js
MODE=hello PORT=8080 node bench/node/server.js

# Python
MODE=hello PORT=8080 python bench/python/server.py

# Go
go build -o build/bench-server ./bench/go
MODE=hello PORT=8080 build/bench-server
```

## JSON and JSON echo

```text
PORT=8080 nac bench/nac/json.nac
node bench/run.js --url http://127.0.0.1:8080/json
node bench/run.js --url http://127.0.0.1:8080/json --method POST \
  --body '{"message":"Hello"}'
```

Reference servers use the same `MODE=json PORT=...` pattern with
`bench/node/server.js`, `bench/python/server.py`, or the Go binary.

## Static file

```text
PORT=8080 nac bench/nac/static.nac
node bench/run.js --url http://127.0.0.1:8080/
```

Reference servers:

```text
MODE=static STATIC_DIR=bench/nac/public PORT=8080 node bench/node/server.js
MODE=static STATIC_DIR=bench/nac/public PORT=8080 python bench/python/server.py
MODE=static STATIC_DIR=bench/nac/public PORT=8080 build/bench-server
```

NaC caches small static files in memory (keyed by path, mtime, and size) and
answers conditional requests with `304 Not Modified`. The reference servers
shown here read the file on every request, so also run a cold-cache variant
(restart the server before each measured run) when comparing disk-bound
behavior.

## Outgoing HTTP

The NaC app makes one outgoing request per incoming request, so this measures
request handling plus the HTTP client path.

```text
# terminal 1: upstream
PORT=8092 nac bench/nac/upstream.nac

# terminal 2: benchmark target
PORT=8080 UPSTREAM=http://127.0.0.1:8092/ nac bench/nac/http_client.nac

# terminal 3
node bench/run.js --url http://127.0.0.1:8080/
```

Run the upstream and the benchmark target on separate processes (separate
cores if possible) so the upstream is not the bottleneck.

## Publishing results

Include at least:

- CPU, RAM, OS version, and whether the machine is a VM/container.
- NaC commit hash, compiler and optimization flags (`build.bat` / `build.sh`).
- Node.js, Python, and Go versions.
- The exact `run.js` (or `wrk`) command for each row.
- Raw `--json` output for every run, not just the best number.
- Median of at least three runs; do not compare a single warm run.

Do not mix connection modes: if NaC closes connections, every server in the
comparison must close them too.
