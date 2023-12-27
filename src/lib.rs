// spell-checker:words chrono datetime eframe egui nahor

mod data;
mod error;

pub use data::*;
pub use error::SensorError;

#[cfg(feature = "rayon")]
use rayon::prelude::*;
use std::{cmp, fmt::Display, fs, str::FromStr};

pub fn parse(file: &str) -> Result<Vec<DataPoint>, SensorError> {
    let start = std::time::Instant::now();

    let content = fs::read_to_string(file)?;
    if content.is_empty() {
        println!("empty");
        return Err(SensorError::from("File empty"));
    }

    let pos = content[0..cmp::min(60, content.len())] // No need to search to whole file, the header is not long
        .find('\n')
        .ok_or(SensorError::from("No data"))?;
    let as_celsius = parse_header(&content[..pos])?;

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
    let data = first_pass(content, pos + 1, as_celsius)?;
    let mid1 = std::time::Instant::now();
    // Second pass converts the date from "fake UTC" to "real UTC" (UTC + TZ)
    let data = second_pass(data, tz)?;
    let mid2 = std::time::Instant::now();
    // Third pass check for date continuity (data at 1min interval)
    let data = third_pass(data)?;

    let end = std::time::Instant::now();

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
        "    First pass: {:.3?} ({:.0} lines/sec)",
        (mid1 - start),
        data.len() as f32 / (mid1 - start).as_secs_f32()
    );
    println!(
        "    Second pass: {:.3?} ({:.0} lines/sec)",
        (mid2 - mid1),
        data.len() as f32 / (mid2 - mid1).as_secs_f32()
    );
    println!(
        "    Third pass: {:.3?} ({:.0} lines/sec)",
        (end - mid2),
        data.len() as f32 / (end - mid2).as_secs_f32()
    );

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
    content: String,
    pos: usize,
    as_celsius: bool,
) -> Result<Vec<(usize, DataPoint)>, SensorError> {
    // Seems faster to split first, and only then do the parsing
    // (possible reason: a search for a char is limited by the memory bandwidth
    // and by doing it separately, we avoid trashing the cache?)
    #[cfg(feature = "rayon")]
    let mut lines: Vec<_> = content[pos..].par_split('\n').collect();
    #[cfg(not(feature = "rayon"))]
    let mut lines: Vec<_> = content[pos..].split('\n').collect();

    if let Some(str) = lines.last() {
        if str.is_empty() {
            lines.pop();
        }
    }
    if lines.is_empty() {
        return Err(SensorError::from("No data"));
    }

    #[cfg(feature = "rayon")]
    let iter = lines.into_par_iter();
    #[cfg(not(feature = "rayon"))]
    let iter = lines.into_iter();

    iter.map(|line| parse_line(line, as_celsius))
        .enumerate()
        .map(|(lineno, data)| {
            match data {
                Ok(None) => Err(SensorError::from(format!(
                    "Unexpected empty line {}",
                    lineno + 2 // "+2" because of header+starting at 1
                ))),
                Ok(Some(data)) => Ok((lineno, data.clone())),
                Err(err) => Err(SensorError::from((
                    format!("failed to parse line {}", lineno + 2), // "+2" because of header+starting at 1
                    err,
                ))),
            }
        })
        .collect()
}

fn second_pass(
    data: Vec<(usize, DataPoint)>,
    tz: impl chrono::TimeZone + Sync + Display,
) -> Result<Vec<(usize, DataPoint)>, SensorError> {
    // To resolve the ambiguous dates, combine the data points with one shifted
    // back by 1h, so we have date D and date D-1h. If D-1h is NOT ambiguous,
    // it means D in the first half of the ambiguous period (still in daylight
    // saving). If D-1h IS ambiguous, then D is in the second half (with D-1h
    // in the first half)
    //
    // To shift, we process/prepend 60 fake data points before chaining with
    // the real data.
    let start_datetime = data[0].1.datetime.naive_utc();
    let extra_data: Vec<_> = (1..=60)
        .rev()
        .map(|i| DataPoint {
            datetime: (start_datetime - chrono::Duration::minutes(i)).and_utc(),
            temperature: Celsius::new(0.0),
            #[cfg(feature = "humidity")]
            humidity: 0.0,
        })
        .collect();

    #[cfg(feature = "rayon")]
    let iter = data.par_iter().zip(
        extra_data
            .par_iter()
            .chain(data.par_iter().map(|(_, data)| data)),
    );
    #[cfg(not(feature = "rayon"))]
    let iter = data.iter().zip(extra_data.iter().chain(data.iter()));

    iter.map(|(&(lineno, data), &data_ago)| {
        let datetime = match tz.from_local_datetime(&data.datetime.naive_utc()) {
            chrono::LocalResult::None => Err(SensorError::from(format!(
                "failed to convert date from {tz} to Utc"
            ))),
            chrono::LocalResult::Single(date) => Ok(date),
            chrono::LocalResult::Ambiguous(min, max) => {
                // If it wasn't for the parallel processing, we could just
                // compare with the datetime of the previous item (presumably
                // properly converted). But with parallelism, the previous item
                // might be in another thread, and yet to be processed.
                // Moreover, this only applies to 0.02% of the dates (2 out of
                // ~8760h/y), ... so yeah, no worth the trouble
                let datetime_ago = tz.from_local_datetime(&data_ago.datetime.naive_utc());
                match datetime_ago {
                    // If `data.datetime` is ambiguous, we are switching from
                    // DST to STD. If so, date_ago can't be switching from STD
                    // to DST, unless we have a huge gap in the original data.
                    // So for now, just use the DST time, and we'll sort it out
                    // when checking for date continuity
                    chrono::LocalResult::None => Ok(min),
                    chrono::LocalResult::Single(_) => Ok(min),
                    chrono::LocalResult::Ambiguous(_, _) => Ok(max),
                }
            }
        }
        .or_else(|err| {
            Err(SensorError::from((
                format!("failed to parse line {}", lineno + 2), // "+2" because of header+starting at 1
                err,
            )))
        })?;
        Ok::<(usize, DataPoint), SensorError>((
            lineno,
            DataPoint {
                datetime: datetime.with_timezone(&chrono::Utc),
                ..data
            },
        ))
    })
    .collect()
}

fn third_pass(data: Vec<(usize, DataPoint)>) -> Result<Vec<DataPoint>, SensorError> {
    let start_datetime = data[0].1.datetime.naive_utc();
    let extra_data: Vec<_> = (1..=1)
        .rev()
        .map(|i| {
            (
                0,
                DataPoint {
                    datetime: (start_datetime - chrono::Duration::minutes(i)).and_utc(),
                    temperature: Celsius::new(0.0),
                    #[cfg(feature = "humidity")]
                    humidity: 0.0,
                },
            )
        })
        .collect();

    #[cfg(feature = "rayon")]
    let iter = data
        .par_iter()
        .zip(extra_data.par_iter().chain(data.par_iter()));
    #[cfg(not(feature = "rayon"))]
    let iter = data.iter().zip(extra_data.iter().chain(data.iter()));

    iter.map(|(&(lineno, data), &(_, data_ago))| {
        if (data.datetime.naive_utc() - data_ago.datetime.naive_utc())
            == chrono::Duration::minutes(1)
        {
            Ok(data)
        } else {
            Err(SensorError::from(format!(
                "missing data before line {}",
                lineno + 2 // "+2" because of header+starting at 1
            )))
        }
    })
    .collect()
}

fn parse_line(line: &str, as_celsius: bool) -> Result<Option<DataPoint>, SensorError> {
    if line.is_empty() {
        return Ok(None);
    }
    let mut record = line.split(',');

    let datetime_str = record.next().expect("No first split");
    let datetime_str = datetime_str.trim_matches('"');
    let datetime = parse_date(datetime_str)?;

    let temperature = record
        .next()
        .ok_or(SensorError::from("Missing temperature"))?
        .trim_matches('"')
        .parse()
        .or_else(|err| Err(SensorError::from(("invalid temperature", err))))?;
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
        .or_else(|err| Err(SensorError::from(("missing humidity", err))))?;

    Ok(Some(DataPoint {
        datetime,
        temperature,
        #[cfg(feature = "humidity")]
        humidity,
    }))
}

fn parse_date(datetime_str: &str) -> Result<chrono::DateTime<chrono::Utc>, SensorError> {
    let datetime = chrono::NaiveDateTime::parse_from_str(datetime_str, "%Y-%m-%d %H:%M")
        .or_else(|err| Err(SensorError::from(("failed to parse date", err))))?;
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
