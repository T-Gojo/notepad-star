fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut arguments = std::env::args_os().skip(1);
    let directory = arguments
        .next()
        .ok_or("Expected one new package directory path.")?;
    if arguments.next().is_some() {
        return Err("Expected exactly one package directory path.".into());
    }
    let metadata = star_extensions::create_example(std::path::Path::new(&directory))?;
    serde_json::to_writer_pretty(std::io::stdout().lock(), &metadata)?;
    Ok(())
}
