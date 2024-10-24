// spell-checker:words condvar chrono

use std::{
    error::Error,
    io::{BufWriter, Write},
    sync::{mpsc, Arc, Condvar, Mutex},
    time::{Duration, Instant},
    vec,
};

use chrono::DateTime;
#[cfg(feature = "rayon")]
use rayon::prelude::*;

const LINE_LEN: usize = r#""9999-99-99 99:99","20.0000","20.0000"."#.len();
const HEADER: &str = concat!(
    r#""Timestamp","Temperature (°F)","Relative Humidity (%)""#,
    "\n"
);
const HEADER_LEN: usize = HEADER.len();

fn size_format(v: usize) -> String {
    const KB: usize = 1024;
    const MB: usize = 1024 * KB;
    const GB: usize = 1024 * MB;
    if v > GB {
        format!("{:.2}GB", v as f32 / GB as f32)
    } else if v > MB {
        format!("{:.2}MB", v as f32 / MB as f32)
    } else if v > KB {
        format!("{:.2}KB", v as f32 / KB as f32)
    } else {
        format!("{}B", v)
    }
}

struct Progress {
    max_value: usize,
    value: usize,

    width: usize,
    mapped_progress: usize,

    start: Instant,
    last_update: Instant,
    speed: Option<f64>,
}
impl Progress {
    const PROGRESS_CHARS: &'static [char] = &['▏', '▎', '▍', '▌', '▋', '▊', '▉', '█'];
    fn new(max_value: usize, width: usize) -> Self {
        Self {
            max_value,
            value: 0,

            width,
            mapped_progress: 0,

            start: Instant::now(),
            last_update: Instant::now() - Duration::from_secs(1),
            speed: None,
        }
    }
    fn inc(&mut self, inc: usize) {
        self.set_value(self.value + inc);
        self.draw();
    }
    fn set_value(&mut self, v: usize) {
        self.value = v.min(self.max_value);
        self.draw();
    }
    fn finish(&mut self) {
        self.value = self.max_value;
        self.draw();
    }

    fn draw(&mut self) {
        let virtual_width = self.width * Self::PROGRESS_CHARS.len();
        let progress = self.value * virtual_width / self.max_value;

        // Skip the refresh if there is no visible progress and not enough time has passed
        let time = Instant::now();
        if (time < self.last_update + Duration::from_millis(100))
            && (progress == self.mapped_progress)
        {
            return;
        }
        self.last_update = time;
        self.mapped_progress = progress;

        // Generate the progress bar
        let full_blocks = progress / Self::PROGRESS_CHARS.len();
        let partial_block_idx = progress % Self::PROGRESS_CHARS.len();
        let mut vec: Vec<char> = vec![*Self::PROGRESS_CHARS.last().unwrap(); full_blocks];
        if partial_block_idx > 0 {
            vec.push(Self::PROGRESS_CHARS[partial_block_idx - 1]);
        }
        let progress_str: String = vec.iter().collect();

        // Estimate the speed
        // Do it only if we have enough data to mean something
        // And use a running average to avoid too much variance early on
        let elapsed = (Instant::now() - self.start).as_secs_f64();
        if elapsed > Duration::from_secs(1).as_secs_f64() {
            self.speed = match self.speed {
                Some(speed) => Some(speed * 0.9 + self.value as f64 / elapsed * 0.1),
                None => Some(self.value as f64 / elapsed),
            }
        }

        // Compute the ETA
        let eta = match self.speed {
            Some(speed) => {
                let eta = f64::ceil((self.max_value - self.value) as f64 / speed) as usize;
                format!("{}:{:02}", eta / 60, eta % 60)
            }
            None => "unk".to_owned(),
        };

        print!(
            //"\r[{:>2}:{:02}] [{:░<50}] {:>3}% ({})",
            "\r[{:>2}:{:02}] [{: <50}] {:>3}% ({})",
            elapsed as usize / 60,
            elapsed as usize % 60,
            progress_str,
            self.value * 100 / self.max_value,
            eta
        );
        std::io::stdout().flush().unwrap();
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
    // Set the size so the filesystem can preallocate
    file.set_len(expected_size as u64)?;

    let mut file = BufWriter::with_capacity(1024 * 1024, file);
    file.write_all(HEADER.as_bytes())?;

    // Split into chunks to avoid one job per line and avoid needless refresh
    // of the display
    const CHUNK_SIZE: usize = 10000;
    let range = 0..(count.div_ceil(CHUNK_SIZE));

    // Channel with enough capacity to hold an item from each thread
    let (tx, rx) = mpsc::sync_channel(std::thread::available_parallelism().map_or(8, |n| n.get()));

    // Gate to block the thread from sending their result out of order
    let gate = Arc::new((Mutex::new(0), Condvar::new()));

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
            let chunk: String = (start..end)
                .map(|i| {
                    let d = DateTime::from_timestamp(i as i64 * 60, 0).unwrap();
                    let d = d.with_timezone(&chrono_tz::America::Los_Angeles);
                    d.format(concat!(r#""%Y-%m-%d %H:%M","20.0000","20.0000""#, "\n"))
                        .to_string()
                })
                .collect();

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
            file.write_all(value.as_bytes()).unwrap();
        });
        file.flush().unwrap();

        progress.finish();
        println!();
    };

    // Start a thread to manage the jobs (need a thread so that we can try
    // to read the results while the jobs are being generated)
    std::thread::scope(|s| {
        s.spawn(move || {
            worker();
        });

        s.spawn(move || {
            receiver();
        });
    });

    Ok(())
}

fn receive<T>(rx: mpsc::Receiver<(usize, T)>, mut op: impl FnMut(T)) {
    for (next_index, (i, value)) in rx.into_iter().enumerate() {
        assert_eq!(i, next_index, "Wrong index {i}, expected {next_index}");
        op(value);
    }
}
