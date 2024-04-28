// spell-checker:words chrono datetime eframe egui nahor
//#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")] // hide console window on Windows in release

// use eframe::egui;
use sensor::SensorError;

//impl std::error::Error for eframe::Error{};

#[cfg(feature = "humidity")]
fn save(data: &Vec<sensor::DataPoint>) -> std::io::Result<()> {
    let mut file = std::io::BufWriter::with_capacity(
        1024 * 1024,
        std::fs::File::create(r#"C:\msys64\home\Nahor\Downloads\sense.csv"#)?,
    );

    std::io::Write::write_all(
        &mut file,
        "\"Timestamp\",\"Temperature (°F)\",\"Relative Humidity (%)\"\n".as_bytes(),
    )?;

    // TODO(nahor): Parallelize (see gen_file)
    let iter = data.iter();
    iter.try_for_each(|&data| {
        let s = format!(
            concat!(r#""{}","{:.04}","{:.04}""#, '\n'),
            chrono::DateTime::from_timestamp(data.timestamp, 0)
                .unwrap()
                .with_timezone(&chrono_tz::America::Los_Angeles)
                .format("%Y-%m-%d %H:%M"),
            Into::<sensor::Fahrenheit>::into(data.temperature).value(),
            data.humidity
        );
        std::io::Write::write_all(&mut file, s.as_bytes())
    })?;
    std::io::Write::flush(&mut file)
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // env_logger::init(); // Log to stderr (if you run with `RUST_LOG=debug`).

    let mut args = std::env::args();
    if args.len() != 2 {
        return Err(Box::new(SensorError::from("Not enough arguments")));
    }
    let file = args.nth(1).unwrap();

    #[allow(unused_variables)]
    let data = sensor::parse(file.as_str())?;
    #[cfg(feature = "humidity")]
    save(&data)?;

    // let options = eframe::NativeOptions {
    //     viewport: egui::ViewportBuilder::default()
    //         .with_inner_size([1920.0, 1080.0])
    //         .with_min_inner_size([800.0, 600.0]),
    //     ..Default::default()
    // };

    // Ok(eframe::run_native(
    //     "Sensor",
    //     options,
    //     Box::new(|_cc| {
    //         // This gives us image support:
    //         //egui_extras::install_image_loaders(&cc.egui_ctx);

    //         _cc.egui_ctx.set_visuals(egui::Visuals::dark());

    //         Box::<MyApp>::default()
    //     }),
    // )?)
    Ok(())
}

// struct MyApp {
//     name: String,
//     age: u32,
// }

// impl Default for MyApp {
//     fn default() -> Self {
//         Self {
//             name: "Arthur".to_owned(),
//             age: 42,
//         }
//     }
// }

// impl eframe::App for MyApp {
//     fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
//         egui::CentralPanel::default().show(ctx, |ui| {
//             ui.heading("My egui Application");
//             ui.horizontal(|ui| {
//                 let name_label = ui.label("Your name: ");
//                 ui.text_edit_singleline(&mut self.name)
//                     .labelled_by(name_label.id);
//             });
//             ui.add(egui::Slider::new(&mut self.age, 0..=120).text("age"));
//             if ui.button("Click each year").clicked() {
//                 self.age += 1;
//             }
//             ui.label(format!("Hello '{}', age {}", self.name, self.age));

//             //egui::widgets::global_dark_light_mode_buttons(ui);

//             // ui.image(egui::include_image!(
//             //     "../../../crates/egui/assets/ferris.png"
//             // ));
//         });
//     }
// }
