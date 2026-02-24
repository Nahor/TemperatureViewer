use std::{
    io::Write,
    time::{Duration, Instant},
};

pub(crate) struct Progress {
    pub max_value: usize,
    pub value: usize,

    pub width: usize,
    pub mapped_progress: usize,

    pub start: Instant,
    pub last_update: Instant,
    pub speed: Option<f64>,
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
            last_update: Instant::now() - Duration::from_secs(1),
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
