//! The scoreboard's database: one SQLite file.
//!
//! Stored per player: the random ID the game made, the name they chose (12
//! characters) and a SHA-256 of their token. Never an email or an IP address.

use rusqlite::{params, Connection, OptionalExtension};

pub const SCHEMA: &str = "
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS players (
    id          TEXT PRIMARY KEY,          -- the game's UUID v4
    name        TEXT NOT NULL,
    token_hash  BLOB NOT NULL UNIQUE,      -- SHA-256 of the secret token
    created     INTEGER NOT NULL,          -- unix ms
    last_seen   INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS tickets (
    nonce       TEXT PRIMARY KEY,
    player      TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    game        TEXT NOT NULL,
    stage       INTEGER NOT NULL,
    issued      INTEGER NOT NULL,
    used        INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS runs (
    id          INTEGER PRIMARY KEY,
    player      TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    game        TEXT NOT NULL,
    stage       INTEGER NOT NULL,
    score       REAL NOT NULL,             -- seconds for timed games, points otherwise
    played_at   INTEGER NOT NULL,          -- when the ticket was issued (the run started)
    submitted   INTEGER NOT NULL,
    build       TEXT NOT NULL,
    status      TEXT NOT NULL CHECK (status IN ('accepted', 'review', 'rejected')),
    reason      TEXT NOT NULL,
    replay      BLOB
);
CREATE INDEX IF NOT EXISTS runs_board ON runs (game, stage, status, score);
CREATE TABLE IF NOT EXISTS plays (
    id          INTEGER PRIMARY KEY,
    player      TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    game        TEXT NOT NULL,
    event       TEXT NOT NULL CHECK (event IN ('start', 'finish')),
    seconds     REAL NOT NULL DEFAULT 0,   -- how long the session was (finish events)
    at          INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS plays_game ON plays (game, at);
CREATE TABLE IF NOT EXISTS ratings (
    player      TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    game        TEXT NOT NULL,
    thumb       INTEGER NOT NULL CHECK (thumb IN (-1, 1)),
    at          INTEGER NOT NULL,
    PRIMARY KEY (player, game)
);
";

pub fn open(path: &str) -> rusqlite::Result<Connection> {
    let db = Connection::open(path)?;
    db.execute_batch(SCHEMA)?;
    Ok(db)
}

pub struct Player {
    pub id: String,
    pub name: String,
}

pub fn player_by_token(db: &Connection, token_hash: &[u8], now: i64) -> rusqlite::Result<Option<Player>> {
    let p = db
        .query_row("SELECT id, name FROM players WHERE token_hash = ?1", params![token_hash], |r| {
            Ok(Player { id: r.get(0)?, name: r.get(1)? })
        })
        .optional()?;
    if let Some(p) = &p {
        db.execute("UPDATE players SET last_seen = ?1 WHERE id = ?2", params![now, p.id])?;
    }
    Ok(p)
}

/// One row of a leaderboard: a player's best accepted score.
#[derive(serde::Serialize, Clone, Debug)]
pub struct Entry {
    pub rank: i64,
    pub name: String,
    pub short_id: String,
    pub score: f64,
    pub you: bool,
}

/// Best accepted score per player, best first. `lower_better` for times.
pub fn leaderboard(db: &Connection, game: &str, stage: i64, lower_better: bool) -> rusqlite::Result<Vec<(String, String, f64)>> {
    let sql = format!(
        "SELECT p.id, p.name, {agg}(r.score) AS best FROM runs r JOIN players p ON p.id = r.player
         WHERE r.game = ?1 AND r.stage = ?2 AND r.status = 'accepted'
         GROUP BY p.id ORDER BY best {dir}, MIN(r.submitted) ASC",
        agg = if lower_better { "MIN" } else { "MAX" },
        dir = if lower_better { "ASC" } else { "DESC" }
    );
    let mut st = db.prepare(&sql)?;
    let rows = st.query_map(params![game, stage], |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)))?;
    rows.collect()
}

/// Per-game popularity for the dashboard and the launcher.
#[derive(serde::Serialize, Clone, Debug, Default)]
pub struct GameStats {
    pub game: String,
    pub plays_7d: i64,
    pub plays_30d: i64,
    pub players_30d: i64,
    pub finishes_30d: i64,
    pub minutes_30d: f64,
    pub up: i64,
    pub down: i64,
    pub runs_accepted: i64,
    pub last_played: Option<i64>,
}

pub fn game_stats(db: &Connection, games: &[String], now: i64) -> rusqlite::Result<Vec<GameStats>> {
    const DAY: i64 = 86_400_000;
    let mut out = Vec::new();
    for g in games {
        let mut s = GameStats { game: g.clone(), ..Default::default() };
        s.plays_7d = db.query_row(
            "SELECT COUNT(*) FROM plays WHERE game = ?1 AND event = 'start' AND at >= ?2",
            params![g, now - 7 * DAY],
            |r| r.get(0),
        )?;
        (s.plays_30d, s.players_30d) = db.query_row(
            "SELECT COUNT(*), COUNT(DISTINCT player) FROM plays WHERE game = ?1 AND event = 'start' AND at >= ?2",
            params![g, now - 30 * DAY],
            |r| Ok((r.get(0)?, r.get(1)?)),
        )?;
        (s.finishes_30d, s.minutes_30d) = db.query_row(
            "SELECT COUNT(*), COALESCE(SUM(seconds), 0) / 60.0 FROM plays WHERE game = ?1 AND event = 'finish' AND at >= ?2",
            params![g, now - 30 * DAY],
            |r| Ok((r.get(0)?, r.get(1)?)),
        )?;
        (s.up, s.down) = db.query_row(
            "SELECT COALESCE(SUM(thumb = 1), 0), COALESCE(SUM(thumb = -1), 0) FROM ratings WHERE game = ?1",
            params![g],
            |r| Ok((r.get(0)?, r.get(1)?)),
        )?;
        s.runs_accepted =
            db.query_row("SELECT COUNT(*) FROM runs WHERE game = ?1 AND status = 'accepted'", params![g], |r| r.get(0))?;
        s.last_played = db.query_row("SELECT MAX(at) FROM plays WHERE game = ?1", params![g], |r| r.get(0))?;
        out.push(s);
    }
    Ok(out)
}
