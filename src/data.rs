// spell-checker:words chrono datetime

#[derive(Copy, Clone, Debug)]
pub struct Celsius(f64);
impl Celsius {
    pub fn new(v: f64) -> Celsius {
        Celsius(v)
    }
    pub fn value(self) -> f64 {
        self.0
    }
}

#[derive(Copy, Clone, Debug)]
pub struct Fahrenheit(f64);
impl Fahrenheit {
    pub fn new(v: f64) -> Fahrenheit {
        Fahrenheit(v)
    }
    pub fn value(self) -> f64 {
        self.0
    }
}

impl From<Celsius> for Fahrenheit {
    fn from(value: Celsius) -> Self {
        Fahrenheit(value.0 * 9.0 / 5.0 + 32.0)
    }
}
impl From<Fahrenheit> for Celsius {
    fn from(value: Fahrenheit) -> Self {
        Celsius((value.0 - 32.0) * 5.0 / 9.0)
    }
}

#[derive(Copy, Clone, Debug)]
pub struct DataPoint {
    pub timestamp: i64, // seconds since Unix epoch
    #[allow(unused)]
    pub temperature: Celsius,
    #[cfg(feature = "humidity")]
    #[allow(unused)]
    pub humidity: f64,
}
