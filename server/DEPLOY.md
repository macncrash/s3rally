# Deploying s3-scores

These are notes for ops. The service is one small container on the same box and network as the other
services behind the shared Caddy. It is set up like the other containers there: no published port,
Caddy terminates TLS, and data lives on a host directory.

## 1. Build the image

On the box, from a checkout of the repository:

```sh
docker build -f server/Dockerfile --build-arg BUILD_ID=$(git rev-parse --short HEAD) -t s3-scores:latest .
```

The image holds:
- the server
- the game's own verifier: the `s3` binary, which replays uploaded runs and needs no display
- the games list

It runs as uid 1000.

## 2. Secrets

Put these in a root-only file, for example `/srv/s3-scores/s3-scores.env` (mode 600):

```sh
S3_ADMIN_TOKEN=<openssl rand -hex 24>          # password for https://<host>/admin, user "admin"
S3_CORS_ORIGINS=https://macncrash.github.io    # the browser build's page
# Optional: feedback also opens a GitHub issue.
# Use a fine-grained token with only "Issues: read and write" on that one repository.
S3_GITHUB_REPO=macncrash/s3rally
S3_GITHUB_TOKEN=github_pat_...
```

## 3. Run

```sh
docker run -d --name s3-scores --restart unless-stopped \
  --network <shared-network> \
  --env-file /srv/s3-scores/s3-scores.env \
  -v /srv/s3-scores/data:/app/data \
  --read-only --tmpfs /tmp:size=64m \
  --cap-drop ALL --security-opt no-new-privileges \
  --memory 512m --pids-limit 256 \
  s3-scores:latest
```

- **No port is published:** Caddy reaches the container at `s3-scores:8790` on the shared network.
- **Writable paths:** only `/app/data` (the SQLite database) and `/tmp`. `/tmp` holds replays for the few milliseconds while they are verified.
- **The data directory must be writable by uid 1000:**

  ```sh
  mkdir -p /srv/s3-scores/data && chown 1000:1000 /srv/s3-scores/data
  ```

## 4. Caddy

Add a site block **before** the `:443` catch-all in the live Caddyfile. Two rules:
- **The explicit `tls` line is required.** Without it, Caddy serves the catch-all's certificate.
- **Only list a hostname once its DNS resolves.**

```caddy
scores.example.com {
    tls ops@example.com {
        protocols tls1.2 tls1.3
    }
    encode gzip
    request_body {
        max_size 1MB
    }
    reverse_proxy s3-scores:8790 {
        header_up X-Forwarded-For {remote_host}
    }
}
```

Validate the Caddyfile in a throwaway `caddy:2` container, back up the live one, then restart the Caddy container.

## 5. Check it

```sh
docker run --rm --network <shared-network> curlimages/curl -fs http://s3-scores:8790/healthz   # ok
curl -fs https://scores.example.com/v1/stats                                                     # {"games":[...]}
```

The dashboard is at `https://scores.example.com/admin`.

## 6. Point the games at it

| Build | How |
|---|---|
| Desktop | `score_url=https://scores.example.com` in the console settings, or `S3_SCORE_URL` |
| Browser | set `window.S3_SCORE_URL` in `web/shell.html`, then publish |

## Updating

Rebuild the image, then `docker rm -f s3-scores` and run it again. The database stays in `/srv/s3-scores/data`.

Back up that directory; SQLite in WAL mode is safe to copy with `sqlite3 scores.db ".backup copy.db"`.

When a game's physics changes, the verifier inside the image must change with it, or new runs will go to review.
