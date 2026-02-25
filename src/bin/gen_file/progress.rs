use std::{
    io::Write,
    time::{Duration, Instant},
};

pub(crate) struct Progress {
    max_value: usize,
    value: usize,

    width: usize,
    mapped_progress: usize,

    start: Instant,
    next_update: Instant,
    speed: Option<f64>,
}

impl Progress {
    const PROGRESS_CHARS: &'static [char] = &['▏', '▎', '▍', '▌', '▋', '▊', '▉', '█'];

    pub(crate) fn new(max_value: usize, width: usize) -> Self {
        Self {
            max_value,
            value: 0,

            width,
            mapped_progress: 0,

            start: Instant::now(),
            next_update: Instant::now(),
            speed: None,
        }
    }
    pub(crate) fn inc(&mut self, inc: usize) {
        self.set_value(self.value + inc);
        self.draw();
    }
    pub(crate) fn set_value(&mut self, v: usize) {
        self.value = v.min(self.max_value);
        self.draw();
    }
    pub(crate) fn finish(&mut self) {
        // Force the value to ensure we don't finish on "99%"
        self.value = self.max_value;
        // Force the speed in case we finished quickly and the ETA is still "unk"
        self.speed = Some(1.0);
        // Force a display update because of the changes above
        self.next_update = Instant::now();
        self.draw();
    }

    fn draw(&mut self) {
        let virtual_width = self.width * Self::PROGRESS_CHARS.len();
        let progress = self.value * virtual_width / self.max_value;

        // Skip the refresh if there is no visible progress and not enough time has passed
        let time = Instant::now();
        if (time < self.next_update) && (progress == self.mapped_progress) {
            return;
        }
        self.next_update = time + Duration::from_millis(100);
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
