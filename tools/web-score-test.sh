#!/bin/sh
# The score client in a browser: builds tools/web-score-test.cpp with Emscripten
# (em++ on the PATH) into one self-contained page, serves it (macOS ruby), and
# starts a local score server that page may call. Open the page it prints: its
# title turns to "WEB SCORE TEST OK" when every step passed.
set -eu
cd "$(dirname "$0")/.."
port=${S3_TEST_PORT:-18796}
page=${S3_PAGE_PORT:-18797}
out=${1:-build-web/score-test}
mkdir -p "$out"
cat > "$out/shell.html" <<HTML
<!doctype html><meta charset=utf-8><title>web score test</title>
<pre id=log></pre>
<script>window.S3_SCORE_URL = 'http://127.0.0.1:$port'; var Module = {};</script>
{{{ SCRIPT }}}
HTML
em++ -std=c++17 -O1 -Isrc tools/web-score-test.cpp src/console/*.cpp -sUSE_SDL=2 -sSINGLE_FILE=1 -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_RUNTIME_METHODS=UTF8ToString --shell-file "$out/shell.html" -o "$out/index.html"
(cd server && cargo build --release -q)
work=$(mktemp -d)
cp server/games.txt "$work/games.txt"
ruby -run -e httpd "$out" -p "$page" -b 127.0.0.1 >/dev/null 2>&1 &
httpd=$!
trap 'kill $httpd 2>/dev/null; rm -rf "$work"' EXIT INT TERM
echo "page:   http://127.0.0.1:$page/index.html"
echo "server: http://127.0.0.1:$port  (ctrl-c stops both)"
S3_SCORES_DB="$work/scores.db" S3_SCORES_GAMES="$work/games.txt" S3_SCORES_ADDR="127.0.0.1:$port" \
    S3_CORS_ORIGINS="http://127.0.0.1:$page" S3_REQUIRE_SIGNED=1 server/target/release/s3-scores
