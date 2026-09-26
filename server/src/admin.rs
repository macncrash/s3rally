//! The dashboard: which games people play, how they rate them, which have gone
//! quiet, and the runs waiting for a person to look at them.
//!
//! Behind HTTP Basic auth (user `admin`, password = S3_ADMIN_TOKEN). With no
//! token set, the dashboard is off. Forms carry a token derived from the admin
//! token, so another site can't post them from a signed-in browser.

use axum::extract::{Form, Path, State};
use axum::http::{HeaderMap, StatusCode};
use axum::response::{Html, IntoResponse, Redirect, Response};
use axum::routing::{get, post};
use axum::Router;
use base64::Engine;
use rusqlite::params;
use serde::Deserialize;
use sha2::{Digest, Sha256};
use subtle::ConstantTimeEq;

use crate::{db, now_ms, Shared};

pub fn routes() -> Router<Shared> {
    Router::new().route("/admin", get(dashboard)).route("/admin/runs/{id}/{action}", post(decide))
}

fn check(st: &Shared, headers: &HeaderMap) -> Result<String, Response> {
    let deny = || {
        (StatusCode::UNAUTHORIZED, [("www-authenticate", "Basic realm=\"s3 scores\", charset=\"UTF-8\"")], "sign in").into_response()
    };
    let Some(token) = st.cfg.admin_token.as_deref() else {
        return Err((StatusCode::NOT_FOUND, "dashboard off: set S3_ADMIN_TOKEN").into_response());
    };
    let given = headers
        .get("authorization")
        .and_then(|v| v.to_str().ok())
        .and_then(|v| v.strip_prefix("Basic "))
        .and_then(|v| base64::engine::general_purpose::STANDARD.decode(v).ok())
        .and_then(|v| String::from_utf8(v).ok())
        .ok_or_else(deny)?;
    let expect = format!("admin:{token}");
    if given.len() != expect.len() || !bool::from(given.as_bytes().ct_eq(expect.as_bytes())) {
        return Err(deny());
    }
    Ok(hex::encode(Sha256::digest(format!("s3-admin-form:{token}"))))
}

fn esc(s: &str) -> String {
    s.chars()
        .map(|c| match c {
            '<' => "&lt;".into(),
            '>' => "&gt;".into(),
            '&' => "&amp;".into(),
            '"' => "&quot;".into(),
            '\'' => "&#39;".into(),
            c => c.to_string(),
        })
        .collect()
}

fn ago(ms: Option<i64>, now: i64) -> String {
    match ms {
        None => "never".into(),
        Some(t) => {
            let d = (now - t) / 86_400_000;
            if d == 0 { "today".into() } else if d == 1 { "yesterday".into() } else { format!("{d} days ago") }
        }
    }
}

async fn dashboard(State(st): State<Shared>, headers: HeaderMap) -> Response {
    let form = match check(&st, &headers) {
        Ok(f) => f,
        Err(r) => return r,
    };
    let now = now_ms();
    let slugs: Vec<String> = st.cfg.games.iter().map(|g| g.slug.clone()).collect();
    let db = st.db.lock().unwrap();
    let mut stats = match db::game_stats(&db, &slugs, now) {
        Ok(s) => s,
        Err(e) => return (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()).into_response(),
    };
    stats.sort_by(|a, b| b.plays_30d.cmp(&a.plays_30d).then(b.up.cmp(&a.up)));
    let players: i64 = db.query_row("SELECT COUNT(*) FROM players", [], |r| r.get(0)).unwrap_or(0);
    let active: i64 = db.query_row("SELECT COUNT(*) FROM players WHERE last_seen >= ?1", params![now - 30 * 86_400_000], |r| r.get(0)).unwrap_or(0);
    let max = stats.iter().map(|s| s.plays_30d).max().unwrap_or(0).max(1);

    let mut rows = String::new();
    for s in &stats {
        let title = st.cfg.games.iter().find(|g| g.slug == s.game).map(|g| g.title.as_str()).unwrap_or("");
        let votes = s.up + s.down;
        let liked = if votes > 0 { format!("{}%", s.up * 100 / votes) } else { "–".into() };
        let quiet = s.last_played.map_or(true, |t| now - t > 30 * 86_400_000);
        let tag = if quiet { "<span class=tag>quiet</span>" } else if votes >= 5 && s.up * 100 / votes < 50 { "<span class=tag warn>disliked</span>" } else { "" };
        rows += &format!(
            "<tr><td><b>{}</b><br><small>{}</small></td><td class=n>{}</td><td><div class=bar style=\"width:{}%\"></div>{}</td><td class=n>{}</td><td class=n>{}</td><td class=n>{:.0}</td><td class=n>{} up / {} down <small>{}</small></td><td class=n>{}</td><td>{} {}</td></tr>",
            esc(title), esc(&s.game), s.plays_7d, s.plays_30d * 100 / max, s.plays_30d, s.players_30d, s.finishes_30d, s.minutes_30d,
            s.up, s.down, liked, s.runs_accepted, ago(s.last_played, now), tag
        );
    }

    let mut review = String::new();
    if let Ok(mut q) = db.prepare(
        "SELECT r.id, p.name, substr(p.id, 1, 8), r.game, r.stage, r.score, r.reason, r.submitted
         FROM runs r JOIN players p ON p.id = r.player WHERE r.status = 'review' ORDER BY r.submitted DESC LIMIT 50",
    ) {
        let it = q.query_map([], |r| {
            Ok((r.get::<_, i64>(0)?, r.get::<_, String>(1)?, r.get::<_, String>(2)?, r.get::<_, String>(3)?, r.get::<_, i64>(4)?,
                r.get::<_, f64>(5)?, r.get::<_, String>(6)?, r.get::<_, i64>(7)?))
        });
        if let Ok(it) = it {
            for (id, name, short, game, stage, score, reason, at) in it.flatten() {
                review += &format!(
                    "<tr><td>{} <small>{}</small></td><td>{} / {}</td><td class=n>{:.2}</td><td>{}</td><td>{}</td><td>\
                     <form method=post action=\"/admin/runs/{id}/accept\"><input type=hidden name=form value=\"{form}\"><button>Accept</button></form> \
                     <form method=post action=\"/admin/runs/{id}/reject\"><input type=hidden name=form value=\"{form}\"><button>Reject</button></form></td></tr>",
                    esc(&name), esc(&short.to_uppercase()), esc(&game), stage, score, esc(&reason), ago(Some(at), now)
                );
            }
        }
    }
    if review.is_empty() {
        review = "<tr><td colspan=6>Nothing waiting.</td></tr>".into();
    }

    Html(format!(
        r#"<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>S3 Scores</title>
<style>
:root{{--bg:#f3f5f7;--panel:#fff;--ink:#17202a;--muted:#5b6775;--line:#d5dce3;--accent:#c9461a;color-scheme:light}}
@media (prefers-color-scheme:dark){{:root{{--bg:#10161c;--panel:#18212a;--ink:#e2e9ef;--muted:#93a3b4;--line:#2a3947;--accent:#f07a45;color-scheme:dark}}}}
body{{margin:0;padding:24px 16px;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,-apple-system,sans-serif}}
main{{max-width:1100px;margin:auto;display:grid;gap:20px}}
h1{{margin:0;font-size:24px}} h2{{margin:0 0 8px;font-size:16px}}
.cards{{display:flex;gap:12px;flex-wrap:wrap}} .card{{background:var(--panel);border:1px solid var(--line);padding:12px 16px}}
.card b{{display:block;font-size:22px;font-variant-numeric:tabular-nums}}
.wrap{{overflow-x:auto;background:var(--panel);border:1px solid var(--line)}}
table{{border-collapse:collapse;width:100%;min-width:760px}} th,td{{padding:8px 10px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}}
th{{font-size:11px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted)}} .n{{text-align:right;font-variant-numeric:tabular-nums}}
small{{color:var(--muted)}} .bar{{height:6px;background:var(--accent);margin-bottom:2px;min-width:1px}}
.tag{{font-size:11px;padding:1px 6px;border:1px solid var(--muted);color:var(--muted)}} .tag.warn{{border-color:var(--accent);color:var(--accent)}}
form{{display:inline}} button{{font:inherit;padding:4px 10px;cursor:pointer}}
</style>
<main>
<h1>S3 Scores</h1>
<div class=cards><div class=card><b>{players}</b>registered players</div><div class=card><b>{active}</b>active in 30 days</div><div class=card><b>{games}</b>games</div></div>
<section><h2>Games, most played first</h2><div class=wrap><table>
<tr><th>Game</th><th class=n>Plays 7d</th><th>Plays 30d</th><th class=n>Players 30d</th><th class=n>Finishes</th><th class=n>Minutes</th><th class=n>Rating</th><th class=n>Scores</th><th>Last played</th></tr>
{rows}</table></div><p><small>"Quiet" means no plays in 30 days: a candidate to archive. "Disliked" means under half thumbs up from at least five votes.</small></p></section>
<section><h2>Runs waiting for review</h2><div class=wrap><table>
<tr><th>Player</th><th>Game / stage</th><th class=n>Score</th><th>Why</th><th>Handed in</th><th></th></tr>
{review}</table></div></section>
</main>"#,
        games = stats.len()
    ))
    .into_response()
}

#[derive(Deserialize)]
struct DecideForm {
    form: String,
}

async fn decide(State(st): State<Shared>, headers: HeaderMap, Path((id, action)): Path<(i64, String)>, Form(f): Form<DecideForm>) -> Response {
    let form = match check(&st, &headers) {
        Ok(f) => f,
        Err(r) => return r,
    };
    if !bool::from(f.form.as_bytes().ct_eq(form.as_bytes())) {
        return (StatusCode::FORBIDDEN, "stale form").into_response();
    }
    let status = match action.as_str() {
        "accept" => "accepted",
        "reject" => "rejected",
        _ => return (StatusCode::NOT_FOUND, "no such action").into_response(),
    };
    let db = st.db.lock().unwrap();
    let _ = db.execute(
        "UPDATE runs SET status = ?1, reason = reason || ' (' || ?2 || ' by a person)' WHERE id = ?3 AND status = 'review'",
        params![status, action, id],
    );
    Redirect::to("/admin").into_response()
}
