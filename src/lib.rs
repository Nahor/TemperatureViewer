// spell-checker:words chrono datetime eframe egui nahor

mod data;
mod error;

pub use data::*;
pub use error::SensorError;

use chrono::TimeZone;
#[cfg(feature = "rayon")]
use rayon::prelude::*;
use std::{fs, str::FromStr};

pub fn parse(file: &str) -> Result<Vec<DataPoint>, SensorError> {
    let content = fs::read_to_string(file)?;
    if content.is_empty() {
        println!("empty");
        return Err(SensorError::from("File empty"));
    }

    let pos = content
        .find('\n')
        .ok_or_else(|| SensorError::from("No data in file"))?;
    let start = std::time::Instant::now();
    let header = &content[..pos];
    let content = &content[pos + 1..];

    let as_celsius;
    if header == r#""Timestamp","Temperature (°C)","Relative Humidity (%)""# {
        as_celsius = true;
    } else if header == r#""Timestamp","Temperature (°F)","Relative Humidity (%)""# {
        as_celsius = false;
    } else {
        return Err(format!("Invalid header '{header}'").into());
    }

    // TODO(nahor): how to get the timezone from Windows
    let tz = std::env::var("TZ").unwrap_or("America/Los_Angeles".to_string());
    println!("Timezone: {tz:?}");
    let tz = chrono_tz::Tz::from_str(&tz)?;

    // Seems faster to split first, and only then do the parsing
    // (possible reason: a search for a char is limited by the memory bandwidth
    // and by doing it separately, we avoid trashing the cache?)

    #[cfg(feature = "rayon")]
    let content: Vec<_> = content.par_split('\n').collect();
    #[cfg(feature = "rayon")]
    let iter = content.par_iter();
    #[cfg(not(feature = "rayon"))]
    let iter = content.split('\n');

    let data : Result<Vec<_>,_> = iter
        .map(|line| {
            parse_line(line, as_celsius)
        })
        .enumerate()
        .filter_map(|(lineno, data)|
            // Remove the `Option`, filtering the `None` cases
            // Note that since filter_map expects an `Option` of its own, this
            // is essentially change a `Result<Option<_>,_>` into a `Option<Result<_, _>>`
            match data {
                Ok(None) => None,
                Ok(Some(data)) => Some(Ok(data.clone())),
                // "+2" because of header+starting at 1
                Err(err) => Some(Err(SensorError::from((format!("Failed to parse line {}", lineno+2), err)))),
            })
        .collect();
    let data = data?;

    let extra_data = [DataPoint {
        datetime: chrono::NaiveDateTime::default().and_utc(),
        temperature: Celsius::new(0.0),
        #[cfg(feature = "humidity")]
        humidity: 0.0,
    }; 60];

    #[cfg(feature = "rayon")]
    let iter = data
        .par_iter()
        .zip(extra_data.par_iter().chain(data.par_iter()));
    #[cfg(not(feature = "rayon"))]
    let iter = data.iter().zip(extra_data.iter().chain(data.iter()));

    let data: Result<Vec<_>, _> = iter
        .map(|(&data, &data_ago)| {
            let datetime = match tz.from_local_datetime(&data.datetime.naive_utc()) {
                chrono::LocalResult::None => Err(SensorError::from("Failed to convert date")),
                chrono::LocalResult::Single(date) => Ok(date),
                chrono::LocalResult::Ambiguous(min, max) => {
                    let datetime_ago = tz.from_local_datetime(&data_ago.datetime.naive_utc());
                    match datetime_ago {
                        chrono::LocalResult::None => {
                            Err(SensorError::from("Failed to convert date"))
                        }
                        chrono::LocalResult::Single(_) => Ok(min),
                        chrono::LocalResult::Ambiguous(_, _) => Ok(max),
                    }
                }
            }?;
            Ok::<DataPoint, SensorError>(DataPoint {
                datetime: datetime.with_timezone(&chrono::Utc),
                ..data
            })
        })
        .collect();
    let data = data?;

    let end = std::time::Instant::now();
    let elapsed = end - start;
    println!("Lines: {}", data.len());
    println!(
        "Speed: {:.0} lines/sec (time: {:?})",
        data.len() as f32 / elapsed.as_secs_f32(),
        elapsed
    );

    Ok(data)
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
        .parse::<f64>()
        .or_else(|err| Err(SensorError::from(("Invalid temperature", err))))?;
    let temperature = if as_celsius {
        Celsius::new(temperature)
    } else {
        Fahrenheit::new(temperature).into()
    };
    #[cfg(feature = "humidity")]
    let humidity = record
        .next()
        .ok_or(SensorError::from("Missing humidity"))?
        .trim_matches('"')
        .parse()
        .or_else(|err| Err(SensorError::from(("Missing humidity", err))))?;

    Ok(Some(DataPoint {
        datetime,
        temperature,
        #[cfg(feature = "humidity")]
        humidity,
    }))
}

fn parse_date(datetime_str: &str) -> Result<chrono::DateTime<chrono::Utc>, SensorError> {
    let datetime = chrono::NaiveDateTime::parse_from_str(datetime_str, "%Y-%m-%d %H:%M")
        .or(Err(SensorError::from("Failed to parse date")))?;
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
