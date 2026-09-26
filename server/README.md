# s3-scores: the S3 online scoreboard

A small server (Rust, one SQLite file) for the S3 games. It handles four things, all opt-in:
- **Leaderboards** of verified scores.
- **Play counts** from registered players.
- **Thumbs up/down** ratings.
- A **dashboard** that shows which games people play and which have gone quiet.

## What a player shares

When a player chooses to join, the game sends:
- the random player ID it already made (a UUID)
- a name of up to 12 characters

The server replies with a secret token, and the game keeps it in `score.txt`. The server stores only the token's SHA-256.

The server never stores an email, a real name or an IP address. It sees addresses only in memory, for rate limits. `DELETE /v1/players/me` removes a player and everything they sent.

## How a score is trusted

The games are open source and the web build runs in the browser, so a secret inside the game (an HMAC key, say) could be pulled out and used to sign anything. Trust comes from layers the server controls instead:

| Layer | What it proves |
|---|---|
| **Token** from registration, used as a signing key | The upload comes from whoever registered that ID |
| **Signed requests**: every signed-in call carries the computer's clock (ms), a one-time nonce and an HMAC-SHA256 over the method, path, time, nonce and body. The token itself never travels again | A copied request can't be sent twice, a changed one fails its signature, and clocks more than 10 s off are logged for review (refused beyond 15 min) |
| **Ticket**, taken when a run starts: single use, stamped with the server's clock | The run started then, and each ticket counts once |
| **Timing**: a timed run can't be handed in sooner than it could have been driven | Nobody submits a 2-minute stage 40 seconds after starting it |
| **Replay**: the game's inputs plus a snapshot of the car every second. `s3 --verify-run` replays each second and checks it lands on the next snapshot, then that the stage ends at the claimed time | The time follows from real driving through the real physics |
| **Review**: anything doubtful waits for a person on the dashboard; only `accepted` runs reach a board | Mistakes and edge cases don't go public |

The replay check works because the games run on a fixed 1/60 s step, so the same inputs give the same drive. It goes a second at a time because maths libraries differ in their last bits between macOS, Linux and the browser. Checking each second on its own keeps that drift from building up into a false alarm. A cheater can move the car by at most about a millimetre per second without the run going to review.

A player who joins after a record run has no ticket for it. The server still replays it, but only a person can accept it.

**What it can't catch:** a perfect bot driving with real inputs. Nothing short of anti-cheat can. The review queue is for runs that look wrong.

## Running it

```sh
cd server
cargo build --release
S3_VERIFIER=$PWD/../s3 S3_ADMIN_TOKEN=$(openssl rand -hex 20) target/release/s3-scores
```

| Variable | Default | |
|---|---|---|
| `S3_SCORES_DB` | `s3-scores.db` | The SQLite file |
| `S3_SCORES_ADDR` | `127.0.0.1:8790` | Where it listens |
| `S3_SCORES_GAMES` | `games.txt` | The games it knows (see the file) |
| `S3_VERIFIER` | none | The `s3` binary that runs `--verify-run`. Without it, timed runs wait for review |
| `S3_ADMIN_TOKEN` | none | Password for `/admin` (user `admin`), at least 20 characters. Without it, the dashboard is off |
| `S3_TRUST_PROXY` | off | `1` takes client addresses from `X-Forwarded-For`. Only set it behind your own proxy |
| `S3_REQUIRE_SIGNED` | off | `1` refuses plain bearer tokens: every signed-in call must be signed (the games always sign) |
| `S3_CORS_ORIGINS` | none | Web pages that may call the API, comma-separated: the browser build's page |
| `S3_GITHUB_REPO`, `S3_GITHUB_TOKEN` | none | Feedback also opens an issue in `owner/repo`. Use a fine-grained token with only issues access to that one repository |

Games point at the server with `S3_SCORE_URL` or a `score_url=` line in the console settings (`console.cfg`). A game accepts only `https://`, or `http://` to this machine. With no server set, the online features are simply absent.

**Adding a game:** add a line to `games.txt`. Points games, or games without a verifier yet, use `review`.

## The dashboard

`/admin` shows:
- the games, most played first, with players, finishes, minutes and thumbs, marking quiet and disliked ones
- runs waiting for review
- players' feedback
- the security log: bad signatures, replayed requests and clocks that were off

## Putting it on the internet

1. Run it on a small machine as its own user, with the database on a disk that is backed up.
2. Put TLS in front: Caddy is simplest (`reverse_proxy 127.0.0.1:8790`), or nginx with Let's Encrypt. Keep the server itself on `127.0.0.1`, and set `S3_TRUST_PROXY=1`.
3. Build `s3` on the same machine for `S3_VERIFIER`. It needs no display.
4. Point the games at `https://your-host`.

The Docker image does all of this for you: see **[DEPLOY.md](DEPLOY.md)**.

## API

| | |
|---|---|
| `POST /v1/players {id, name}` | Join. Returns `{token, short_id}` |
| `DELETE /v1/players/me` | Leave; everything of yours is deleted |
| `POST /v1/tickets {game, stage}` | A run starts. Returns `{ticket}` |
| `POST /v1/runs {ticket, score, replay, build}` | Hand in a run. Returns `{status, reason, rank, of}` |
| `GET /v1/leaderboard/{game}/{stage}` | Top 10 and, when signed in, where you stand |
| `POST /v1/plays {game, event, seconds}` | `start` or `finish` of a session |
| `PUT /v1/ratings/{game} {thumb}` | +1 or -1; can be changed |
| `GET /v1/stats` | Public popularity per game, for the launcher |
| `POST /v1/feedback {game, text, build}` | Anyone. Kept for the dashboard, and opened as a GitHub issue when configured. The words go in a code block, so links and @mentions stay inert |
| `GET /admin` | The dashboard (Basic auth) |

Signed-in calls send `X-S3-Auth: <id>:<unix ms>:<nonce>:<HMAC-SHA256(SHA-256(token), METHOD\nPATH\nms\nnonce\nhex(SHA-256(body)))>` (see `signed_requests` in `src/lib.rs`, and `ScoreClient::signature` in the game). Bearer tokens are still accepted unless `S3_REQUIRE_SIGNED=1`.

## Tests

- `cargo test` runs the API in memory. It covers:
  - registration rules, tickets, timing and verifier verdicts
  - rate limits, ratings, stats and deleting a player
  - the dashboard's password and form protection
- `../tools/score-test.sh` runs everything end to end:
  - it starts this server with the real verifier
  - the game's own client joins, drives a stage, uploads it, and tries five forgeries
