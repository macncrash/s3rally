#!/bin/sh
# The online scoreboard end to end: build the server, start it on a scratch
# database with ./s3 as its verifier, run `s3 --score-test` against it, then
# check the dashboard and the public stats. Needs cargo and curl.
set -eu
cd "$(dirname "$0")/.."
make -s s3
(cd server && cargo build --release -q)
work=$(mktemp -d)
port=${S3_TEST_PORT:-18790}
admin=score-test-admin-$(od -An -N8 -tx1 /dev/urandom | tr -d ' \n')
cp server/games.txt "$work/games.txt"
S3_SCORES_DB="$work/scores.db" S3_SCORES_GAMES="$work/games.txt" S3_SCORES_ADDR="127.0.0.1:$port" \
    S3_VERIFIER="$PWD/s3" S3_ADMIN_TOKEN="$admin" S3_REQUIRE_SIGNED=1 server/target/release/s3-scores 2>"$work/server.log" &
server=$!
trap 'kill $server 2>/dev/null || true; rm -rf "$work"' EXIT
for _ in $(seq 50); do curl -fs "http://127.0.0.1:$port/healthz" >/dev/null 2>&1 && break; sleep 0.1; done
S3_SCORE_URL="http://127.0.0.1:$port" ./s3 --score-test
curl -fs "http://127.0.0.1:$port/v1/stats" | grep -q '"game":"rally","players_30d":1,"plays_30d":1,"plays_7d":1,"title":"(3) RALLY","up":1' && echo "ok   public stats count the play and the thumbs up" || { echo "FAIL public stats"; exit 1; }
curl -fs -u "admin:$admin" "http://127.0.0.1:$port/admin" | grep -q "handed in sooner" && echo "ok   the dashboard lists the held run for review" || { echo "FAIL dashboard"; exit 1; }
curl -fs -u "admin:$admin" "http://127.0.0.1:$port/admin" | grep -q "Norway ice stages" && echo "ok   the dashboard shows the feedback" || { echo "FAIL feedback on the dashboard"; exit 1; }
