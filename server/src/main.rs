//! s3-scores: the S3 online scoreboard server.
//!
//! Environment:
//!   S3_SCORES_DB      SQLite file (default s3-scores.db)
//!   S3_SCORES_ADDR    listen address (default 127.0.0.1:8790; put TLS in front for the internet)
//!   S3_SCORES_GAMES   games file (default games.txt next to the binary's working directory)
//!   S3_VERIFIER       path to the s3 binary that runs `--verify-run`
//!   S3_ADMIN_TOKEN    password for /admin (user "admin"); unset turns the dashboard off
//!   S3_TRUST_PROXY=1  take client addresses from X-Forwarded-For (only behind your own proxy)

use std::net::SocketAddr;

fn main() {
    let env = |k: &str| std::env::var(k).ok().filter(|v| !v.is_empty());
    let games_path = env("S3_SCORES_GAMES").unwrap_or_else(|| "games.txt".into());
    let games = match std::fs::read_to_string(&games_path).map_err(|e| e.to_string()).and_then(|t| s3_scores::parse_games(&t)) {
        Ok(g) if !g.is_empty() => g,
        Ok(_) => fail(&format!("{games_path}: no games listed")),
        Err(e) => fail(&format!("{games_path}: {e}")),
    };
    let admin_token = env("S3_ADMIN_TOKEN");
    if let Some(t) = &admin_token {
        if t.len() < 20 {
            fail("S3_ADMIN_TOKEN must be at least 20 characters");
        }
    }
    let cfg = s3_scores::Config {
        db_path: env("S3_SCORES_DB").unwrap_or_else(|| "s3-scores.db".into()),
        admin_token,
        verifier: env("S3_VERIFIER").map(Into::into),
        games,
        trust_proxy: env("S3_TRUST_PROXY").as_deref() == Some("1"),
    };
    let addr: SocketAddr = env("S3_SCORES_ADDR").unwrap_or_else(|| "127.0.0.1:8790".into()).parse().unwrap_or_else(|_| fail("bad S3_SCORES_ADDR"));
    let st = s3_scores::state(cfg).unwrap_or_else(|e| fail(&e));
    eprintln!(
        "s3-scores on http://{addr}  ({} games, verifier {}, dashboard {})",
        st.cfg.games.len(),
        if st.cfg.verifier.is_some() { "on" } else { "off: timed runs wait for review" },
        if st.cfg.admin_token.is_some() { "at /admin" } else { "off" }
    );
    let rt = tokio::runtime::Runtime::new().unwrap_or_else(|e| fail(&e.to_string()));
    rt.block_on(async move {
        let listener = tokio::net::TcpListener::bind(addr).await.unwrap_or_else(|e| fail(&format!("{addr}: {e}")));
        let app = s3_scores::router(st).into_make_service_with_connect_info::<SocketAddr>();
        axum::serve(listener, app)
            .with_graceful_shutdown(async {
                let _ = tokio::signal::ctrl_c().await;
            })
            .await
            .unwrap_or_else(|e| fail(&e.to_string()));
    });
}

fn fail(msg: &str) -> ! {
    eprintln!("s3-scores: {msg}");
    std::process::exit(1)
}
