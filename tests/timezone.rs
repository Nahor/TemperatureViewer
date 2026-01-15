// spell-checker:words chrono datetime nocapture

use std::time::Duration;

use jiff::{Timestamp, tz::TimeZone};
#[cfg(feature = "rayon")]
use rayon::prelude::*;

const RUN_DURATION: Duration = Duration::from_secs(5);

// No rayon:
//    cargo test -r --no-default-features -- --nocapture
// Rayon single thread:
//    RAYON_NUM_THREADS=1 cargo test -r -- --nocapture
// Rayon auto-thread:
//    cargo test -r -- --nocapture

#[test]
fn test() {
    let start = std::time::Instant::now();
    let mut count: i64 = 0;

    let tz = TimeZone::get("America/Los_Angeles").unwrap();
    while (std::time::Instant::now() - start) < RUN_DURATION {
        #[cfg(feature = "rayon")]
        let iter = (0..100_000).into_par_iter();
        #[cfg(not(feature = "rayon"))]
        let iter = (0..100_000).into_iter();
        count += iter
            .map(|acc| {
                Timestamp::new(acc, 0)
                    .unwrap_or_else(|err| panic!("Failed with {acc}: {err}"))
                    .to_zoned(tz.clone());
                1
            })
            .sum::<i64>();
    }

    let end = std::time::Instant::now();
    println!(
        "Time: {:?}, {} iterations, {:0} it/sec",
        (end - start),
        count,
        count as f64 / (end - start).as_secs_f64()
    );
}
