fn main() {
    if let Err(err) = kasane_cli::run() {
        eprintln!("kasane-cli: {err}");
        std::process::exit(1);
    }
}
