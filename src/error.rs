// spell-checker:words chrono

use std::error::Error;
use std::fmt::Debug;
use std::io;
use std::num::ParseFloatError;
use std::str::Utf8Error;
use std::sync::Arc;

#[derive(Debug, Clone)]
pub(crate) enum ErrorKind {
    None,
    Io(Arc<io::Error>),
    ParseFloatError(ParseFloatError),
    ChronoParseError(chrono::ParseError),
    ChronoTzParseError(chrono_tz::ParseError),
    Utf8(Utf8Error),
}

pub trait FromSource<S: ToString, E: Error> {
    fn from_source(desc: S, err: E) -> SensorError;
}

pub struct SensorError {
    pub(crate) desc: Option<String>,
    pub(crate) source: ErrorKind,
}

impl Error for SensorError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match &self.source {
            ErrorKind::None => None,
            ErrorKind::Io(e) => Some(e),
            ErrorKind::ParseFloatError(e) => Some(e),
            ErrorKind::ChronoParseError(e) => Some(e),
            ErrorKind::ChronoTzParseError(e) => Some(e),
            ErrorKind::Utf8(e) => Some(e),
        }
    }
}

impl std::fmt::Display for SensorError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}{}",
            self.desc.as_ref().unwrap_or(&"".to_string()),
            match &self.source {
                ErrorKind::None => "".to_string(),
                ErrorKind::Io(err) => err.to_string(),
                ErrorKind::ParseFloatError(err) => err.to_string(),
                ErrorKind::ChronoParseError(err) => err.to_string(),
                ErrorKind::ChronoTzParseError(err) => err.to_string(),
                ErrorKind::Utf8(err) => err.to_string(),
            },
        )
    }
}
impl std::fmt::Debug for SensorError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut se = f.debug_struct("SensorError");
        if let Some(desc) = &self.desc {
            se.field("desc", &desc);
        }
        match &self.source {
            ErrorKind::None => (),
            ErrorKind::Io(err) => {
                se.field("source", &err);
            }
            ErrorKind::ParseFloatError(err) => {
                se.field("source", &err);
            }
            ErrorKind::ChronoParseError(err) => {
                se.field("source", &err);
            }
            ErrorKind::ChronoTzParseError(err) => {
                se.field("source", &err);
            }
            ErrorKind::Utf8(err) => {
                se.field("source", &err);
            }
        }
        se.finish()
    }
}

impl From<io::Error> for SensorError {
    fn from(source: io::Error) -> Self {
        Self {
            desc: None,
            source: ErrorKind::Io(Arc::new(source)),
        }
    }
}

impl From<ParseFloatError> for SensorError {
    fn from(source: ParseFloatError) -> Self {
        Self {
            desc: None,
            source: ErrorKind::ParseFloatError(source),
        }
    }
}

impl From<chrono_tz::ParseError> for SensorError {
    fn from(source: chrono_tz::ParseError) -> Self {
        Self {
            desc: None,
            source: ErrorKind::ChronoTzParseError(source),
        }
    }
}

impl<S: ToString> FromSource<S, ParseFloatError> for SensorError {
    fn from_source(desc: S, source: ParseFloatError) -> SensorError {
        Self {
            desc: Some(desc.to_string()),
            source: ErrorKind::ParseFloatError(source),
        }
    }
}

impl<S: ToString> FromSource<S, chrono::ParseError> for SensorError {
    fn from_source(desc: S, source: chrono::ParseError) -> SensorError {
        Self {
            desc: Some(desc.to_string()),
            source: ErrorKind::ChronoParseError(source),
        }
    }
}

impl<S: ToString> FromSource<S, Utf8Error> for SensorError {
    fn from_source(desc: S, source: Utf8Error) -> SensorError {
        Self {
            desc: Some(desc.to_string()),
            source: ErrorKind::Utf8(source),
        }
    }
}

impl<S: ToString> FromSource<S, SensorError> for SensorError {
    fn from_source(desc: S, source: SensorError) -> SensorError {
        Self {
            desc: Some(desc.to_string() + ": " + &source.desc.unwrap_or("".to_string())),
            source: source.source,
        }
    }
}

impl<S: ToString> FromSource<S, &SensorError> for SensorError {
    fn from_source(desc: S, source: &SensorError) -> SensorError {
        Self {
            desc: Some(desc.to_string() + ": " + source.desc.as_ref().unwrap_or(&"".to_string())),
            source: source.source.clone(),
        }
    }
}

impl From<&str> for SensorError {
    fn from(s: &str) -> Self {
        Self {
            desc: Some(s.to_string()),
            source: ErrorKind::None,
        }
    }
}

impl From<String> for SensorError {
    fn from(str: String) -> Self {
        Self {
            desc: Some(str),
            source: ErrorKind::None,
        }
    }
}
