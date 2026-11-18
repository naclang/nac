#!/bin/sh
# usage: httpbench.sh <hello|json> <labelA> <binA> [<labelB> <binB>]   (repo root)
# Alternates the binaries for 3 rounds with the rate limit disabled and
# prints requests/s and error count for each run. BENCH_ARGS adds run.js flags.
APP="$1"; LA="$2"; BA="$3"; LB="$4"; BB="$5"
URL="http://127.0.0.1:18120/"; [ "$APP" = json ] && URL="${URL}json"
run() {
  NAC_RATE_LIMIT_MAX=0 PORT=18120 "$2" bench/nac/$APP.nac >/dev/null 2>&1 &
  PID=$!; sleep 2
  node bench/run.js --url "$URL" --duration 5 --warmup 1 --concurrency 50 $BENCH_ARGS --json \
    | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const r=JSON.parse(s);console.log(process.argv[1],"rps="+Math.round(r.requestsPerSecond??r.rps),"errors="+r.errors)})' "$1"
  taskkill //F //PID $PID >/dev/null 2>&1; sleep 1
}
for round in 1 2 3; do
  run "$LA" "$BA"
  [ -n "$BB" ] && run "$LB" "$BB"
done
