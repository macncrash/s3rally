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
    verifying: Semaphore,
}

pub type Shared = Arc<AppState>;

pub fn now_ms() -> i64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_millis() as i64).unwrap_or(0)
}

pub fn state(cfg: Config) -> Result<Shared, String> {
    let db = db::open(&cfg.db_path).map_err(|e| format!("database: {e}"))?;
    Ok(Arc::new(AppState { cfg, db: Mutex::new(db), limits: Mutex::new(HashMap::new()), verifying: Semaphore::new(2) }))
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
        .route("/healthz", get(|| async { "ok" }))
        .layer(DefaultBodyLimit::max(16 * 1024));
    api.merge(admin::routes()).layer(middleware::from_fn_with_state(st.clone(), client_addr)).with_state(st)
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

/// The player behind the bearer token, or 401.
fn authed(st: &AppState, headers: &HeaderMap) -> ApiResult<db::Player> {
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
