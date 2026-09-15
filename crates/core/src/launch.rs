use std::{ffi::OsString, path::PathBuf};

pub const HELP: &str = "Notepad Star\n\
Usage: notepad-star [options] [--] [files...]\n\
  --preview             Open the welcome document\n\
  --read-only, -ro       Open documents read-only\n\
  --no-session, -nosession  Do not restore or save the prior clean session\n\
  --reuse-instance      Open files in an existing opt-in shared instance\n\
  --new-instance, -multiInst  Keep this launch independent (the default)\n\
  --profile-dir PATH    Use an explicit application-data directory\n\
  --line N, -nN         Go to a 1-based line in the active document\n\
  --column N, -cN       Go to a 1-based column\n\
  --language NAME, -lNAME  Select a language profile\n\
  --help                Print this help\n\
  --version             Print the application version and build channel\n\
Private validation options: --smoke-test, --screenshot PATH\n";

#[derive(Default, Debug)]
pub struct Options {
    pub files: Vec<PathBuf>,
    pub preview: bool,
    pub smoke_test: bool,
    pub screenshot: String,
    pub read_only: bool,
    pub no_session: bool,
    pub line: u64,
    pub column: u64,
    pub language: String,
    pub help: bool,
    pub version: bool,
    pub reuse_instance: bool,
    pub profile: Option<PathBuf>,
}

fn position(value: &str) -> Result<u64, String> {
    let result = value
        .parse::<u64>()
        .map_err(|_| "Line/column must be a positive integer.")?;
    if result == 0 || result > 1_000_000_000 {
        return Err("Line/column must be between 1 and 1,000,000,000.".into());
    }
    Ok(result)
}

pub fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Options, String> {
    let mut result = Options::default();
    let mut arguments = arguments.into_iter();
    let mut paths_only = false;
    let mut instance_mode = None;
    while let Some(argument) = arguments.next() {
        if paths_only {
            result.files.push(argument.into());
            continue;
        }
        match argument.to_str() {
            Some("--") => paths_only = true,
            Some("--help") => result.help = true,
            Some("--version") => result.version = true,
            Some("--preview") => result.preview = true,
            Some("--smoke-test") => result.smoke_test = true,
            Some("--read-only" | "-ro") => result.read_only = true,
            Some("--no-session" | "-nosession") => result.no_session = true,
            Some("--profile-dir") => {
                let path: PathBuf = arguments
                    .next()
                    .ok_or("--profile-dir requires a directory.")?
                    .into();
                if path.as_os_str().is_empty() {
                    return Err("Profile directory must not be empty.".into());
                }
                result.profile = Some(std::path::absolute(path).map_err(|e| e.to_string())?);
            }
            Some(flag @ ("--reuse-instance" | "--new-instance" | "-multiInst")) => {
                let reuse = flag == "--reuse-instance";
                if instance_mode.is_some_and(|previous| previous != reuse) {
                    return Err("Choose either reuse-instance or new-instance, not both.".into());
                }
                instance_mode = Some(reuse);
                result.reuse_instance = reuse;
            }
            Some(flag @ ("--line" | "--column" | "--language" | "--screenshot")) => {
                let value = arguments
                    .next()
                    .ok_or_else(|| format!("{flag} requires a value."))?
                    .into_string()
                    .map_err(|_| format!("{flag} requires Unicode text."))?;
                match flag {
                    "--line" => result.line = position(&value)?,
                    "--column" => result.column = position(&value)?,
                    "--language" => result.language = value,
                    _ => result.screenshot = value,
                }
            }
            Some(flag) if flag.starts_with("-n") && flag.len() > 2 => {
                result.line = position(&flag[2..])?
            }
            Some(flag) if flag.starts_with("-c") && flag.len() > 2 => {
                result.column = position(&flag[2..])?
            }
            Some(flag) if flag.starts_with("-l") && flag.len() > 2 => {
                result.language = flag[2..].into()
            }
            Some(flag) if flag.starts_with('-') => {
                return Err(format!(
                    "Unknown option {flag}. Use -- before filenames beginning with a hyphen."
                ))
            }
            _ => result.files.push(argument.into()),
        }
    }
    if result.files.len() > 64 || result.language.len() > 128 || result.language.contains('\0') {
        return Err("Too many files or an invalid language option.".into());
    }
    if result.reuse_instance
        && (result.preview
            || result.smoke_test
            || result.no_session
            || !result.screenshot.is_empty())
    {
        return Err("Shared-instance launches cannot use preview, validation, screenshot or no-session options.".into());
    }
    if result.reuse_instance
        && result.files.is_empty()
        && (result.read_only
            || result.line != 0
            || result.column != 0
            || !result.language.is_empty())
    {
        return Err("File-specific options require a file when reusing an instance.".into());
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn parses_compatible_positions_without_losing_file_paths() {
        let options = parse(
            [
                "-n12",
                "--column",
                "4",
                "-lpython",
                "-ro",
                "-nosession",
                "--",
                "-file.txt",
            ]
            .map(OsString::from),
        )
        .unwrap();
        assert_eq!((options.line, options.column), (12, 4));
        assert_eq!(options.language, "python");
        assert!(options.read_only && options.no_session);
        assert_eq!(options.files, [PathBuf::from("-file.txt")]);
        assert!(parse([OsString::from("--line")]).is_err());
        assert!(parse([OsString::from("-n0")]).is_err());
        assert!(parse([OsString::from("--unknown")]).is_err());
    }
}
