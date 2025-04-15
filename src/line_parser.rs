use chrono::{NaiveDate, NaiveDateTime};
use winnow::{
    Parser,
    ascii::{Uint, dec_uint, digit1, float, multispace0},
    combinator::{delimited, separated_pair, seq, trace},
    error::{StrContext, StrContextValue},
    token::rest,
};

use crate::{Celsius, DataPoint, Fahrenheit, SensorError};

const SEC_PER_MIN: i64 = 60;

pub fn parse_line(line: &[u8], as_celsius: bool) -> Result<Option<DataPoint>, SensorError> {
    Ok(parse_line_(as_celsius).parse(line)?)
}

pub fn parse_line_(
    as_celsius: bool,
) -> impl FnMut(&mut &[u8]) -> winnow::Result<Option<DataPoint>> {
    move |input: &mut &[u8]| {
        if input.is_empty() {
            return Ok(None);
        }

        #[allow(unused, reason = "humidity is a compilation feature")]
        trace(
            "parse_line",
            seq!(
                parse_quoted_date.map(|date| (date.and_utc().timestamp() / SEC_PER_MIN) as i32),
                _:(multispace0,',',multispace0),
                parse_temperature(as_celsius),
                _:(multispace0,',',multispace0),
                parse_humidity,
            )
            .map(|(minutes, temperature, humidity)| {
                Some(DataPoint {
                    minutes,
                    temperature,
                    #[cfg(feature = "humidity")]
                    humidity,
                })
            }),
        )
        .parse_next(input)
    }
}

fn parse_quoted_date(input: &mut &[u8]) -> winnow::Result<NaiveDateTime> {
    trace("parse_date", delimited('"', parse_unquoted_date, '"')).parse_next(input)
}

fn parse_unquoted_date(input: &mut &[u8]) -> winnow::Result<NaiveDateTime> {
    trace(
        "parse_unquoted_date",
        separated_pair(parse_ymd, ' ', parse_hm).map(|((year, month, day), (hour, minute))| {
            NaiveDate::from_ymd_opt(year as i32, month as u32, day as u32)
                .unwrap()
                .and_hms_opt(hour as u32, minute as u32, 0)
                .unwrap()
        }),
    )
    .parse_next(input)
}

fn parse_ymd(input: &mut &[u8]) -> winnow::Result<(u16, u8, u8)> {
    trace(
        "parse_ymd",
        seq!(
            dec_uint,
            _:"-",
            dec_uint_with_leading
                .verify(|v| (1..=12).contains(v))
                .context(StrContext::Label("month"))
                .context(StrContext::Expected(StrContextValue::Description("01..12"))),
            _:"-",
            dec_uint_with_leading
                .verify(|v| (1..=31).contains(v))
                .context(StrContext::Label("day"))
                .context(StrContext::Expected(StrContextValue::Description("01..31"))),
        ),
    )
    .parse_next(input)
}

fn parse_hm(input: &mut &[u8]) -> winnow::Result<(u8, u8)> {
    trace(
        "parse_hm",
        separated_pair(
            dec_uint_with_leading
                .verify(|v| (0..24).contains(v))
                .context(StrContext::Label("hour"))
                .context(StrContext::Expected(StrContextValue::Description("00..23"))),
            ":",
            dec_uint_with_leading
                .verify(|v| (0..60).contains(v))
                .context(StrContext::Label("minute"))
                .context(StrContext::Expected(StrContextValue::Description("00..59"))),
        ),
    )
    .parse_next(input)
}

// dec_uint doesn't handle numbers with leading `0`, e.g. `02` gets parsed as `0`
// (with `2` remaining)
fn dec_uint_with_leading<O>(input: &mut &[u8]) -> winnow::Result<O>
where
    O: Uint + std::str::FromStr,
{
    digit1.parse_to().parse_next(input)
}

fn parse_temperature(as_celsius: bool) -> impl FnMut(&mut &[u8]) -> winnow::Result<Celsius> {
    move |input: &mut &[u8]| {
        delimited('"', float, '"')
            .map(|temperature| {
                if as_celsius {
                    Celsius::new(temperature)
                } else {
                    Fahrenheit::new(temperature).into()
                }
            })
            .parse_next(input)
    }
}

fn parse_humidity(input: &mut &[u8]) -> winnow::Result<f32> {
    if cfg!(feature = "humidity") {
        delimited('"', float::<_, f32, _>, '"').parse_next(input)
    } else {
        rest.map(|_| 0.0f32).parse_next(input)
    }
}

#[cfg(test)]
#[allow(unused)]
mod test {
    use std::hint::black_box;
    use std::time::Instant;

    use super::*;

    #[test]
    fn test_parse_date_full() {
        let input = r#""2020-01-20 09:43""#;

        let date = parse_quoted_date.parse(input.as_bytes());
        assert!(date.is_ok());
    }

    #[test]
    fn test_parse_date_extra_char() {
        let input = r#""2020-01-20 09:43" foo"#;
        // sliced to skip the first \n, which we use so the actual error string
        // starts at the beginning of the line
        let err = &r#"
"2020-01-20 09:43" foo
                  ^
"#[1..];

        let e = parse_quoted_date.parse(input.as_bytes()).unwrap_err();
        assert_eq!(e.to_string(), err);
    }

    #[test]
    fn test_parse_date_wrong_month() {
        {
            let input = r#""2020-13-20 09:43""#;
            // sliced to skip the first \n, which we use so the actual error string
            // starts at the beginning of the line
            let err = &r#"
"2020-13-20 09:43"
      ^
invalid month
expected 01..12"#[1..];

            let e = parse_quoted_date.parse(input.as_bytes()).unwrap_err();
            assert_eq!(e.to_string(), err);
        }
        {
            let input = r#""2020-00-20 09:43""#;
            // sliced to skip the first \n, which we use so the actual error string
            // starts at the beginning of the line
            let err = &r#"
"2020-00-20 09:43"
      ^
invalid month
expected 01..12"#[1..];

            let e = parse_quoted_date.parse(input.as_bytes()).unwrap_err();
            assert_eq!(e.to_string(), err);
        }
    }

    #[test]
    fn test_parse_date_wrong_day() {
        {
            let input = r#""2020-01-0 09:43""#;
            // sliced to skip the first \n, which we use so the actual error string
            // starts at the beginning of the line
            let err = &r#"
"2020-01-0 09:43"
         ^
invalid day
expected 01..31"#[1..];

            let e = parse_quoted_date.parse(input.as_bytes()).unwrap_err();
            assert_eq!(e.to_string(), err);
        }
        {
            let input = r#""2020-01-32 09:43""#;
            // sliced to skip the first \n, which we use so the actual error string
            // starts at the beginning of the line
            let err = &r#"
"2020-01-32 09:43"
         ^
invalid day
expected 01..31"#[1..];

            let e = parse_quoted_date.parse(input.as_bytes()).unwrap_err();
            assert_eq!(e.to_string(), err);
        }
    }

    #[test]
    fn test_parse_date_wrong_hour() {
        {
            let input = r#""2020-01-20 -01:43""#;
            // sliced to skip the first \n, which we use so the actual error string
            // starts at the beginning of the line
            let err = &r#"
"2020-01-20 -01:43"
            ^
invalid hour
expected 00..23"#[1..];

            let e = parse_quoted_date.parse(input.as_bytes()).unwrap_err();
            assert_eq!(e.to_string(), err);
        }
        {
            let input = r#""2020-01-20 24:43""#;
            // sliced to skip the first \n, which we use so the actual error string
            // starts at the beginning of the line
            let err = &r#"
"2020-01-20 24:43"
            ^
invalid hour
expected 00..23"#[1..];

            let e = parse_quoted_date.parse(input.as_bytes()).unwrap_err();
            assert_eq!(e.to_string(), err);
        }
    }

    #[test]
    fn test_parse_date_wrong_minute() {
        {
            let input = r#""2020-01-20 09:-01""#;
            // sliced to skip the first \n, which we use so the actual error string
            // starts at the beginning of the line
            let err = &r#"
"2020-01-20 09:-01"
               ^
invalid minute
expected 00..59"#[1..];

            let e = parse_quoted_date.parse(input.as_bytes()).unwrap_err();
            assert_eq!(e.to_string(), err);
        }
        {
            let input = r#""2020-01-20 09:60""#;
            // sliced to skip the first \n, which we use so the actual error string
            // starts at the beginning of the line
            let err = &r#"
"2020-01-20 09:60"
               ^
invalid minute
expected 00..59"#[1..];

            let e = parse_quoted_date.parse(input.as_bytes()).unwrap_err();
            assert_eq!(e.to_string(), err);
        }
    }

    #[test]
    fn test_bench() {
        let input = r#"2020-01-20 09:43"#;

        {
            let s = Instant::now();
            let mut count = 0;
            let duration = loop {
                const LOOP_COUNT: u64 = 1000;
                for _ in 0..LOOP_COUNT {
                    let _ = black_box(
                        chrono::NaiveDateTime::parse_from_str(input, "%Y-%m-%d %H:%M").unwrap(),
                    );
                }
                count += LOOP_COUNT;

                let duration = s.elapsed();
                if duration >= std::time::Duration::from_secs(5) {
                    break duration;
                }
            };
            println!(
                "chrono speed: {:}/s,  {:.3}ns",
                count / duration.as_secs(),
                duration.as_secs_f64() * 1_000_000_000.0 / count as f64
            );
        }

        {
            let s = Instant::now();
            let mut count = 0;
            let duration = loop {
                const LOOP_COUNT: u64 = 1000;
                for _ in 0..LOOP_COUNT {
                    let _ = black_box(parse_unquoted_date.parse(input.as_bytes()).unwrap());
                }
                count += LOOP_COUNT;

                let duration = s.elapsed();
                if duration >= std::time::Duration::from_secs(5) {
                    break duration;
                }
            };
            println!(
                "parse speed: {:}/s,  {:.3}ns",
                count / duration.as_secs(),
                duration.as_secs_f64() * 1_000_000_000.0 / count as f64
            );
        }
    }
}
