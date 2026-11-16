#!/usr/bin/env python3
"""Python reference servers for bench/run.js.

    MODE=hello PORT=8080 python bench/python/server.py
    MODE=json PORT=8081 python bench/python/server.py
    MODE=static PORT=8082 STATIC_DIR=bench/nac/public python bench/python/server.py
"""

import json
import os
import posixpath
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlparse

MODE = os.environ.get("MODE", "hello")
PORT = int(os.environ.get("PORT", "8080"))
STATIC_DIR = os.path.abspath(os.environ.get("STATIC_DIR", "bench/nac/public"))

MIME = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".json": "application/json",
    ".txt": "text/plain; charset=utf-8",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def send_body(self, status, content_type, body, head_only=False):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if not head_only:
            self.wfile.write(body)

    def do_GET(self):
        self.handle_request(head_only=False)

    def do_HEAD(self):
        self.handle_request(head_only=True)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b""
        if MODE == "json" and urlparse(self.path).path == "/json":
            try:
                json.loads(raw)
            except Exception:
                self.send_body(400, "application/json", b'{"error":"invalid json"}')
                return
            self.send_body(200, "application/json", raw)
            return
        self.send_body(404, "text/plain", b"not found")

    def handle_request(self, head_only):
        if MODE == "hello":
            self.send_body(200, "text/plain; charset=utf-8", b"Hello, World!", head_only)
            return

        if MODE == "json":
            body = b'{"message":"Hello, World!","ok":true}'
            self.send_body(200, "application/json", body, head_only)
            return

        if MODE == "static":
            path = unquote(urlparse(self.path).path)
            rel = "index.html" if path == "/" else path.lstrip("/")
            file = os.path.abspath(os.path.join(STATIC_DIR, rel))
            if not file.startswith(STATIC_DIR + os.sep) and file != STATIC_DIR:
                self.send_body(403, "text/plain", b"forbidden", head_only)
                return
            try:
                with open(file, "rb") as f:
                    body = f.read()
            except OSError:
                self.send_body(404, "text/plain", b"not found", head_only)
                return
            ext = posixpath.splitext(file)[1].lower()
            self.send_body(200, MIME.get(ext, "application/octet-stream"), body, head_only)
            return

        self.send_body(404, "text/plain", b"unknown MODE")


def main():
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"python {MODE} server on http://127.0.0.1:{PORT}/")
    server.serve_forever()


if __name__ == "__main__":
    main()
