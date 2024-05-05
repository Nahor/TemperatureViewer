// spell-checker:words chrono datetime Deque eframe egui memmap2 mmap nahor

mod data;
mod error;

use chrono::prelude::DateTime;
pub use data::*;
pub use error::{FromSource, SensorError};

#[cfg(feature = "mmap")]
use memmap2::MmapOptions;
#[cfg(feature = "rayon")]
use rayon::prelude::*;
use std::{collections::VecDeque, error::Error, fmt::Display, fs, str::FromStr};

fn line_error<E: Error>(lineno: usize, err: E) -> SensorError
where
    SensorError: FromSource<String, E>,
{
    SensorError::from_source(format!("failed to parse line {}", lineno), err)
}

//////////////////////////////////////
// mmap version
#[cfg(feature = "mmap")]
struct FileData {
    mmap: memmap2::Mmap,
}
#[cfg(feature = "mmap")]
impl FileData {
    fn open(file: &str) -> Result<Self, SensorError> {
        let file = fs::File::open(file)?;
        let mmap = unsafe { MmapOptions::new().map(&file)? };
        Ok(Self { mmap })
    }
    #[cfg(feature = "rayon")]
    fn vec(&self) -> Result<VecDeque<&str>, SensorError> {
        let lines: VecDeque<_> = self.mmap.par_split(|&b| b == b'\n').collect();
        let iter = lines.into_par_iter();
        iter.enumerate().map(FileData::to_utf8).collect()
    }
    #[cfg(not(feature = "rayon"))]
    fn vec<'a>(&'a self) -> Result<VecDeque<&'a str>, SensorError> {
        let lines: VecDeque<_> = self.mmap.split(|&b| b == b'\n').collect();
        let iter = lines.into_iter();
        iter.enumerate().map(FileData::to_utf8).collect()
    }

    fn to_utf8((lineno, x): (usize, &[u8])) -> Result<&str, SensorError> {
        std::str::from_utf8(x).map_err(
            |err| line_error(lineno + 1, err), // "+1" to start at 1
        )
    }
}

//////////////////////////////////////
// `read_to_string` version
#[cfg(not(feature = "mmap"))]
struct FileData {
    content: String,
}
#[cfg(not(feature = "mmap"))]
impl FileData {
    fn open(file: &str) -> Result<Self, SensorError> {
        Ok(Self {
            content: fs::read_to_string(file)?,
        })
    }
    #[cfg(feature = "rayon")]
    fn vec<'a>(&'a self) -> Result<VecDeque<&'a str>, SensorError> {
        Ok(self.content.par_split('\n').collect())
    }
    #[cfg(not(feature = "rayon"))]
    fn vec<'a>(&'a self) -> Result<VecDeque<&'a str>, SensorError> {
        Ok(self.content.split('\n').collect())
    }
}

pub fn parse(file: &str) -> Result<Vec<DataPoint>, SensorError> {
    let start = std::time::Instant::now();

    let file = FileData::open(file)?;
    let mut lines = file.vec()?;

    let as_celsius = parse_header(lines.pop_front().ok_or(SensorError::from("File empty"))?)?;
    if let Some(str) = lines.back() {
        if str.is_empty() {
            lines.pop_back();
        }
    }
    if lines.is_empty() {
        return Err(SensorError::from("No data"));
    }

    // TODO(nahor): how to get the timezone from Windows?
    let tz = chrono_tz::Tz::from_str(&match std::env::var("TZ") {
        Ok(tz) => {
            println!("Timezone: {tz:?}");
            tz
        }
        Err(_) => {
            const DEFAULT: &str = "America/Los_Angeles";
            println!("Using default timezone: {DEFAULT}");
            DEFAULT.to_owned()
        }
    })?;

    println!("Load: {:.03?}", (std::time::Instant::now() - start));
    let start = std::time::Instant::now();

    // First pass parses the strings using the data as-is (i.e. pretending it's UTC)
    //let data = first_pass(content, pos + 1, as_celsius)?;
    let data = first_pass(lines, as_celsius)?;
    let mid1 = std::time::Instant::now();
    println!(
        "    First pass: {:.3?} ({:.0} lines/sec)",
        (mid1 - start),
        data.0.len() as f32 / (mid1 - start).as_secs_f32()
    );

    // Second pass converts the date from "fake UTC" to "real UTC" (UTC + TZ)
    let data = second_pass(data, tz)?;
    let mid2 = std::time::Instant::now();
    println!(
        "    Second pass: {:.3?} ({:.0} lines/sec)",
        (mid2 - mid1),
        data.0.len() as f32 / (mid2 - mid1).as_secs_f32()
    );

    // Third pass check for date continuity (data at 1min interval)
    let data = third_pass(data)?;

    let end = std::time::Instant::now();
    println!(
        "    Third pass: {:.3?} ({:.0} lines/sec)",
        (end - mid2),
        data.len() as f32 / (end - mid2).as_secs_f32()
    );

    // Statistics
    // {
    //     let start = std::time::Instant::now();
    //     #[cfg(feature = "rayon")]
    //     let _data: Vec<_> = data.par_iter().map(|d| d.clone()).collect();
    //     #[cfg(not(feature = "rayon"))]
    //     let data: Vec<_> = data.iter().map(|d| d.clone()).collect();
    //     let end = std::time::Instant::now();
    //     println!(
    //         "    (Empty pass: {:.3?} ({:.0} lines/sec))",
    //         (end - start),
    //         data.len() as f32 / (end - start).as_secs_f32()
    //     );
    // }

    println!(
        "Total: {:.3?} for {} lines ({:.0} lines/sec)",
        (end - start),
        data.len(),
        data.len() as f32 / (end - start).as_secs_f32()
    );

    Ok(data)
}

fn parse_header(header: &str) -> Result<bool, SensorError> {
    if header == r#""Timestamp","Temperature (°C)","Relative Humidity (%)""# {
        Ok(true)
    } else if header == r#""Timestamp","Temperature (°F)","Relative Humidity (%)""# {
        Ok(false)
    } else {
        Err(SensorError::from("invalid header"))
    }
}

fn first_pass(
    lines: VecDeque<&str>,
    as_celsius: bool,
) -> Result<(Vec<usize>, Vec<DataPoint>), SensorError> {
    // // Seems faster to split first, and only then do the parsing
    // // (possible reason: a search for a char is limited by the memory bandwidth
    // // and by doing it separately, we avoid trashing the cache?)
    // #[cfg(feature = "rayon")]
    // let mut lines: Vec<_> = content[pos..].par_split('\n').collect();
    // #[cfg(not(feature = "rayon"))]
    // let mut lines: Vec<_> = content[pos..].split('\n').collect();

    // if let Some(str) = lines.last() {
    //     if str.is_empty() {
    //         lines.pop();
    //     }
    // }
    // if lines.is_empty() {
    //     return Err(SensorError::from("No data"));
    // }

    #[cfg(feature = "rayon")]
    let iter = lines.into_par_iter();
    #[cfg(not(feature = "rayon"))]
    let iter = lines.into_iter();

    iter.map(|line| parse_line(line, as_celsius))
        .enumerate()
        .map(|(lineno, data)| {
            // +2 because of the header + we want the line numbers to start at 1
            let lineno = lineno + 2;
            match data {
                Ok(None) => Err(SensorError::from(format!(
                    "Unexpected empty line {}",
                    lineno
                ))),
                Ok(Some(data)) => Ok((lineno, data)),
                Err(err) => Err(line_error(lineno, err)),
            }
        })
        .try_fold(
            || (Vec::new(), Vec::new()),
            |(mut linenos, mut data), result| match result {
                Ok((lineno, datapoint)) => {
                    linenos.extend([lineno]);
                    data.extend([datapoint]);
                    Ok((linenos, data))
                }
                Err(err) => Err(err),
            },
        )
        .try_reduce(
            || (Vec::new(), Vec::new()),
            |(mut acc_lines, mut acc_data), (fold_lines, fold_data)| {
                acc_lines.extend(fold_lines);
                acc_data.extend(fold_data);
                Ok((acc_lines, acc_data))
            },
        )
}

fn second_pass(
    data_in: (Vec<usize>, Vec<DataPoint>),
    tz: impl chrono::TimeZone + Sync + Display,
) -> Result<(Vec<usize>, Vec<DataPoint>), SensorError> {
    // To resolve the ambiguous dates, combine the data points with one shifted
    // back by 1h, so we have date D and date D-1h. If D-1h is NOT ambiguous,
    // it means D in the first half of the ambiguous period (still in daylight
    // saving). If D-1h IS ambiguous, then D is in the second half (with D-1h
    // in the first half).
    //
    // If it wasn't for the parallel processing, we could just compare with
    // the datetime of the previous item (presumably properly converted). But
    // with parallelism, the previous item might be in another thread, and yet
    // to be processed.
    // That means D will be converting to Utc twice: once when resolving D's
    // ambiguity, then again when resolving D+1. But the ambiguous dates are
    // only 2h per year (~8,766h/y), i.e. 0.02% of the dates, so insignificant.

    let (lineno, data_v) = data_in;

    #[cfg(feature = "rayon")]
    let iter = data_v.par_iter();
    #[cfg(not(feature = "rayon"))]
    let iter = data_v.iter();

    const SKIP_AMOUNT: usize = 60; // 1h-worth of data
    let data = iter
        .enumerate()
        .map(|(i, &data)| {
            let datetime = match tz.from_local_datetime(
                &chrono::DateTime::from_timestamp(data.timestamp, 0)
                    .map(|d| d.naive_utc())
                    .ok_or("Invalid timestamp")?,
            ) {
                chrono::LocalResult::None => Err(SensorError::from(format!(
                    "failed to convert date from {tz} to Utc"
                ))),
                chrono::LocalResult::Single(date) => Ok(date),
                chrono::LocalResult::Ambiguous(min, max) => {
                    if i < SKIP_AMOUNT {
                        unimplemented!("File can't start when daylight saving ends");
                    } else {
                        let data_ago = data_v[i - SKIP_AMOUNT]; // safe since the enumerate value starts after skipping
                        let datetime_ago = tz.from_local_datetime(
                            &chrono::DateTime::from_timestamp(data_ago.timestamp, 0)
                                .map(|d| d.naive_utc())
                                .ok_or("Invalid timestamp")?,
                        );
                        match datetime_ago {
                            // If `data.datetime` is ambiguous, we are switching from
                            // DST to STD. If so, date_ago can't be switching from STD
                            // to DST, which is the only normal way to get a gap,
                            // unless we have a huge gap in the original data.
                            chrono::LocalResult::None => Err(SensorError::from("missing data")),
                            chrono::LocalResult::Single(_) => Ok(min),
                            chrono::LocalResult::Ambiguous(_, _) => Ok(max),
                        }
                    }
                }
            }
            .map_err(|err| line_error(lineno[i], err))?;
            Ok::<DataPoint, SensorError>(DataPoint {
                timestamp: datetime.with_timezone(&chrono::Utc).timestamp(),
                ..data
            })
        })
        .collect::<Result<Vec<_>, _>>()?;
    Ok((lineno, data))
}

fn third_pass(vec_data: (Vec<usize>, Vec<DataPoint>)) -> Result<Vec<DataPoint>, SensorError> {
    #[cfg(feature = "rayon")]
    let iter = vec_data.1.par_iter();
    #[cfg(not(feature = "rayon"))]
    let iter = vec_data.1.iter_mut();

    iter.skip(1).enumerate().try_for_each(|(i, v_data)| {
        let data_prev = vec_data.1[i];
        if (v_data.timestamp - data_prev.timestamp) != 60 {
            return Err(SensorError::from(format!(
                "missing data before line {}, change from {} to {}",
                vec_data.0[i + 1],
                DateTime::from_timestamp(data_prev.timestamp, 0).unwrap(),
                DateTime::from_timestamp(v_data.timestamp, 0).unwrap(),
            )));
        }
        Ok(())
    })?;

    Ok(vec_data.1)
}

fn parse_line(line: &str, as_celsius: bool) -> Result<Option<DataPoint>, SensorError> {
    if line.is_empty() {
        return Ok(None);
    }
    let mut record = line.split(',');

    let datetime_str = record.next().expect("No first split").trim_matches('"');
    let timestamp = parse_date(datetime_str)?.timestamp();

    let temperature = record
        .next()
        .ok_or(SensorError::from("missing temperature"))?
        .trim_matches('"')
        .parse()
        .map_err(|err| SensorError::from_source("invalid temperature", err))?;
    let temperature = if as_celsius {
        Celsius::new(temperature)
    } else {
        Fahrenheit::new(temperature).into()
    };
    #[cfg(feature = "humidity")]
    let humidity = record
        .next()
        .ok_or(SensorError::from("missing humidity"))?
        .trim_matches('"')
        .parse()
        .or_else(|err| Err(SensorError::from_source("missing humidity", err)))?;

    Ok(Some(DataPoint {
        timestamp,
        temperature,
        #[cfg(feature = "humidity")]
        humidity,
    }))
}

fn parse_date(datetime_str: &str) -> Result<chrono::DateTime<chrono::Utc>, SensorError> {
    let datetime = chrono::NaiveDateTime::parse_from_str(datetime_str, "%Y-%m-%d %H:%M")
        .map_err(|err| SensorError::from_source("failed to parse date", err))?;
    Ok(datetime.and_utc())
}

// pub fn ui() {
//     ui.heading("My egui Application");
//     ui.horizontal(|ui| {
//         ui.label("Your name: ");
//         ui.text_edit_singleline(&mut name);
//     });
//     ui.add(egui::Slider::new(&mut age, 0..=120).text("age"));
//     if ui.button("Click each year").clicked() {
//         age += 1;
//     }
//     ui.label(format!("Hello '{name}', age {age}"));
//     ui.image(egui::include_image!("ferris.png"));
// }
