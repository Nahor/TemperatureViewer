// spell-checker:words chrono datetime

#[derive(Copy, Clone, Debug, Default)]
pub struct Celsius(f32);
impl Celsius {
    pub fn new(v: f32) -> Celsius {
        Celsius(v)
    }
    pub fn value(self) -> f32 {
        self.0
    }
}

#[derive(Copy, Clone, Debug, Default)]
pub struct Fahrenheit(f32);
impl Fahrenheit {
    pub fn new(v: f32) -> Fahrenheit {
        Fahrenheit(v)
    }
    pub fn value(self) -> f32 {
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

#[derive(Copy, Clone, Debug, Default)]
pub struct DataPoint {
    // Seconds since Unix epoch
    // Use timestamps instead of DateTime and the like because they tend to
    // carry more info than we need. And given that we'll use millions of data
    // points, the cost would be significant.
    pub timestamp: i64,
    #[allow(unused)]
    pub temperature: Celsius,
    #[cfg(feature = "humidity")]
    #[allow(unused)]
    pub humidity: f32,
}
