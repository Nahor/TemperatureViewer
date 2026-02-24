// spell-checker:words condvar chrono

use std::{
    error::Error,
    io::{BufWriter, Write},
    sync::{Arc, Condvar, Mutex, mpsc},
};

use jiff::{SignedDuration, tz::TimeZone};
#[cfg(feature = "rayon")]
use rayon::prelude::*;
use sensor::DATAPOINT_EPOCH;

use crate::progress::Progress;

mod progress;

#[allow(unused)]
const MAX_THREADS: usize = usize::MAX;

const LINE_LEN: usize = r#""9999-99-99 99:99","20.0000","20.0000"."#.len();
const HEADER: &str = concat!(
    r#""Timestamp","Temperature (°F)","Relative Humidity (%)""#,
    "\n"
);
const HEADER_LEN: usize = HEADER.len();

fn size_format(v: usize) -> String {
    const KB: f32 = 1024_f32;
    const MB: f32 = 1024_f32 * KB;
    const GB: f32 = 1024_f32 * MB;
    let v = v as f32;
    if v > GB {
        format!("{v:.2}GB", v = v / GB)
    } else if v > MB {
        format!("{v:.2}MB", v = v / MB)
    } else if v > KB {
        format!("{v:.2}KB", v = v / KB)
    } else {
        format!("{v}B")
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let count: usize = std::env::args()
        .nth(1)
        .map(|x| x.parse())
        .unwrap_or(Ok(10))?;
    let expected_size = HEADER_LEN + count * LINE_LEN;
    let path = std::env::args().nth(2).unwrap_or("./test.csv".to_owned());
    println!(
        "Generating {count} entries in '{path}' ({} - {expected_size} bytes)",
        size_format(expected_size)
    );

    let file = std::fs::File::create(path)?;
    // Set the size so the filesystem can preallocate (it's ok if this fails,
    // some file cannot be resize, e.g. NUL on Windows and /dev/null on Unix)
    let _ = file.set_len(expected_size as u64);

    let mut file = BufWriter::with_capacity(1024 * 1024, file);
    file.write_all(HEADER.as_bytes())?;

    // Split into chunks to avoid one job per line and avoid needless refresh
    // of the display
    const CHUNK_SIZE: usize = 10000;
    let range = 0..(count.div_ceil(CHUNK_SIZE));

    #[cfg(feature = "rayon")]
    let num_threads = {
        let num_threads = std::thread::available_parallelism()
            .map_or(8, |c| c.get())
            .min(MAX_THREADS);
        rayon::ThreadPoolBuilder::new()
            .num_threads(num_threads)
            .build_global()
            .unwrap();
        num_threads
    };
    #[cfg(not(feature = "rayon"))]
    let num_threads = 1;

    // Channel with enough capacity to hold an item from each thread
    let (tx, rx) = mpsc::sync_channel(num_threads);

    // Gate to block the thread from sending their result out of order
    let gate = Arc::new((Mutex::new(0), Condvar::new()));

    static TZ: TimeZone = jiff::tz::get!("America/Los_Angeles");
    let tz = TimeZone::try_system().unwrap_or_else(|_| TZ.clone());

    let epoch = DATAPOINT_EPOCH.to_zoned(tz.clone()).unwrap();

    let worker = move || {
        #[cfg(feature = "rayon")]
        // Use par_bridge which forces processing each chunk in sequence,
        // instead of random chunks
        let iter = range.par_bridge();
        #[cfg(not(feature = "rayon"))]
        let iter = range.into_iter();

        iter.map(|i| {
            // The job
            let start = (i * CHUNK_SIZE).min(count);
            let end = (start + CHUNK_SIZE).min(count);

            let start_date = epoch.clone() + SignedDuration::from_mins(start as i64);

            let chunk = (0..(end - start))
                .scan(start_date, |date, _| {
                    // Convert to civil time to remove the timezone, thus
                    // preventing `strftime` from cloning it (when `TimeZone` is
                    // backed by an `Arc`, cloning gets expensive fast in
                    // multithreaded environment because of contention on the
                    // Arc's atomic ref-counter)
                    let civil = date.datetime();

                    let mut str = [0_u8; LINE_LEN];
                    let _ = write!(
                        &mut str[..],
                        "{}",
                        civil.strftime(concat!(r#""%Y-%m-%d %H:%M","20.0000","20.0000""#, "\n"))
                    );

                    *date += SignedDuration::from_mins(1);

                    Some(str)
                })
                .fold(Vec::with_capacity(CHUNK_SIZE * LINE_LEN), |mut vec, str| {
                    vec.extend_from_slice(&str);
                    vec
                });

            (i, chunk)
        })
        .for_each(|(i, value)| {
            // Send the result to the receiver, in order
            let (lock, cond) = &*gate;

            {
                // Block until it's our turn
                let mut guard = cond.wait_while(lock.lock().unwrap(), |v| *v < i).unwrap();
                tx.send((i, value)).unwrap();
                *guard = i + 1;
            }

            cond.notify_all();
        });
    };

    let receiver = move || {
        let mut progress = Progress::new(count, 50);
        receive(rx, |value| {
            progress.inc(CHUNK_SIZE);
            file.write_all(&value).unwrap();
        });
        file.flush().unwrap();

        progress.finish();
        println!();
    };

    // Start a thread to manage the jobs (need a thread so that we can try
    // to read the results while the jobs are being generated)
    let start = std::time::Instant::now();
    std::thread::scope(|s| {
        s.spawn(move || {
            worker();
        });

        s.spawn(move || {
            receiver();
        });
    });
    let elapsed = start.elapsed();
    println!("time: {:.2?}", elapsed,);
    println!(
        "speed: {} line/s",
        (count as f32 / elapsed.as_secs_f32()).round()
    );

    Ok(())
}

fn receive<T>(rx: mpsc::Receiver<(usize, T)>, mut op: impl FnMut(T)) {
    for (next_index, (i, value)) in rx.into_iter().enumerate() {
        assert_eq!(i, next_index, "Wrong index {i}, expected {next_index}");
        op(value);
    }
}
