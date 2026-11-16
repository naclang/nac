# Deploying NaC

NaC speaks plain HTTP. For HTTPS, put a reverse proxy in front of it. The proxy
terminates TLS, renews certificates and talks to NaC over a loopback address.
This page covers that setup and the flags that make it work.

## 1. Run NaC for a proxy

Listen on loopback only, so the only way in is through the proxy:

```bash
./nac --host 127.0.0.1 --port 8080 --health app.nac
# or, for a program built with `nac build`:
./app --host 127.0.0.1 --port 8080 --health
```

Flags can go anywhere on the command line (`nac app.nac --port 8080` is the same
as `nac --port 8080 app.nac`) and are not passed to the script. Use `--` to pass
everything after it through untouched.

| Flag                    | Environment variable        | Default     | Meaning |
| ----------------------- | --------------------------- | ----------- | ------- |
| `--port N`              | `PORT` (see below)          | `8080`      | Listen port, 1 to 65535 |
| `--host ADDR`           | `NAC_HOST`                  | `0.0.0.0`   | IPv4 address or name to bind. `127.0.0.1` is loopback only |
| `--max-connections N`   | `NAC_MAX_CONNECTIONS`       | `64`        | Connections open at once (1 to 4096). More wait in `accept()` |
| `--workers N`           | `NAC_WORKERS`               | `8`         | Worker threads that run requests (1 to 256). Linux and macOS; Windows uses a thread per connection |
| `--read-timeout-ms N`   | `NAC_READ_TIMEOUT_MS`       | `10000`     | Deadline for receiving a whole request (100 to 600000) |
| `--max-header-bytes N`  | `NAC_MAX_HEADER_BYTES`      | `16384`     | Header block limit (1024 to 32768), over it: `431` |
| `--rate-limit N`        | `NAC_RATE_LIMIT_MAX`        | `120`       | Requests per client IP per window. `0` turns the limiter off |
| `--rate-window-sec N`   | `NAC_RATE_LIMIT_WINDOW_SEC` | `60`        | Length of that window (1 to 86400) |
| `--health`              | `NAC_HEALTH`                | off         | Answer `GET /health` with `{"status":"ok"}` |
| `--shutdown-timeout-ms N` | `NAC_SHUTDOWN_TIMEOUT_MS` | `10000`     | How long to wait for requests in flight when stopping (0 to 600000). `0` = don't wait |

Priority, highest first: command-line flag, then the environment (including a
`.env` file), then the default. For the port and host there is one more step:
`--port` beats `serve(port)`, which beats `PORT`, which beats `8080`. In the same
way `--host` beats `serve(port, host)`, which beats `NAC_HOST`.

Bad values stop the server at start-up with a message, for example
`--port: expected an integer from 1 to 65535, got 'abc'`.

`HOST` is deliberately not read: some shells export it as the machine name.

The request **body** limit (8191 bytes) is not configurable. It is the size of a
NaC string and goes away with heap-backed strings (Roadmap item 5).

### The health check

`--health` adds `GET /health` (and `HEAD`), returning `200` and
`{"status":"ok"}`. It is exempt from the rate limiter, so a probe is never
turned into a `429`. If your script defines its own `route GET "/health"`, that
route is used instead, which is the way to report database status and the like.
The built-in one only says that the process is up and accepting requests.

## 2. Caddy

Caddy obtains and renews certificates by itself and sets `X-Forwarded-For` and
`X-Forwarded-Proto`. Point a domain's DNS at the server, then:

```caddyfile
example.com {
    encode gzip
    request_body {
        max_size 8KB
    }
    reverse_proxy 127.0.0.1:8080 {
        health_uri /health
        health_interval 10s
    }
}
```

## 3. nginx

Get a certificate (for example `sudo certbot certonly --nginx -d example.com`),
then:

```nginx
server {
    listen 80;
    server_name example.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl;
    server_name example.com;

    ssl_certificate     /etc/letsencrypt/live/example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/example.com/privkey.pem;

    client_max_body_size 8k;        # NaC's own limit is 8191 bytes

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host              $host;
        proxy_set_header X-Real-IP         $remote_addr;
        proxy_set_header X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 30s;
    }
}
```

NaC keeps HTTP/1.1 connections alive (idle ones close after a few seconds), so
an upstream can reuse them. Idle connections are watched by one `poll()` thread
and only occupy a worker while a request is being read and handled, so
`--max-connections` can be much larger than `--workers`. In nginx, enable HTTP/1.1 and an upstream keep-alive
pool:

```nginx
upstream nac {
    server 127.0.0.1:8080;
    keepalive 32;
}

location / {
    proxy_pass http://nac;
    proxy_http_version 1.1;
    proxy_set_header Connection "";
    proxy_set_header Host              $host;
    proxy_set_header X-Real-IP         $remote_addr;
    proxy_set_header X-Forwarded-For   $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_read_timeout 30s;
}
```

## 4. Things that change behind a proxy

**Rate limiting sees only the proxy.** NaC counts requests per socket address,
not per `X-Forwarded-For`. Behind a proxy every visitor arrives from
`127.0.0.1`, so the default 120 requests a minute is shared by **all** of them.
Do one of these:

- turn NaC's limiter off and limit in the proxy (nginx `limit_req`, Caddy rate
  limit module): `--rate-limit 0`, or
- raise it to a figure for your whole site: `--rate-limit 6000`.

**Cookies.** Once the site is served over HTTPS, set `secure: true` on cookies:
`setCookie("sid", sid, { httpOnly: true, secure: true, sameSite: "Lax" })`.

**Client address and scheme.** Routes can read `headers["x-forwarded-for"]` and
`headers["x-forwarded-proto"]`. Trust them only because the proxy sets them and
NaC listens on loopback; a NaC exposed directly would let clients forge them.

**Redirect HTTP to HTTPS** in the proxy (the nginx example above does; Caddy does
it automatically).

## 5. Keeping it running

systemd (`/etc/systemd/system/nac-app.service`):

```ini
[Unit]
Description=NaC app
After=network.target

[Service]
WorkingDirectory=/srv/app
ExecStart=/srv/app/app --host 127.0.0.1 --port 8080 --health
Environment=JWT_SECRET=change-me
Restart=on-failure
User=www-data
# systemd sends SIGTERM, NaC drains (up to --shutdown-timeout-ms), then exits.
# Keep TimeoutStopSec above the shutdown timeout (systemd default: 90s).
TimeoutStopSec=30

[Install]
WantedBy=multi-user.target
```

### Stopping cleanly

On `SIGINT` or `SIGTERM` NaC closes its listening socket (new connections are
refused, so a proxy or load balancer fails over), lets every request already
running finish, and then exits with status 0. It waits at most
`--shutdown-timeout-ms` (default 10 seconds). If requests are still running
after that, NaC exits with status 1 and they are cut off. A second signal exits
at once with status 130. Windows handles Ctrl+C, Ctrl+Break and console close.

Docker: `docker stop` sends `SIGTERM` and waits 10 seconds before `SIGKILL`. For
a longer drain use `docker stop -t 30` and a matching `--shutdown-timeout-ms`.
On Unix, `nac dev` stops its child the same way when it restarts on a file change.

Docker: bind to every interface inside the container and publish the port on
loopback only.

```dockerfile
CMD ["./app", "--host", "0.0.0.0", "--port", "8080", "--health"]
```

```bash
docker run -p 127.0.0.1:8080:8080 my-nac-app
```

A health probe for Docker or Kubernetes is `GET /health` on that port.

## Known limits

- `--host` takes IPv4 addresses and names that resolve to IPv4. IPv6 listening is not supported.
- No native TLS. This page is the supported way to serve HTTPS.
