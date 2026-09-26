//! S3 online scoreboard.
//!
//! Players who opt in register the random ID their game already made, plus a
//! name, and get back a secret token (the server keeps only its SHA-256). A run
//! starts with a single-use ticket that records when it began; a finished run
//! comes back with its replay, which the game's own code replays
//! (`s3 --verify-run`) before the score can go on a board. The same players can
//! report plays and rate games, which feed the dashboard and the public stats.

pub mod admin;
pub mod db;

use std::collections::HashMap;
use std::net::SocketAddr;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use axum::extract::{ConnectInfo, DefaultBodyLimit, Path, Request, State};
use axum::http::{HeaderMap, StatusCode};
use axum::middleware::{self, Next};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post, put};
use axum::{Extension, Json, Router};
use base64::Engine;
use rand::RngCore;
use rusqlite::{params, Connection, OptionalExtension};
use serde::Deserialize;
use serde_json::json;
use sha2::{Digest, Sha256};
use tokio::sync::Semaphore;

/// How a game is scored and checked, from the games file.
#[derive(Clone, Debug)]
pub struct GameInfo {
    pub slug: String,
    pub title: String,
    pub lower_better: bool,  // times: lower wins
    pub verified: bool,      // runs are replayed by the verifier; otherwise they wait for review
}

#[derive(Clone, Debug)]
pub struct Config {
    pub db_path: String,
    pub admin_token: Option<String>,
    pub verifier: Option<PathBuf>,
    pub games: Vec<GameInfo>,
    pub trust_proxy: bool,  // take the client address from X-Forwarded-For (behind a TLS proxy only)
    pub require_signed: bool,       // refuse bearer tokens: every signed-in call must be a signed request
    pub cors_origins: Vec<String>,  // web pages allowed to call the API (the browser build)
    pub github: Option<GitHub>,     // feedback also opens an issue here
}

#[derive(Clone, Debug)]
pub struct GitHub {
    pub repo: String,   // owner/name
    pub token: String,  // fine-grained, issues: write on that repo only
}

/// Games file: one per line, `slug time|points verify|review Title words`.
pub fn parse_games(text: &str) -> Result<Vec<GameInfo>, String> {
    let mut out = Vec::new();
    for (n, line) in text.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut w = line.split_whitespace();
        let (Some(slug), Some(kind), Some(check)) = (w.next(), w.next(), w.next()) else {
            return Err(format!("games line {}: need slug, kind, check, title", n + 1));
        };
        if !valid_slug(slug) || !matches!(kind, "time" | "points") || !matches!(check, "verify" | "review") {
            return Err(format!("games line {}: bad entry", n + 1));
        }
        let title = w.collect::<Vec<_>>().join(" ");
        out.push(GameInfo { slug: slug.into(), title, lower_better: kind == "time", verified: check == "verify" });
    }
    Ok(out)
}

pub struct AppState {
    pub cfg: Config,
    pub db: Mutex<Connection>,
    limits: Mutex<HashMap<String, (i64, u32)>>,
    nonces: Mutex<HashMap<String, i64>>,
    verifying: Semaphore,
}

pub type Shared = Arc<AppState>;

pub fn now_ms() -> i64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_millis() as i64).unwrap_or(0)
}

pub fn state(cfg: Config) -> Result<Shared, String> {
    let db = db::open(&cfg.db_path).map_err(|e| format!("database: {e}"))?;
    Ok(Arc::new(AppState { cfg, db: Mutex::new(db), limits: Mutex::new(HashMap::new()), nonces: Mutex::new(HashMap::new()), verifying: Semaphore::new(2) }))
}

pub fn router(st: Shared) -> Router {
    let api = Router::new()
        .route("/v1/players", post(register))
        .route("/v1/players/me", axum::routing::delete(delete_me))
        .route("/v1/tickets", post(ticket))
        .route("/v1/runs", post(submit_run).layer(DefaultBodyLimit::max(768 * 1024)))
        .route("/v1/leaderboard/{game}/{stage}", get(leaderboard))
        .route("/v1/plays", post(play))
        .route("/v1/ratings/{game}", put(rate))
        .route("/v1/stats", get(stats))
        .route("/v1/feedback", post(feedback))
        .route("/healthz", get(|| async { "ok" }))
        .layer(DefaultBodyLimit::max(16 * 1024));
    // Outermost first: the client's address, then CORS, then the signature check.
    api.merge(admin::routes())
        .layer(middleware::from_fn_with_state(st.clone(), signed_requests))
        .layer(middleware::from_fn_with_state(st.clone(), cors))
        .layer(middleware::from_fn_with_state(st.clone(), client_addr))
        .with_state(st)
}

// ---------------------------------------------------------------- signed requests
//
// A signed-in game signs every call instead of sending its token:
//
//   X-S3-Auth: <player id>:<unix ms>:<nonce, 32 hex>:<signature, 64 hex>
//   signature = HMAC-SHA256(key = SHA-256(token),
//                           METHOD \n PATH \n ms \n nonce \n hex(SHA-256(body)))
//
// The token itself never travels again after sign-up, a copied request can't be
// sent twice (the nonce), and a stale one is refused. The server keeps
// SHA-256(token), which is exactly the key. Clock differences over 10 s are
// logged, not refused (the player's clock may just be wrong); past 15 min the
// request is refused, because that is how long nonces are remembered.

const VERIFIED: &str = "x-s3-verified-player";
const SKEW_LOG_MS: i64 = 10_000;
const SKEW_MAX_MS: i64 = 15 * 60_000;

pub fn hmac_sha256(key: &[u8], data: &[u8]) -> [u8; 32] {
    let mut k = [0u8; 64];
    if key.len() > 64 {
        k[..32].copy_from_slice(&Sha256::digest(key));
    } else {
        k[..key.len()].copy_from_slice(key);
    }
    let (mut ipad, mut opad) = ([0u8; 64], [0u8; 64]);
    for i in 0..64 {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    let inner = Sha256::new().chain_update(ipad).chain_update(data).finalize();
    Sha256::new().chain_update(opad).chain_update(inner).finalize().into()
}

/// What a request's signature covers.
pub fn signing_string(method: &str, path: &str, ms: i64, nonce: &str, body: &[u8]) -> String {
    format!("{method}\n{path}\n{ms}\n{nonce}\n{}", hex::encode(Sha256::digest(body)))
}

fn log_auth(st: &AppState, player: &str, kind: &str, detail: String) {
    let db = st.db.lock().unwrap();
    let _ = db.execute(
        "INSERT INTO auth_events (player, kind, detail, at) VALUES (?1, ?2, ?3, ?4)",
        params![player.chars().take(36).collect::<String>(), kind, detail, now_ms()],
    );
    let _ = db.execute("DELETE FROM auth_events WHERE at < ?1", params![now_ms() - 90 * 86_400_000]);
}

async fn signed_requests(State(st): State<Shared>, Extension(addr): Extension<ClientAddr>, req: Request, next: Next) -> Response {
    let (mut parts, body) = req.into_parts();
    parts.headers.remove(VERIFIED);  // only this layer may say who is signed in
    let Some(auth) = parts.headers.get("x-s3-auth").and_then(|v| v.to_str().ok()).map(str::to_owned) else {
        return next.run(Request::from_parts(parts, body)).await;
    };
    let deny = |code: StatusCode, msg: &'static str| ApiError(code, msg).into_response();
    let f: Vec<&str> = auth.split(':').collect();
    let (Some(&id), Some(ms), Some(&nonce), Some(&sig)) = (f.first(), f.get(1).and_then(|m| m.parse::<i64>().ok()), f.get(2), f.get(3)) else {
        return deny(StatusCode::UNAUTHORIZED, "bad signature header");
    };
    if f.len() != 4 || !valid_uuid(id) || nonce.len() != 32 || sig.len() != 64 || !nonce.bytes().chain(sig.bytes()).all(|b| b.is_ascii_hexdigit()) {
        return deny(StatusCode::UNAUTHORIZED, "bad signature header");
    }
    if !st.allow(format!("sig:{}", addr.0), 600, 60_000) {
        return deny(StatusCode::TOO_MANY_REQUESTS, "slow down");
    }
    let Ok(bytes) = axum::body::to_bytes(body, 800 * 1024).await else {
        return deny(StatusCode::PAYLOAD_TOO_LARGE, "too big");
    };
    let key: Option<Vec<u8>> = {
        let db = st.db.lock().unwrap();
        db.query_row("SELECT token_hash FROM players WHERE id = ?1", params![id], |r| r.get(0)).optional().ok().flatten()
    };
    let Some(key) = key else { return deny(StatusCode::UNAUTHORIZED, "unknown player") };
    let path = parts.uri.path_and_query().map(|p| p.as_str()).unwrap_or("/");
    let want = hmac_sha256(&key, signing_string(parts.method.as_str(), path, ms, nonce, &bytes).as_bytes());
    let given = hex::decode(sig).unwrap_or_default();
    if given.len() != 32 || !bool::from(subtle::ConstantTimeEq::ct_eq(&want[..], &given[..])) {
        log_auth(&st, id, "badsig", format!("{} {}", parts.method, path.chars().take(60).collect::<String>()));
        return deny(StatusCode::UNAUTHORIZED, "bad signature");
    }
    let now = now_ms();
    let skew = now - ms;
    if skew.abs() > SKEW_MAX_MS {
        log_auth(&st, id, "skew", format!("{} s, refused", skew / 1000));
        return deny(StatusCode::UNAUTHORIZED, "your clock is too far off");
    }
    {
        let mut seen = st.nonces.lock().unwrap();
        seen.retain(|_, at| now - *at < SKEW_MAX_MS * 2);
        if seen.insert(format!("{id}:{nonce}"), now).is_some() {
            drop(seen);
            log_auth(&st, id, "replay", format!("{} {}", parts.method, path.chars().take(60).collect::<String>()));
            return deny(StatusCode::CONFLICT, "this request was already used");
        }
    }
    if skew.abs() > SKEW_LOG_MS {
        log_auth(&st, id, "skew", format!("{:.1} s", skew as f64 / 1000.0));
    }
    parts.headers.insert(VERIFIED, id.parse().unwrap());
    next.run(Request::from_parts(parts, axum::body::Body::from(bytes))).await
}

// ---------------------------------------------------------------- the browser build

async fn cors(State(st): State<Shared>, req: Request, next: Next) -> Response {
    let origin = req.headers().get("origin").and_then(|v| v.to_str().ok()).map(str::to_owned);
    let allowed = origin.filter(|o| st.cfg.cors_origins.iter().any(|a| a == o));
    if req.method() == axum::http::Method::OPTIONS {
        let mut r = StatusCode::NO_CONTENT.into_response();
        if let Some(o) = &allowed {
            let h = r.headers_mut();
            h.insert("access-control-allow-origin", o.parse().unwrap());
            h.insert("access-control-allow-methods", "GET, POST, PUT, DELETE".parse().unwrap());
            h.insert("access-control-allow-headers", "content-type, x-s3-auth".parse().unwrap());
            h.insert("access-control-max-age", "600".parse().unwrap());
            h.insert("vary", "origin".parse().unwrap());
        }
        return r;
    }
    let mut r = next.run(req).await;
    if let Some(o) = allowed {
        r.headers_mut().insert("access-control-allow-origin", o.parse().unwrap());
        r.headers_mut().insert("vary", "origin".parse().unwrap());
    }
    r
}

// ---------------------------------------------------------------- plumbing

/// The client's address, used only in memory for rate limits. Never stored.
#[derive(Clone)]
pub struct ClientAddr(pub String);

async fn client_addr(State(st): State<Shared>, mut req: Request, next: Next) -> Response {
    let mut addr = req.extensions().get::<ConnectInfo<SocketAddr>>().map(|c| c.0.ip().to_string()).unwrap_or_else(|| "local".into());
    if st.cfg.trust_proxy {
        if let Some(f) = req.headers().get("x-forwarded-for").and_then(|v| v.to_str().ok()) {
            if let Some(first) = f.split(',').next() {
                addr = first.trim().chars().take(64).collect();
            }
        }
    }
    req.extensions_mut().insert(ClientAddr(addr));
    next.run(req).await
}

pub struct ApiError(StatusCode, &'static str);
impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        (self.0, Json(json!({ "error": self.1 }))).into_response()
    }
}
type ApiResult<T> = Result<T, ApiError>;

fn internal<E: std::fmt::Display>(e: E) -> ApiError {
    eprintln!("s3-scores: {e}");
    ApiError(StatusCode::INTERNAL_SERVER_ERROR, "server error")
}

impl AppState {
    /// A fixed-window limit: at most `max` calls per `window_ms` for this key.
    fn allow(&self, key: String, max: u32, window_ms: i64) -> bool {
        let now = now_ms();
        let mut l = self.limits.lock().unwrap();
        if l.len() > 100_000 {
            l.retain(|_, (start, _)| now - *start < 3_600_000);
        }
        let e = l.entry(key).or_insert((now, 0));
        if now - e.0 >= window_ms {
            *e = (now, 0);
        }
        e.1 += 1;
        e.1 <= max
    }

    fn game(&self, slug: &str) -> Option<&GameInfo> {
        self.cfg.games.iter().find(|g| g.slug == slug)
    }
}

pub fn valid_slug(s: &str) -> bool {
    !s.is_empty() && s.len() <= 24 && s.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit())
}

/// A UUID v4 in the form the games write: lowercase, with dashes.
pub fn valid_uuid(s: &str) -> bool {
    let b = s.as_bytes();
    b.len() == 36
        && b.iter().enumerate().all(|(i, &c)| if [8, 13, 18, 23].contains(&i) { c == b'-' } else { c.is_ascii_digit() || (b'a'..=b'f').contains(&c) })
        && b[14] == b'4'
        && matches!(b[19], b'8' | b'9' | b'a' | b'b')
}

/// The games' name rules: 1-12 of A-Z, 0-9, space, - . !
pub fn valid_name(s: &str) -> bool {
    let t = s.trim();
    !t.is_empty() && s.len() <= 12 && s.bytes().all(|b| b.is_ascii_uppercase() || b.is_ascii_digit() || b" -.!".contains(&b))
}

fn token_hash(token: &str) -> Vec<u8> {
    Sha256::digest(token.as_bytes()).to_vec()
}

/// The player behind a signed request (checked by `signed_requests`), or a bearer token; else 401.
fn authed(st: &AppState, headers: &HeaderMap) -> ApiResult<db::Player> {
    if let Some(id) = headers.get(VERIFIED).and_then(|v| v.to_str().ok()) {
        let db = st.db.lock().unwrap();
        let now = now_ms();
        let p = db
            .query_row("SELECT id, name FROM players WHERE id = ?1", params![id], |r| Ok(db::Player { id: r.get(0)?, name: r.get(1)? }))
            .optional()
            .map_err(internal)?
            .ok_or(ApiError(StatusCode::UNAUTHORIZED, "unknown player"))?;
        db.execute("UPDATE players SET last_seen = ?1 WHERE id = ?2", params![now, p.id]).map_err(internal)?;
        drop(db);
        if !st.allow(format!("p:{}", p.id), 240, 60_000) {
            return Err(ApiError(StatusCode::TOO_MANY_REQUESTS, "slow down"));
        }
        return Ok(p);
    }
    if st.cfg.require_signed {
        return Err(ApiError(StatusCode::UNAUTHORIZED, "sign your requests"));
    }
    let token = headers
        .get("authorization")
        .and_then(|v| v.to_str().ok())
        .and_then(|v| v.strip_prefix("Bearer "))
        .filter(|t| t.len() == 64 && t.bytes().all(|b| b.is_ascii_hexdigit()))
        .ok_or(ApiError(StatusCode::UNAUTHORIZED, "sign in"))?;
    let db = st.db.lock().unwrap();
    let p = db::player_by_token(&db, &token_hash(token), now_ms()).map_err(internal)?;
    let p = p.ok_or(ApiError(StatusCode::UNAUTHORIZED, "unknown player"))?;
    drop(db);
    if !st.allow(format!("p:{}", p.id), 240, 60_000) {
        return Err(ApiError(StatusCode::TOO_MANY_REQUESTS, "slow down"));
    }
    Ok(p)
}

// ---------------------------------------------------------------- players

#[derive(Deserialize)]
struct RegisterBody {
    id: String,
    name: String,
}

async fn register(State(st): State<Shared>, Extension(addr): Extension<ClientAddr>, Json(b): Json<RegisterBody>) -> ApiResult<impl IntoResponse> {
    if !st.allow(format!("reg:{}", addr.0), 10, 3_600_000) {
        return Err(ApiError(StatusCode::TOO_MANY_REQUESTS, "too many sign-ups from here"));
    }
    if !valid_uuid(&b.id) {
        return Err(ApiError(StatusCode::BAD_REQUEST, "bad player id"));
    }
    if !valid_name(&b.name) {
        return Err(ApiError(StatusCode::BAD_REQUEST, "bad name"));
    }
    let mut raw = [0u8; 32];
    rand::rngs::OsRng.fill_bytes(&mut raw);
    let token = hex::encode(raw);
    let now = now_ms();
    let db = st.db.lock().unwrap();
    let taken: bool = db.query_row("SELECT COUNT(*) FROM players WHERE id = ?1", params![b.id], |r| r.get::<_, i64>(0)).map_err(internal)? > 0;
    if taken {
        return Err(ApiError(StatusCode::CONFLICT, "that id is already registered"));
    }
    db.execute(
        "INSERT INTO players (id, name, token_hash, created, last_seen) VALUES (?1, ?2, ?3, ?4, ?4)",
        params![b.id, b.name.trim(), token_hash(&token), now],
    )
    .map_err(internal)?;
    Ok((StatusCode::CREATED, Json(json!({ "token": token, "short_id": b.id[..8].to_uppercase() }))))
}

async fn delete_me(State(st): State<Shared>, headers: HeaderMap) -> ApiResult<impl IntoResponse> {
    let p = authed(&st, &headers)?;
    let db = st.db.lock().unwrap();
    db.execute("DELETE FROM players WHERE id = ?1", params![p.id]).map_err(internal)?;
    Ok(Json(json!({ "deleted": true })))
}

// ---------------------------------------------------------------- runs

#[derive(Deserialize)]
struct TicketBody {
    game: String,
    stage: i64,
}

/// A run starts here: a single-use ticket stamped with the server's clock.
async fn ticket(State(st): State<Shared>, headers: HeaderMap, Json(b): Json<TicketBody>) -> ApiResult<impl IntoResponse> {
    let p = authed(&st, &headers)?;
    if st.game(&b.game).is_none() {
        return Err(ApiError(StatusCode::NOT_FOUND, "unknown game"));
    }
    if !(0..1000).contains(&b.stage) {
        return Err(ApiError(StatusCode::BAD_REQUEST, "bad stage"));
    }
    let mut raw = [0u8; 16];
    rand::rngs::OsRng.fill_bytes(&mut raw);
    let nonce = hex::encode(raw);
    let now = now_ms();
    let db = st.db.lock().unwrap();
    // Tickets nobody used within a day are just noise.
    db.execute("DELETE FROM tickets WHERE issued < ?1", params![now - 86_400_000]).map_err(internal)?;
    db.execute(
        "INSERT INTO tickets (nonce, player, game, stage, issued) VALUES (?1, ?2, ?3, ?4, ?5)",
        params![nonce, p.id, b.game, b.stage, now],
    )
    .map_err(internal)?;
    Ok(Json(json!({ "ticket": nonce })))
}

#[derive(Deserialize)]
struct RunBody {
    ticket: Option<String>,
    game: Option<String>,   // with no ticket: which game and stage this was
    stage: Option<i64>,
    score: f64,
    build: Option<String>,
    replay: Option<String>,  // base64
}

pub const TICKET_LIFE_MS: i64 = 2 * 3_600_000;

async fn submit_run(State(st): State<Shared>, headers: HeaderMap, Json(b): Json<RunBody>) -> ApiResult<impl IntoResponse> {
    let p = authed(&st, &headers)?;
    if !b.score.is_finite() || b.score < 0.0 || b.score > 1e9 {
        return Err(ApiError(StatusCode::BAD_REQUEST, "bad score"));
    }
    let now = now_ms();
    // Spend the ticket: it must be this player's, unused and fresh. One statement, so two submissions can't both win.
    // A run with no ticket (the player joined after driving it) can still be checked, but only a person can accept it.
    let ticketless = b.ticket.is_none();
    let (game, stage, issued) = if let Some(ticket) = &b.ticket {
        let db = st.db.lock().unwrap();
        let t: Option<(String, i64, i64)> = db
            .query_row(
                "UPDATE tickets SET used = 1 WHERE nonce = ?1 AND player = ?2 AND used = 0 AND issued >= ?3 RETURNING game, stage, issued",
                params![ticket, p.id, now - TICKET_LIFE_MS],
                |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)),
            )
            .optional()
            .map_err(internal)?;
        t.ok_or(ApiError(StatusCode::CONFLICT, "ticket already used, expired or not yours"))?
    } else {
        let game = b.game.clone().filter(|g| st.game(g).is_some()).ok_or(ApiError(StatusCode::NOT_FOUND, "unknown game"))?;
        let stage = b.stage.filter(|s| (0..1000).contains(s)).ok_or(ApiError(StatusCode::BAD_REQUEST, "bad stage"))?;
        if !st.allow(format!("noticket:{}", p.id), 10, 3_600_000) {
            return Err(ApiError(StatusCode::TOO_MANY_REQUESTS, "slow down"));
        }
        (game, stage, now)
    };
    let info = st.game(&game).cloned().ok_or(ApiError(StatusCode::NOT_FOUND, "unknown game"))?;
    let replay = match &b.replay {
        Some(r) if r.len() <= 700 * 1024 => {
            Some(base64::engine::general_purpose::STANDARD.decode(r).map_err(|_| ApiError(StatusCode::BAD_REQUEST, "bad replay"))?)
        }
        Some(_) => return Err(ApiError(StatusCode::PAYLOAD_TOO_LARGE, "replay too big")),
        None => None,
    };

    // Decide what this run is worth.
    let (mut status, mut reason) = ("review", String::from("waiting for a person"));
    if info.verified {
        match (&replay, &st.cfg.verifier) {
            (None, _) => (status, reason) = ("rejected", "no replay".into()),
            (Some(_), None) => reason = "no verifier on this server".into(),
            (Some(bytes), Some(bin)) => {
                let v = verify(&st, bin, bytes).await;
                (status, reason) = match v {
                    Ok(v) if v.game != game || v.stage != stage => ("rejected", "replay is for another game or stage".into()),
                    Ok(v) if (v.claimed - b.score).abs() > 0.001 => ("rejected", "score does not match the replay".into()),
                    Ok(v) => (v.verdict, v.reason),
                    Err(e) => ("review", format!("verifier: {e}")),
                };
            }
        }
    }
    if status == "accepted" && ticketless {
        (status, reason) = ("review", format!("{reason}; no start ticket"));
    }
    // Timed runs can't be handed in before they could have ended.
    if status == "accepted" && !ticketless && info.lower_better && (b.score * 1000.0) as i64 > now - issued + 1000 {
        (status, reason) = ("review", "handed in sooner than the run could have ended".into());
    }
    let build: String = b.build.unwrap_or_default().chars().filter(|c| c.is_ascii_graphic() || *c == ' ').take(40).collect();
    let db = st.db.lock().unwrap();
    db.execute(
        "INSERT INTO runs (player, game, stage, score, played_at, submitted, build, status, reason, replay)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
        params![p.id, game, stage, b.score, issued, now, build, status, reason, replay],
    )
    .map_err(internal)?;
    let mut resp = json!({ "status": status, "reason": reason });
    if status == "accepted" {
        let board = db::leaderboard(&db, &game, stage, info.lower_better).map_err(internal)?;
        if let Some(i) = board.iter().position(|(id, _, _)| *id == p.id) {
            resp["rank"] = json!(i + 1);
            resp["of"] = json!(board.len());
        }
    }
    Ok(Json(resp))
}

pub struct Verified {
    pub verdict: &'static str,
    pub reason: String,
    pub game: String,
    pub stage: i64,
    pub claimed: f64,
}

/// Replay a run with the game's own code. The verdict comes from the exit code;
/// the printed line only supplies what the replay says about itself.
async fn verify(st: &AppState, bin: &PathBuf, bytes: &[u8]) -> Result<Verified, String> {
    let _slot = st.verifying.acquire().await.map_err(|e| e.to_string())?;
    let mut raw = [0u8; 12];
    rand::rngs::OsRng.fill_bytes(&mut raw);
    let path = std::env::temp_dir().join(format!("s3-replay-{}.bin", hex::encode(raw)));
    tokio::fs::write(&path, bytes).await.map_err(|e| e.to_string())?;
    let run = tokio::process::Command::new(bin).arg("--verify-run").arg(&path).kill_on_drop(true).output();
    let out = tokio::time::timeout(Duration::from_secs(30), run).await;
    let _ = tokio::fs::remove_file(&path).await;
    let out = out.map_err(|_| "took too long".to_string())?.map_err(|e| e.to_string())?;
    let verdict = match out.status.code() {
        Some(0) => "accepted",
        Some(1) => "review",
        Some(2) => "rejected",
        _ => return Err("crashed".into()),
    };
    let line: serde_json::Value = serde_json::from_slice(out.stdout.split(|&c| c == b'\n').next().unwrap_or(&[])).map_err(|_| "unreadable".to_string())?;
    Ok(Verified {
        verdict,
        reason: line["reason"].as_str().unwrap_or("").chars().take(120).collect(),
        game: line["game"].as_str().unwrap_or("").into(),
        stage: line["stage"].as_i64().unwrap_or(-1),
        claimed: line["claimed"].as_f64().unwrap_or(-1.0),
    })
}

// ---------------------------------------------------------------- boards and stats

async fn leaderboard(State(st): State<Shared>, headers: HeaderMap, Path((game, stage)): Path<(String, i64)>) -> ApiResult<impl IntoResponse> {
    let info = st.game(&game).cloned().ok_or(ApiError(StatusCode::NOT_FOUND, "unknown game"))?;
    let me = if headers.contains_key("authorization") { Some(authed(&st, &headers)?.id) } else { None };
    let db = st.db.lock().unwrap();
    let board = db::leaderboard(&db, &game, stage, info.lower_better).map_err(internal)?;
    let entry = |i: usize| {
        let (id, name, score) = &board[i];
        db::Entry { rank: i as i64 + 1, name: name.clone(), short_id: id[..8].to_uppercase(), score: *score, you: Some(id) == me.as_ref() }
    };
    let top: Vec<_> = (0..board.len().min(10)).map(entry).collect();
    let you = me.as_ref().and_then(|m| board.iter().position(|(id, _, _)| id == m)).map(entry);
    Ok(Json(json!({ "game": game, "stage": stage, "total": board.len(), "lower_better": info.lower_better, "top": top, "you": you })))
}

#[derive(Deserialize)]
struct PlayBody {
    game: String,
    event: String,
    seconds: Option<f64>,
}

async fn play(State(st): State<Shared>, headers: HeaderMap, Json(b): Json<PlayBody>) -> ApiResult<impl IntoResponse> {
    let p = authed(&st, &headers)?;
    if st.game(&b.game).is_none() {
        return Err(ApiError(StatusCode::NOT_FOUND, "unknown game"));
    }
    if !matches!(b.event.as_str(), "start" | "finish") {
        return Err(ApiError(StatusCode::BAD_REQUEST, "bad event"));
    }
    if !st.allow(format!("play:{}", p.id), 60, 3_600_000) {
        return Err(ApiError(StatusCode::TOO_MANY_REQUESTS, "slow down"));
    }
    let secs = b.seconds.filter(|s| s.is_finite()).unwrap_or(0.0).clamp(0.0, 6.0 * 3600.0);
    let db = st.db.lock().unwrap();
    db.execute("INSERT INTO plays (player, game, event, seconds, at) VALUES (?1, ?2, ?3, ?4, ?5)", params![p.id, b.game, b.event, secs, now_ms()])
        .map_err(internal)?;
    Ok(StatusCode::NO_CONTENT)
}

#[derive(Deserialize)]
struct RateBody {
    thumb: i64,
}

async fn rate(State(st): State<Shared>, headers: HeaderMap, Path(game): Path<String>, Json(b): Json<RateBody>) -> ApiResult<impl IntoResponse> {
    let p = authed(&st, &headers)?;
    if st.game(&game).is_none() {
        return Err(ApiError(StatusCode::NOT_FOUND, "unknown game"));
    }
    if b.thumb != 1 && b.thumb != -1 {
        return Err(ApiError(StatusCode::BAD_REQUEST, "thumb is 1 or -1"));
    }
    let db = st.db.lock().unwrap();
    db.execute(
        "INSERT INTO ratings (player, game, thumb, at) VALUES (?1, ?2, ?3, ?4)
         ON CONFLICT (player, game) DO UPDATE SET thumb = excluded.thumb, at = excluded.at",
        params![p.id, game, b.thumb, now_ms()],
    )
    .map_err(internal)?;
    Ok(StatusCode::NO_CONTENT)
}

/// Public popularity, for the s3 launcher to download now and then.
async fn stats(State(st): State<Shared>) -> ApiResult<impl IntoResponse> {
    let slugs: Vec<String> = st.cfg.games.iter().map(|g| g.slug.clone()).collect();
    let db = st.db.lock().unwrap();
    let s = db::game_stats(&db, &slugs, now_ms()).map_err(internal)?;
    let games: Vec<_> = s
        .into_iter()
        .zip(&st.cfg.games)
        .map(|(s, g)| {
            json!({ "game": s.game, "title": g.title, "plays_7d": s.plays_7d, "plays_30d": s.plays_30d, "players_30d": s.players_30d,
                    "up": s.up, "down": s.down })
        })
        .collect();
    Ok(([("cache-control", "public, max-age=600")], Json(json!({ "generated": now_ms(), "games": games }))))
}

// ---------------------------------------------------------------- feedback

#[derive(Deserialize)]
struct FeedbackBody {
    game: String,
    text: String,
    build: Option<String>,
}

/// Anyone can send feedback (signed-in players get their name on it). It is kept
/// for the dashboard and, when the server has a GitHub token, opened as an issue.
async fn feedback(
    State(st): State<Shared>,
    Extension(addr): Extension<ClientAddr>,
    headers: HeaderMap,
    Json(b): Json<FeedbackBody>,
) -> ApiResult<impl IntoResponse> {
    let player = if headers.contains_key(VERIFIED) || headers.contains_key("authorization") { Some(authed(&st, &headers)?) } else { None };
    let who = player.as_ref().map(|p| p.id.clone()).unwrap_or_else(|| addr.0.clone());
    if !st.allow(format!("fb:{who}"), 5, 3_600_000) {
        return Err(ApiError(StatusCode::TOO_MANY_REQUESTS, "thanks, that's enough for now"));
    }
    if !(b.game == "s3" || st.game(&b.game).is_some()) {
        return Err(ApiError(StatusCode::NOT_FOUND, "unknown game"));
    }
    let text: String = b.text.trim().chars().map(|c| if c.is_control() && c != '\n' { ' ' } else { c }).take(2000).collect();
    if text.chars().filter(|c| !c.is_whitespace()).count() < 3 {
        return Err(ApiError(StatusCode::BAD_REQUEST, "say a little more"));
    }
    let build: String = b.build.unwrap_or_default().chars().filter(|c| c.is_ascii_graphic() || *c == ' ').take(40).collect();
    let id = {
        let db = st.db.lock().unwrap();
        db.execute(
            "INSERT INTO feedback (player, game, build, text, at) VALUES (?1, ?2, ?3, ?4, ?5)",
            params![player.as_ref().map(|p| &p.id), b.game, build, text, now_ms()],
        )
        .map_err(internal)?;
        db.last_insert_rowid()
    };
    if let Some(gh) = st.cfg.github.clone() {
        let st2 = st.clone();
        let from = player.map(|p| format!("{} ({})", p.name, &p.id[..8].to_uppercase())).unwrap_or_else(|| "anonymous".into());
        tokio::spawn(async move {
            match open_issue(&gh, id, &b.game, &build, &from, &text).await {
                Ok(url) => {
                    let db = st2.db.lock().unwrap();
                    let _ = db.execute("UPDATE feedback SET issue_url = ?1 WHERE id = ?2", params![url, id]);
                }
                Err(e) => eprintln!("s3-scores: feedback #{id} not sent to GitHub: {e}"),
            }
        });
    }
    Ok((StatusCode::CREATED, Json(json!({ "received": true }))))
}

/// The issue: the player's words in a code block (so nothing in them is read as
/// markdown, links or @mentions), with the game, the build and who sent it.
pub fn issue_json(id: i64, game: &str, build: &str, from: &str, text: &str) -> serde_json::Value {
    let first: String = text.lines().next().unwrap_or("").chars().filter(|c| *c != '@').take(60).collect();
    let fenced = text.replace("````", "'''");
    json!({
        "title": format!("Feedback ({game}): {first}"),
        "body": format!("**Game:** `{game}` · **Build:** `{build}` · **From:** {from}\n\n````text\n{fenced}\n````\n\n_Sent from the S3 feedback screen (feedback #{id})._"),
        "labels": ["feedback"],
    })
}

async fn open_issue(gh: &GitHub, id: i64, game: &str, build: &str, from: &str, text: &str) -> Result<String, String> {
    let ok = |s: &str| !s.is_empty() && s.bytes().all(|b| b.is_ascii_alphanumeric() || b"-_.".contains(&b));
    let Some((owner, name)) = gh.repo.split_once('/').filter(|(o, n)| ok(o) && ok(n)) else { return Err("bad repo".into()) };
    let body = issue_json(id, game, build, from, text).to_string();
    let mut raw = [0u8; 12];
    rand::rngs::OsRng.fill_bytes(&mut raw);
    let path = std::env::temp_dir().join(format!("s3-issue-{}.json", hex::encode(raw)));
    tokio::fs::write(&path, body).await.map_err(|e| e.to_string())?;
    // The token goes in on stdin, never on the command line.
    let mut child = tokio::process::Command::new("curl")
        .args(["-sS", "--max-time", "20", "--proto", "=https", "-X", "POST", "-H", "@-", "--data-binary"])
        .arg(format!("@{}", path.display()))
        .arg(format!("https://api.github.com/repos/{owner}/{name}/issues"))
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .kill_on_drop(true)
        .spawn()
        .map_err(|e| e.to_string())?;
    if let Some(mut stdin) = child.stdin.take() {
        use tokio::io::AsyncWriteExt;
        let headers = format!(
            "Authorization: Bearer {}\nAccept: application/vnd.github+json\nX-GitHub-Api-Version: 2022-11-28\nUser-Agent: s3-scores\nContent-Type: application/json\n",
            gh.token
        );
        stdin.write_all(headers.as_bytes()).await.map_err(|e| e.to_string())?;
    }
    let out = tokio::time::timeout(Duration::from_secs(30), child.wait_with_output()).await;
    let _ = tokio::fs::remove_file(&path).await;
    let out = out.map_err(|_| "timed out".to_string())?.map_err(|e| e.to_string())?;
    let v: serde_json::Value = serde_json::from_slice(&out.stdout).map_err(|_| "unreadable reply".to_string())?;
    v["html_url"].as_str().map(str::to_owned).ok_or_else(|| v["message"].as_str().unwrap_or("no issue made").to_string())
}
