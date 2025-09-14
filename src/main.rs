// spell-checker:words chrono datetime eframe egui nahor
//#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")] // hide console window on Windows in release

use std::path::PathBuf;

// use eframe::egui;
use clap::{
    CommandFactory, Parser, Subcommand,
    builder::{
        Styles,
        styling::{AnsiColor, Effects},
    },
    value_parser,
};
use clap_complete::{Shell, generate};
use sensor::SensorError;

//impl std::error::Error for eframe::Error{};

/// Show various graphs from a SensorPush temperature sensor.
#[derive(Parser, Debug)]
#[command(version, about, long_about = None, styles=styles())]
struct Args {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Debug, Subcommand)]
enum Commands {
    /// Process SensorPush data in CSV format (decompressed exported data)
    Csv {
        /// Decompressed SensorPush data file (.csv)
        #[arg(value_hint = clap::ValueHint::AnyPath)]
        file: PathBuf,
    },

    /// Process SensorPush data in Zip format (compressed exported data)
    Zip {
        /// Compressed SensorPush data file (.zip)
        #[arg(value_hint = clap::ValueHint::AnyPath)]
        file: PathBuf,
    },

    /// Generate tab-completion scripts for your shell
    Completions {
        #[arg(value_parser = value_parser!(Shell))]
        shell: Shell,
    },
}

fn styles() -> Styles {
    Styles::styled()
        .header(AnsiColor::BrightGreen.on_default() | Effects::BOLD | Effects::UNDERLINE)
        .usage(AnsiColor::Yellow.on_default() | Effects::BOLD | Effects::UNDERLINE)
        .literal(AnsiColor::BrightCyan.on_default() | Effects::BOLD)
        .placeholder(AnsiColor::Cyan.on_default())
        .error(AnsiColor::BrightRed.on_default() | Effects::BOLD)
        .valid(AnsiColor::BrightGreen.on_default() | Effects::BOLD)
        .invalid(AnsiColor::BrightYellow.on_default() | Effects::BOLD)
}

fn main() -> Result<(), SensorError> {
    let args = Args::parse();
    match args.command {
        Commands::Csv { file } => {
            let _ = sensor::parse_csv(file)?;
        }
        Commands::Zip { file } => {
            let _ = sensor::parse_zip(file)?;
        }
        Commands::Completions { shell } => {
            let mut cmd = Args::command();
            let name = cmd.get_name().to_owned();
            generate(shell, &mut cmd, name, &mut std::io::stdout());
        }
    }
    Ok(())
}
