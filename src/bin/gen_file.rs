// spell-checker:words condvar chrono

use std::{
    error::Error,
    fmt::Display,
    io::{BufWriter, Write},
    sync::{mpsc, Arc, Condvar, Mutex},
    time::{Duration, Instant},
    vec,
};

use chrono::DateTime;
use rayon::prelude::*;

#[derive(Debug, Default)]
struct MyError;
impl Error for MyError {}
impl Display for MyError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "MyError")
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let count = std::env::args()
        .nth(1)
        .map(|x| x.parse())
        .unwrap_or(Ok(10))?;
    let path = std::env::args().nth(2).unwrap_or("./test.csv".to_owned());
    println!("Generating {count} entries in '{path}'");

    let mut file = BufWriter::with_capacity(1024 * 1024, std::fs::File::create(path)?);

    file.write_all("\"Timestamp\",\"Temperature (°F)\",\"Relative Humidity (%)\"\n".as_bytes())?;

    let start = Instant::now();
    let mut speed: Option<f64> = None;

    // closure to draw the progress
    let progress_chars: Vec<_> = "▏▎▍▌▋▊▉█".chars().collect();
    let mut last_update: Instant = Instant::now() - Duration::from_secs(1);
    let mut progress = String::default();
    let mut draw = |i| -> () {
        const COL_COUNT: usize = 50;
        let bucket_count: usize = 50 * progress_chars.len();

        // Generate the progress bar
        let full_blocks = i * COL_COUNT / count;
        let partial_block_idx = (i * bucket_count / count) % progress_chars.len() - 1;
        let mut vec: Vec<char> = vec![*progress_chars.last().unwrap(); full_blocks];
        if let Some(c) = progress_chars.get(partial_block_idx) {
            vec.push(*c);
        }
        let new_progress: String = vec.iter().collect();

        // Skip the refresh if there is no visible progress and not enough time has passed
        let time = Instant::now();
        if (time < last_update + Duration::from_millis(100)) && (new_progress == progress) {
            return;
        }
        last_update = time;
        progress = new_progress;

        // Estimate the speed
        // Do it only if we have enough data to mean something
        // And use a running average to avoid too much variance early on
        let elapsed = (Instant::now() - start).as_secs_f64();
        if i > (count / 100000) {
            speed = match speed {
                Some(speed) => Some(speed * 0.9 + i as f64 / elapsed * 0.1),
                None => Some(i as f64 / elapsed),
            }
        }

        // Compute the ETA
        let eta = match speed {
            Some(speed) => {
                let eta = f64::ceil((count - i) as f64 / speed) as usize;
                format!("{}:{:02}", eta / 60, eta % 60)
            }
            None => "unk".to_owned(),
        };

        print!(
            "\r[{:>2}:{:02}] [{:░<50}] {:>3}% ({})",
            elapsed as usize / 60,
            elapsed as usize % 60,
            progress,
            i * 100 / count,
            eta
        );
        std::io::stdout().flush().unwrap();
    };

    // Split into chunks to avoid one job per line and avoid needless refresh
    // of the display
    const CHUNK_SIZE: usize = 10000;
    let range = 0..=(count / CHUNK_SIZE);

    // Channel with enough capacity to hold an item from each thread
    let (tx, rx) = mpsc::sync_channel(std::thread::available_parallelism().map_or(8, |n| n.get()));

    // Gate to block the thread from sending their result out of order
    let gate = Arc::new((Mutex::new(0), Condvar::new()));

    // Start a thread to manage the jobs (need a thread so that we can try
    // to read the results while the jobs are being generated)
    rayon::scope(|s| {
        s.spawn(move |_| {
            range
                .par_bridge()
                .map(|i| {
                    // The job
                    let start = (i * CHUNK_SIZE).min(count);
                    let end = (start + CHUNK_SIZE).min(count);
                    let chunk: String = (start..end)
                        .into_iter()
                        .map(|i| {
                            let d = DateTime::from_timestamp(i as i64 * 60, 0).unwrap();
                            let d = d.with_timezone(&chrono_tz::America::Los_Angeles);
                            d.format(concat!(r#""%Y-%m-%d %H:%M","20","20""#, "\n"))
                                .to_string()
                        })
                        .collect();

                    (i, chunk)
                })
                .for_each_with((tx, gate), |(tx, gate), (i, value)| {
                    // Send the result to the receiver, in order
                    let (lock, cond) = &**gate;

                    {
                        // Block until it's our turn
                        let mut guard = cond.wait_while(lock.lock().unwrap(), |v| *v < i).unwrap();
                        tx.send((i, value)).unwrap();
                        *guard = i + 1;
                    }

                    cond.notify_all();
                });
        });

        receive(rx, |i, value| {
            draw((i * CHUNK_SIZE).min(count));
            file.write_all(value.as_bytes()).unwrap();
        });
        draw(count);
        file.flush().unwrap();
    });

    Ok(())
}

fn receive<T>(rx: mpsc::Receiver<(usize, T)>, mut op: impl FnMut(usize, T)) {
    let mut next_index: usize = 0;
    for (i, value) in rx {
        assert_eq!(i, next_index, "Wrong index {i}, expected {next_index}");
        op(i, value);
        next_index += 1;
    }
}