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
    // Minutes since Unix epoch
    // Use minutes instead of DateTime and the like because they tend to carry
    // more precision than we need (seconds, nanoseconds) or duplicated data
    // (timezone offset).
    // Similarly, we only need a i32 which still gives us +/-4000 years.
    // With potentially millions of data points, the saving is significant both
    // in total memory usage, and in speed (cache usage)
    pub minutes: i32,
    #[allow(unused)]
    pub temperature: Celsius,
    #[cfg(feature = "humidity")]
    #[allow(unused)]
    pub humidity: f32,
}
