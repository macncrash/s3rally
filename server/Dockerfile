# s3-scores: the S3 online scoreboard, with the game's own verifier inside.
#
# Build from the repository root (the verifier is the game itself):
#   docker build -f server/Dockerfile --build-arg BUILD_ID=$(git rev-parse --short HEAD) -t s3-scores:latest .
# Run: see server/DEPLOY.md.

# ---- the server
FROM rust:1-bookworm AS server
WORKDIR /src/server
COPY server/Cargo.toml server/Cargo.lock ./
COPY server/src ./src
RUN cargo build --release --locked

# ---- the verifier: the s3 binary, which replays runs (`s3 --verify-run`). No display needed.
FROM debian:bookworm-slim AS verifier
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make libsdl2-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Makefile ./
COPY src ./src
ARG BUILD_ID=docker
RUN make -j"$(nproc)" s3 BUILD_ID="$BUILD_ID" && ./s3 --version

# ---- what runs
FROM debian:bookworm-slim AS runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends libsdl2-2.0-0 curl ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --uid 1000 --create-home --shell /usr/sbin/nologin scores \
 && mkdir -p /app/data && chown scores:scores /app/data
COPY --from=server /src/server/target/release/s3-scores /app/s3-scores
COPY --from=verifier /src/s3 /app/s3
COPY server/games.txt /app/games.txt
ENV S3_SCORES_DB=/app/data/scores.db \
    S3_SCORES_GAMES=/app/games.txt \
    S3_SCORES_ADDR=0.0.0.0:8790 \
    S3_VERIFIER=/app/s3 \
    S3_TRUST_PROXY=1 \
    S3_REQUIRE_SIGNED=1
USER scores
WORKDIR /app
EXPOSE 8790
VOLUME ["/app/data"]
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s CMD curl -fs http://127.0.0.1:8790/healthz || exit 1
CMD ["/app/s3-scores"]
