//! The scoreboard's API, end to end in memory. A small shell script stands in
//! for the game's verifier (the real one is exercised by `s3 --score-test`).

use std::os::unix::fs::PermissionsExt;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use serde_json::{json, Value};
use tower::ServiceExt;

struct T {
    app: axum::Router,
    _dir: tempfile::TempDir,
}

const ADMIN: &str = "test-admin-token-0123456789";

/// A verifier that answers like `s3 --verify-run`: accepted for any replay,
/// claiming the time written in the replay file itself (so tests choose it).
fn fake_verifier(dir: &std::path::Path) -> std::path::PathBuf {
    let p = dir.join("verifier.sh");
    std::fs::write(
        &p,
        "#!/bin/sh\n# replay file: 'game stage claimed verdict-code'\nread g s c v < \"$2\"\n\
         printf '{\"verdict\":\"x\",\"game\":\"%s\",\"stage\":%s,\"time\":%s,\"claimed\":%s,\"reason\":\"fake\"}\\n' \"$g\" \"$s\" \"$c\" \"$c\"\nexit \"$v\"\n",
    )
    .unwrap();
    std::fs::set_permissions(&p, std::fs::Permissions::from_mode(0o755)).unwrap();
    p
}

fn setup() -> T {
    let dir = tempfile::tempdir().unwrap();
    let cfg = s3_scores::Config {
        db_path: dir.path().join("t.db").to_string_lossy().into(),
        admin_token: Some(ADMIN.into()),
        verifier: Some(fake_verifier(dir.path())),
        games: s3_scores::parse_games("rally time verify (3) RALLY\npuzzle points review A PUZZLE\n").unwrap(),
        trust_proxy: false,
    };
    T { app: s3_scores::router(s3_scores::state(cfg).unwrap()), _dir: dir }
}

impl T {
    async fn call(&self, method: &str, uri: &str, token: Option<&str>, body: Option<Value>) -> (StatusCode, Value) {
        let mut req = Request::builder().method(method).uri(uri);
        if let Some(t) = token {
            req = req.header("authorization", format!("Bearer {t}"));
        }
        let req = match body {
            Some(b) => req.header("content-type", "application/json").body(Body::from(b.to_string())),
            None => req.body(Body::empty()),
        }
        .unwrap();
        let resp = self.app.clone().oneshot(req).await.unwrap();
        let status = resp.status();
        let bytes = resp.into_body().collect().await.unwrap().to_bytes();
        (status, serde_json::from_slice(&bytes).unwrap_or(Value::String(String::from_utf8_lossy(&bytes).into())))
    }

    async fn register(&self, id: &str, name: &str) -> String {
        let (s, v) = self.call("POST", "/v1/players", None, Some(json!({"id": id, "name": name}))).await;
        assert_eq!(s, StatusCode::CREATED, "{v}");
        v["token"].as_str().unwrap().to_string()
    }

    async fn ticket(&self, token: &str, game: &str, stage: i64) -> String {
        let (s, v) = self.call("POST", "/v1/tickets", Some(token), Some(json!({"game": game, "stage": stage}))).await;
        assert_eq!(s, StatusCode::OK, "{v}");
        v["ticket"].as_str().unwrap().to_string()
    }
}

fn replay(game: &str, stage: i64, claimed: f64, code: i32) -> String {
    use base64::Engine;
    base64::engine::general_purpose::STANDARD.encode(format!("{game} {stage} {claimed} {code}\n"))
}

const ID1: &str = "3f2b8c1e-5d4a-4b6c-9e7f-0a1b2c3d4e5f";
const ID2: &str = "7a6b5c4d-3e2f-4a1b-8c9d-0e1f2a3b4c5d";
const ID3: &str = "0c1d2e3f-4a5b-4c6d-a7e8-9f0a1b2c3d4e";

#[tokio::test]
async fn registration_rules() {
    let t = setup();
    t.register(ID1, "ZACK R").await;
    let (s, _) = t.call("POST", "/v1/players", None, Some(json!({"id": ID1, "name": "AGAIN"}))).await;
    assert_eq!(s, StatusCode::CONFLICT, "an id registers once");
    for bad_id in ["not-a-uuid", "3F2B8C1E-5D4A-4B6C-9E7F-0A1B2C3D4E5F", "3f2b8c1e-5d4a-1b6c-9e7f-0a1b2c3d4e5f"] {
        let (s, _) = t.call("POST", "/v1/players", None, Some(json!({"id": bad_id, "name": "X"}))).await;
        assert_eq!(s, StatusCode::BAD_REQUEST, "{bad_id}");
    }
    for bad_name in ["", "lowercase", "THIRTEEN CHARS", "<SCRIPT>"] {
        let (s, _) = t.call("POST", "/v1/players", None, Some(json!({"id": ID2, "name": bad_name}))).await;
        assert_eq!(s, StatusCode::BAD_REQUEST, "{bad_name:?}");
    }
    let (s, _) = t.call("POST", "/v1/tickets", Some(&"0".repeat(64)), Some(json!({"game": "rally", "stage": 0}))).await;
    assert_eq!(s, StatusCode::UNAUTHORIZED, "a made-up token is refused");
}

#[tokio::test]
async fn verified_runs_and_the_board() {
    let t = setup();
    let a = t.register(ID1, "ALICE").await;
    let b = t.register(ID2, "BOB").await;
    // Tickets from a minute ago would be needed for a real 125 s run: the fake runs are short.
    let ta = t.ticket(&a, "rally", 3).await;
    let (_, v) = t.call("POST", "/v1/runs", Some(&a), Some(json!({"ticket": ta, "score": 0.5, "replay": replay("rally", 3, 0.5, 0)}))).await;
    assert_eq!(v["status"], "accepted", "{v}");
    assert_eq!(v["rank"], 1);
    let tb = t.ticket(&b, "rally", 3).await;
    let (_, v) = t.call("POST", "/v1/runs", Some(&b), Some(json!({"ticket": tb, "score": 0.25, "replay": replay("rally", 3, 0.25, 0)}))).await;
    assert_eq!(v["status"], "accepted", "{v}");
    assert_eq!(v["rank"], 1, "lower time wins");

    // The same ticket again: refused.
    let (s, _) = t.call("POST", "/v1/runs", Some(&b), Some(json!({"ticket": tb, "score": 0.25, "replay": replay("rally", 3, 0.25, 0)}))).await;
    assert_eq!(s, StatusCode::CONFLICT);
    // Someone else's ticket: refused.
    let tb2 = t.ticket(&b, "rally", 3).await;
    let (s, _) = t.call("POST", "/v1/runs", Some(&a), Some(json!({"ticket": tb2, "score": 0.1, "replay": replay("rally", 3, 0.1, 0)}))).await;
    assert_eq!(s, StatusCode::CONFLICT);

    // A replay of another stage, or a score that isn't the replay's: rejected.
    let t1 = t.ticket(&a, "rally", 3).await;
    let (_, v) = t.call("POST", "/v1/runs", Some(&a), Some(json!({"ticket": t1, "score": 0.1, "replay": replay("rally", 4, 0.1, 0)}))).await;
    assert_eq!(v["status"], "rejected", "{v}");
    let t2 = t.ticket(&a, "rally", 3).await;
    let (_, v) = t.call("POST", "/v1/runs", Some(&a), Some(json!({"ticket": t2, "score": 0.1, "replay": replay("rally", 3, 0.3, 0)}))).await;
    assert_eq!(v["status"], "rejected", "{v}");
    // No replay at all for a verified game: rejected.
    let t3 = t.ticket(&a, "rally", 3).await;
    let (_, v) = t.call("POST", "/v1/runs", Some(&a), Some(json!({"ticket": t3, "score": 0.1}))).await;
    assert_eq!(v["status"], "rejected", "{v}");
    // The verifier says review / rejected: that stands.
    let t4 = t.ticket(&a, "rally", 3).await;
    let (_, v) = t.call("POST", "/v1/runs", Some(&a), Some(json!({"ticket": t4, "score": 0.2, "replay": replay("rally", 3, 0.2, 1)}))).await;
    assert_eq!(v["status"], "review", "{v}");
    // A 90-second run handed in the moment its ticket was issued: held for review.
    let t5 = t.ticket(&a, "rally", 3).await;
    let (_, v) = t.call("POST", "/v1/runs", Some(&a), Some(json!({"ticket": t5, "score": 90.0, "replay": replay("rally", 3, 90.0, 0)}))).await;
    assert_eq!(v["status"], "review", "{v}");
    assert!(v["reason"].as_str().unwrap().contains("sooner"));

    // The board: best accepted run per player, Bob first. Only accepted runs count.
    let (s, v) = t.call("GET", "/v1/leaderboard/rally/3", Some(&a), None).await;
    assert_eq!(s, StatusCode::OK);
    assert_eq!(v["total"], 2);
    assert_eq!(v["top"][0]["name"], "BOB");
    assert_eq!(v["top"][1]["name"], "ALICE");
    assert_eq!(v["top"][1]["you"], true);
    assert_eq!(v["you"]["rank"], 2);
    assert_eq!(v["top"][0]["short_id"], "7A6B5C4D");
    let (_, anon) = t.call("GET", "/v1/leaderboard/rally/3", None, None).await;
    assert!(anon["you"].is_null());
}

#[tokio::test]
async fn unverified_games_wait_for_a_person() {
    let t = setup();
    let a = t.register(ID1, "ALICE").await;
    let tk = t.ticket(&a, "puzzle", 0).await;
    let (_, v) = t.call("POST", "/v1/runs", Some(&a), Some(json!({"ticket": tk, "score": 900.0}))).await;
    assert_eq!(v["status"], "review");
    let (s, _) = t.call("POST", "/v1/tickets", Some(&a), Some(json!({"game": "nosuchgame", "stage": 0}))).await;
    assert_eq!(s, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn plays_ratings_stats_and_leaving() {
    let t = setup();
    let a = t.register(ID1, "ALICE").await;
    let b = t.register(ID2, "BOB").await;
    for tok in [&a, &b] {
        let (s, _) = t.call("POST", "/v1/plays", Some(tok), Some(json!({"game": "rally", "event": "start"}))).await;
        assert_eq!(s, StatusCode::NO_CONTENT);
    }
    t.call("POST", "/v1/plays", Some(&a), Some(json!({"game": "rally", "event": "finish", "seconds": 600}))).await;
    t.call("PUT", "/v1/ratings/rally", Some(&a), Some(json!({"thumb": 1}))).await;
    t.call("PUT", "/v1/ratings/rally", Some(&b), Some(json!({"thumb": -1}))).await;
    t.call("PUT", "/v1/ratings/rally", Some(&b), Some(json!({"thumb": 1}))).await;  // changed their mind
    let (s, _) = t.call("PUT", "/v1/ratings/rally", Some(&a), Some(json!({"thumb": 5}))).await;
    assert_eq!(s, StatusCode::BAD_REQUEST);
    let (_, v) = t.call("GET", "/v1/stats", None, None).await;
    let rally = v["games"].as_array().unwrap().iter().find(|g| g["game"] == "rally").unwrap().clone();
    assert_eq!(rally["plays_7d"], 2);
    assert_eq!(rally["players_30d"], 2);
    assert_eq!(rally["up"], 2);
    assert_eq!(rally["down"], 0);
    // Leaving takes everything of theirs with them.
    let (s, _) = t.call("DELETE", "/v1/players/me", Some(&b), None).await;
    assert_eq!(s, StatusCode::OK);
    let (_, v) = t.call("GET", "/v1/stats", None, None).await;
    let rally = v["games"].as_array().unwrap().iter().find(|g| g["game"] == "rally").unwrap().clone();
    assert_eq!(rally["plays_7d"], 1);
    assert_eq!(rally["up"], 1);
    let (s, _) = t.call("POST", "/v1/tickets", Some(&b), Some(json!({"game": "rally", "stage": 0}))).await;
    assert_eq!(s, StatusCode::UNAUTHORIZED);
}

#[tokio::test]
async fn sign_up_rate_limit() {
    let t = setup();
    let mut last = StatusCode::OK;
    for k in 0..12 {
        let id = format!("{:08x}-0000-4000-8000-000000000000", k);
        last = t.call("POST", "/v1/players", None, Some(json!({"id": id, "name": "SPAM"}))).await.0;
    }
    assert_eq!(last, StatusCode::TOO_MANY_REQUESTS);
    let _ = ID3;
}

#[tokio::test]
async fn dashboard_needs_the_admin_password() {
    let t = setup();
    let get = |auth: Option<String>| {
        let mut r = Request::builder().uri("/admin");
        if let Some(a) = auth {
            r = r.header("authorization", a);
        }
        t.app.clone().oneshot(r.body(Body::empty()).unwrap())
    };
    assert_eq!(get(None).await.unwrap().status(), StatusCode::UNAUTHORIZED);
    use base64::Engine;
    let wrong = format!("Basic {}", base64::engine::general_purpose::STANDARD.encode("admin:nope"));
    assert_eq!(get(Some(wrong)).await.unwrap().status(), StatusCode::UNAUTHORIZED);
    let right = format!("Basic {}", base64::engine::general_purpose::STANDARD.encode(format!("admin:{ADMIN}")));
    let resp = get(Some(right.clone())).await.unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let html = String::from_utf8(resp.into_body().collect().await.unwrap().to_bytes().to_vec()).unwrap();
    assert!(html.contains("(3) RALLY") && html.contains("quiet"));

    // A review decision without the form token (as another site would send it): refused.
    let a = t.register(ID1, "ALICE").await;
    let tk = t.ticket(&a, "puzzle", 0).await;
    t.call("POST", "/v1/runs", Some(&a), Some(json!({"ticket": tk, "score": 1.0}))).await;
    let post = |form: &str| {
        Request::builder()
            .method("POST")
            .uri("/admin/runs/1/accept")
            .header("authorization", right.clone())
            .header("content-type", "application/x-www-form-urlencoded")
            .body(Body::from(format!("form={form}")))
            .unwrap()
    };
    assert_eq!(t.app.clone().oneshot(post("forged")).await.unwrap().status(), StatusCode::FORBIDDEN);
    let token = {
        use sha2::{Digest, Sha256};
        hex::encode(Sha256::digest(format!("s3-admin-form:{ADMIN}")))
    };
    assert_eq!(t.app.clone().oneshot(post(&token)).await.unwrap().status(), StatusCode::SEE_OTHER);
    let (_, v) = t.call("GET", "/v1/leaderboard/puzzle/0", None, None).await;
    assert_eq!(v["top"][0]["name"], "ALICE", "accepted by a person, now on the board");
}

#[tokio::test]
async fn a_run_without_a_ticket_waits_for_a_person() {
    let t = setup();
    let a = t.register(ID1, "ALICE").await;
    let (_, v) = t
        .call("POST", "/v1/runs", Some(&a), Some(json!({"game": "rally", "stage": 2, "score": 0.5, "replay": replay("rally", 2, 0.5, 0)})))
        .await;
    assert_eq!(v["status"], "review", "{v}");
    let (_, v) = t
        .call("POST", "/v1/runs", Some(&a), Some(json!({"game": "rally", "stage": 2, "score": 0.5, "replay": replay("rally", 2, 0.9, 0)})))
        .await;
    assert_eq!(v["status"], "rejected", "the replay is still checked: {v}");
    let (s, _) = t.call("POST", "/v1/runs", Some(&a), Some(json!({"game": "nosuch", "stage": 2, "score": 0.5}))).await;
    assert_eq!(s, StatusCode::NOT_FOUND);
}
