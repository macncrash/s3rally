#!/bin/sh
# Lines for server/games.txt, one per s3games title, so the score server takes
# their plays, thumbs and feedback. Their scores wait for a person (review)
# until a title has its own verifier. Usage: tools/s3games-list.sh ~/dev/s3games
set -eu
root=${1:?usage: $0 path/to/s3games}
for d in "$root"/s3*/; do
    slug=$(basename "$d")
    [ "$slug" = s3 ] && continue
    printf '%s' "$slug" | grep -Eq '^[a-z0-9]{1,24}$' || continue
    title=$(grep -m1 '^# ' "$d/README.md" 2>/dev/null | sed 's/^# //' | tr -cd 'A-Za-z0-9 ().!-' || true)
    [ -n "$title" ] || title=$(printf '%s' "$slug" | tr a-z A-Z)
    printf '%-14s points review %s\n' "$slug" "$title"
done
