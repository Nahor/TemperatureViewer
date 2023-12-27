// spell-checker:words chrono

use std;
use std::error::Error;
use std::fmt::Debug;
use std::io;
use std::num::ParseFloatError;
use std::sync::Arc;

use chrono;

#[derive(Debug, Clone)]
pub(crate) enum ErrorKind {
    None,
    Io(Arc<io::Error>),
    ParseFloatError(ParseFloatError),
    ParseChronoError(chrono::ParseError),
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
            ErrorKind::ParseChronoError(e) => Some(e),
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
                ErrorKind::ParseChronoError(err) => err.to_string(),
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
            ErrorKind::ParseChronoError(err) => {
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

impl<S: ToString> From<(S, ParseFloatError)> for SensorError {
    fn from((s, source): (S, ParseFloatError)) -> Self {
        Self {
            desc: Some(s.to_string()),
            source: ErrorKind::ParseFloatError(source),
        }
    }
}

impl<S: ToString> From<(S, chrono::ParseError)> for SensorError {
    fn from((s, source): (S, chrono::ParseError)) -> Self {
        Self {
            desc: Some(s.to_string()),
            source: ErrorKind::ParseChronoError(source),
        }
    }
}

impl<S: ToString> From<(S, SensorError)> for SensorError {
    fn from((s, source): (S, SensorError)) -> Self {
        Self {
            desc: Some(s.to_string() + ": " + &source.desc.unwrap_or("".to_string())),
            source: source.source,
        }
    }
}

impl<S: ToString> From<(S, &SensorError)> for SensorError {
    fn from((s, source): (S, &SensorError)) -> Self {
        Self {
            desc: Some(s.to_string() + ": " + source.desc.as_ref().unwrap_or(&"".to_string())),
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
