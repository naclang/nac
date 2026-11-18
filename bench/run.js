#!/usr/bin/env node
'use strict';

/*
 * Dependency-free HTTP benchmark runner.
 *
 * By default the runner sends "Connection: close" and opens a new connection
 * for every request, which measures connection setup + one request. Pass
 * --keep-alive to reuse connections instead (the way browsers and production
 * clients talk to a server).
 *
 * Usage:
 *   node bench/run.js --url http://127.0.0.1:8080/
 *   node bench/run.js --url http://127.0.0.1:8080/json --method POST \
 *       --body '{"message":"Hello"}' --duration 10 --concurrency 50
 *
 * Options:
 *   --url <url>          target URL (required)
 *   --method <method>    HTTP method (default: GET)
 *   --body <text>        request body
 *   --header "K: V"      extra header, repeatable
 *   --duration <sec>     measured seconds (default: 5)
 *   --warmup <sec>       warmup seconds (default: 1)
 *   --concurrency <n>    parallel requests (default: 50)
 *   --keep-alive         reuse connections instead of closing each one
 *   --json               print machine-readable JSON instead of a table
 */

const http = require('http');
const https = require('https');

function parseArgs(argv) {
  const opts = {
    url: null,
    method: 'GET',
    body: null,
    headers: [],
    duration: 5,
    warmup: 1,
    concurrency: 50,
    keepAlive: false,
    json: false,
  };

  for (let i = 2; i < argv.length; i++) {
    const arg = argv[i];
    const next = () => {
      if (i + 1 >= argv.length) throw new Error(`Missing value for ${arg}`);
      return argv[++i];
    };
    switch (arg) {
      case '--url': opts.url = next(); break;
      case '--method': opts.method = next().toUpperCase(); break;
      case '--body': opts.body = next(); break;
      case '--header': opts.headers.push(next()); break;
      case '--duration': opts.duration = Number(next()); break;
      case '--warmup': opts.warmup = Number(next()); break;
      case '--concurrency': opts.concurrency = Number(next()); break;
      case '--keep-alive': opts.keepAlive = true; break;
      case '--json': opts.json = true; break;
      case '--help':
      case '-h':
        console.log('See the comment at the top of bench/run.js for usage.');
        process.exit(0);
        break;
      default:
        throw new Error(`Unknown option: ${arg}`);
    }
  }

  if (!opts.url) throw new Error('--url is required');
  if (!(opts.duration > 0)) throw new Error('--duration must be > 0');
  if (!(opts.warmup >= 0)) throw new Error('--warmup must be >= 0');
  if (!(opts.concurrency >= 1)) throw new Error('--concurrency must be >= 1');
  return opts;
}

function parseHeaders(list, body, keepAlive) {
  const headers = keepAlive ? {} : { Connection: 'close' };
  for (const item of list) {
    const idx = item.indexOf(':');
    if (idx <= 0) throw new Error(`Invalid --header value: ${item}`);
    headers[item.slice(0, idx).trim()] = item.slice(idx + 1).trim();
  }
  if (body !== null && !Object.keys(headers).some((k) => k.toLowerCase() === 'content-type')) {
    headers['Content-Type'] = 'application/json';
  }
  if (body !== null) headers['Content-Length'] = Buffer.byteLength(body);
  return headers;
}

function percentile(sorted, p) {
  if (sorted.length === 0) return 0;
  const idx = Math.min(sorted.length - 1, Math.max(0, Math.ceil((p / 100) * sorted.length) - 1));
  return sorted[idx];
}

async function runPhase(target, opts, headers, seconds) {
  const durationMs = seconds * 1000;
  const latencies = [];
  let errors = 0;
  let firstError = null;
  const deadline = Date.now() + durationMs;
  const transport = target.protocol === 'https:' ? https : http;
  const agent = opts.keepAlive
    ? new transport.Agent({ keepAlive: true, maxSockets: opts.concurrency })
    : false;

  const requestOnce = () => new Promise((resolve) => {
    const started = process.hrtime.bigint();
    const req = transport.request(target, {
      method: opts.method,
      headers,
      agent,
    }, (res) => {
      res.resume();
      res.on('end', () => {
        const elapsed = Number(process.hrtime.bigint() - started) / 1e6;
        if (res.statusCode >= 400) {
          errors++;
          if (!firstError) firstError = `HTTP ${res.statusCode}`;
        } else {
          latencies.push(elapsed);
        }
        resolve();
      });
    });
    req.setTimeout(10000, () => req.destroy(new Error('request timeout')));
    req.on('error', (err) => {
      errors++;
      if (!firstError) firstError = err.message;
      resolve();
    });
    if (opts.body !== null) req.write(opts.body);
    req.end();
  });

  const worker = async () => {
    while (Date.now() < deadline) await requestOnce();
  };

  const startedAt = Date.now();
  await Promise.all(Array.from({ length: opts.concurrency }, worker));
  const elapsedMs = Date.now() - startedAt;

  latencies.sort((a, b) => a - b);
  const total = latencies.reduce((a, b) => a + b, 0);
  if (agent) agent.destroy();
  return {
    requests: latencies.length,
    errors,
    firstError,
    seconds: elapsedMs / 1000,
    rps: latencies.length / (elapsedMs / 1000),
    min: latencies.length ? latencies[0] : 0,
    mean: latencies.length ? total / latencies.length : 0,
    p50: percentile(latencies, 50),
    p90: percentile(latencies, 90),
    p99: percentile(latencies, 99),
    max: latencies.length ? latencies[latencies.length - 1] : 0,
  };
}

function fmt(n) {
  return Number(n).toFixed(2);
}

async function main() {
  const opts = parseArgs(process.argv);
  const target = new URL(opts.url);
  const headers = parseHeaders(opts.headers, opts.body, opts.keepAlive);

  if (!opts.json) {
    console.log(`Target:      ${opts.method} ${target.href}`);
    console.log(`Concurrency: ${opts.concurrency}  Duration: ${opts.duration}s  Warmup: ${opts.warmup}s` +
                `  Keep-alive: ${opts.keepAlive ? 'yes' : 'no'}`);
  }

  if (opts.warmup > 0) {
    await runPhase(target, opts, headers, opts.warmup);
    if (!opts.json) console.log('Warmup done.');
  }

  const result = await runPhase(target, opts, headers, opts.duration);
  result.url = target.href;
  result.method = opts.method;
  result.concurrency = opts.concurrency;
  result.keepAlive = opts.keepAlive;
  result.duration = opts.duration;

  if (opts.json) {
    console.log(JSON.stringify(result, null, 2));
    return;
  }

  console.log('');
  console.log(`Requests:    ${result.requests} in ${fmt(result.seconds)}s`);
  console.log(`Throughput:  ${fmt(result.rps)} req/s`);
  console.log(`Latency:     min ${fmt(result.min)} ms  mean ${fmt(result.mean)} ms  ` +
              `p50 ${fmt(result.p50)} ms  p90 ${fmt(result.p90)} ms  ` +
              `p99 ${fmt(result.p99)} ms  max ${fmt(result.max)} ms`);
  console.log(`Errors:      ${result.errors}${result.firstError ? ` (first: ${result.firstError})` : ''}`);
}

main().catch((err) => {
  console.error(`error: ${err.message}`);
  process.exit(1);
});
