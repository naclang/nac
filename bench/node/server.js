'use strict';

// Node.js reference servers for bench/run.js.
//
//   MODE=hello PORT=8080 node bench/node/server.js
//   MODE=json PORT=8081 node bench/node/server.js
//   MODE=static PORT=8082 STATIC_DIR=bench/nac/public node bench/node/server.js

const fs = require('fs');
const http = require('http');
const path = require('path');

const mode = process.env.MODE || 'hello';
const port = Number(process.env.PORT || 8080);
const staticDir = path.resolve(process.env.STATIC_DIR || 'bench/nac/public');

const mime = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json',
  '.txt': 'text/plain; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
};

function send(res, status, type, body) {
  res.writeHead(status, {
    'Content-Type': type,
    'Content-Length': Buffer.byteLength(body),
    Connection: 'close',
  });
  res.end(body);
}

const server = http.createServer((req, res) => {
  res.shouldKeepAlive = false;

  if (mode === 'hello') {
    send(res, 200, 'text/plain; charset=utf-8', 'Hello, World!');
    return;
  }

  if (mode === 'json') {
    if (req.method === 'POST' && req.url === '/json') {
      const chunks = [];
      req.on('data', (chunk) => chunks.push(chunk));
      req.on('end', () => {
        const raw = Buffer.concat(chunks).toString('utf8');
        try {
          JSON.parse(raw);
        } catch {
          send(res, 400, 'application/json', '{"error":"invalid json"}');
          return;
        }
        send(res, 200, 'application/json', raw);
      });
      return;
    }
    send(res, 200, 'application/json', '{"message":"Hello, World!","ok":true}');
    return;
  }

  if (mode === 'static') {
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      send(res, 405, 'text/plain', 'method not allowed');
      return;
    }
    const urlPath = decodeURIComponent((req.url || '/').split('?')[0]);
    const rel = urlPath === '/' ? 'index.html' : urlPath.replace(/^\/+/, '');
    const file = path.resolve(staticDir, rel);
    if (file !== staticDir && !file.startsWith(staticDir + path.sep)) {
      send(res, 403, 'text/plain', 'forbidden');
      return;
    }
    fs.readFile(file, (err, data) => {
      if (err) {
        send(res, 404, 'text/plain', 'not found');
        return;
      }
      const type = mime[path.extname(file).toLowerCase()] || 'application/octet-stream';
      res.writeHead(200, {
        'Content-Type': type,
        'Content-Length': data.length,
        Connection: 'close',
      });
      res.end(req.method === 'HEAD' ? undefined : data);
    });
    return;
  }

  send(res, 404, 'text/plain', 'unknown MODE');
});

server.listen(port, '127.0.0.1', () => {
  console.log(`node ${mode} server on http://127.0.0.1:${port}/`);
});
