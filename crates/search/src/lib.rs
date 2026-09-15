#![forbid(unsafe_code)]
use serde::{Deserialize, Serialize};

pub const MAX_TEXT: usize = 32 * 1024 * 1024;
pub const MAX_PROTOCOL: usize = 128 * 1024 * 1024;
pub const MAX_MATCHES: usize = 10000;
pub mod batch_replace;
pub mod outline;

#[derive(Serialize, Deserialize)]
pub struct FileRequest {
    pub root: star_io::NativePath,
    pub patterns: String,
}
#[derive(Serialize, Deserialize)]
pub struct FileResponse {
    pub files: Vec<star_io::NativePath>,
    pub warnings: Vec<String>,
}
pub fn list_files(request: FileRequest) -> Result<FileResponse, String> {
    let root = request.root.to_path()?;
    if !root.is_dir() {
        return Err("Choose an existing directory.".into());
    }
    if request.patterns.len() > 1024 {
        return Err("File filters exceed their limit.".into());
    }
    let patterns = if request.patterns.trim().is_empty() {
        "*"
    } else {
        &request.patterns
    };
    let mut filters = globset::GlobSetBuilder::new();
    for pattern in patterns.split_whitespace() {
        filters.add(globset::Glob::new(pattern).map_err(|e| format!("Invalid file pattern: {e}"))?);
    }
    let filters = filters.build().map_err(|e| e.to_string())?;
    let mut result = FileResponse {
        files: Vec::new(),
        warnings: Vec::new(),
    };
    for (index, entry) in walkdir::WalkDir::new(root)
        .follow_links(false)
        .into_iter()
        .enumerate()
    {
        if index >= 10000 {
            return Err("Directory scan exceeds 10,000 entries. Choose a narrower folder.".into());
        }
        match entry {
            Ok(entry) if entry.file_type().is_file() && filters.is_match(entry.file_name()) => {
                if result.files.len() >= 1000 {
                    return Err("More than 1,000 matching files. Narrow the filter.".into());
                }
                result
                    .files
                    .push(star_io::NativePath::from_path(entry.path()));
            }
            Ok(_) => {}
            Err(error) => result.warnings.push(error.to_string()),
        }
    }
    Ok(result)
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Request {
    pub text: String,
    pub query: String,
    pub replacement: String,
    pub mode: String,
    pub match_case: bool,
    pub whole_word: bool,
    pub dot_newline: bool,
    pub replace: bool,
    #[serde(default)]
    pub start: u64,
    #[serde(default)]
    pub end: Option<u64>,
    #[serde(default)]
    pub replace_limit: u32,
    #[serde(default)]
    pub file: Option<star_io::NativePath>,
    #[serde(default)]
    pub file_encoding: Option<star_io::Encoding>,
    #[serde(skip)]
    pub source_stamp: Option<String>,
    #[serde(default)]
    pub reverse: bool,
    #[serde(default)]
    pub first_only: bool,
    #[serde(default)]
    pub wrap: bool,
    #[serde(default)]
    pub require_full_range: bool,
}

impl Request {
    pub fn prepare(mut self) -> Result<Self, String> {
        if let Some(path) = &self.file {
            let bytes = star_io::read_limited(&path.to_path()?, MAX_TEXT)?;
            self.source_stamp =
                Some(serde_json::to_string(&star_io::stamp(&bytes)).map_err(|e| e.to_string())?);
            let (text, encoding) = star_io::decode(&bytes, self.file_encoding)?;
            self.text = text;
            self.file_encoding = Some(encoding);
        }
        if self.text.len() > MAX_TEXT || self.query.len() > 4096 || self.replacement.len() > 65536 {
            return Err(
                "Search request exceeds the document, pattern or replacement limit.".into(),
            );
        }
        match self.mode.as_str() {
            "normal" | "regex" => {}
            "extended" => {
                self.query = extended(&self.query)?;
                self.replacement = extended(&self.replacement)?;
                self.mode = "normal".into();
            }
            _ => return Err("Unknown search mode.".into()),
        }
        if self.query.is_empty() {
            return Err("Enter a non-empty search pattern.".into());
        }
        let end = self.end.unwrap_or(self.text.len() as u64);
        if ((!self.reverse && self.start > end) || (self.reverse && self.start < end))
            || end > self.text.len() as u64
            || self.start > self.text.len() as u64
            || !self.text.is_char_boundary(self.start as usize)
            || !self.text.is_char_boundary(end as usize)
            || self.replace_limit > MAX_MATCHES as u32
        {
            return Err("Invalid search range or replacement limit.".into());
        }
        if self.reverse && (!self.first_only || self.replace) {
            return Err("Reverse search is supported for single-match navigation only.".into());
        }
        if self.first_only && self.replace {
            return Err("Navigation and replacement flags cannot be combined.".into());
        }
        if self.text.contains('\0') || self.query.contains('\0') || self.replacement.contains('\0')
        {
            return Err("NUL-containing searches are not supported by this text editor.".into());
        }
        Ok(self)
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Match {
    pub start: u64,
    pub end: u64,
    pub line: u64,
    pub preview: String,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Response {
    pub matches: Vec<Match>,
    pub count: u64,
    pub replacement_text: Option<String>,
    #[serde(default)]
    pub source: Option<String>,
    #[serde(default)]
    pub source_stamp: Option<String>,
    #[serde(default)]
    pub source_encoding: Option<star_io::Encoding>,
    #[serde(default)]
    pub next_position: Option<u64>,
}

impl Response {
    pub fn validate(&self, source: &str) -> Result<(), String> {
        if self.source.is_some() && (self.source_encoding.is_none() || self.source_stamp.is_none())
        {
            return Err("File search response omitted its encoding or source stamp.".into());
        }
        if self.matches.len() > MAX_MATCHES || self.count > MAX_MATCHES as u64 {
            return Err("Worker returned too many matches.".into());
        }
        if let Some(text) = &self.replacement_text {
            if text.len() > MAX_TEXT || text.contains('\0') {
                return Err("Invalid replacement output.".into());
            }
            let position = self
                .next_position
                .ok_or("Replacement response has no cursor position")?;
            if position > text.len() as u64 || !text.is_char_boundary(position as usize) {
                return Err("Invalid replacement cursor position.".into());
            }
        } else {
            let mut previous = 0;
            for hit in &self.matches {
                let start = usize::try_from(hit.start).map_err(|_| "Invalid result offset")?;
                let end = usize::try_from(hit.end).map_err(|_| "Invalid result offset")?;
                if start < previous
                    || start > end
                    || end > source.len()
                    || !source.is_char_boundary(start)
                    || !source.is_char_boundary(end)
                    || hit.preview.len() > 2048
                {
                    return Err("Worker returned an invalid text range.".into());
                }
                previous = end;
            }
        }
        Ok(())
    }
}

pub fn extended(value: &str) -> Result<String, String> {
    let mut result = String::new();
    let mut chars = value.chars();
    while let Some(c) = chars.next() {
        if c != '\\' {
            result.push(c);
            continue;
        }
        match chars.next().ok_or("Trailing escape in extended search")? {
            'n' => result.push('\n'),
            'r' => result.push('\r'),
            't' => result.push('\t'),
            '\\' => result.push('\\'),
            'b' => result.push('\u{8}'),
            'f' => result.push('\u{c}'),
            'v' => result.push('\u{b}'),
            kind @ ('x' | 'u') => {
                let digits = if kind == 'x' { 2 } else { 4 };
                let mut code = 0;
                for _ in 0..digits {
                    code = code * 16
                        + chars
                            .next()
                            .and_then(|c| c.to_digit(16))
                            .ok_or("Invalid hexadecimal escape")?;
                }
                result.push(char::from_u32(code).ok_or("Invalid Unicode escape")?);
            }
            _ => return Err("Unsupported extended-search escape.".into()),
        }
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn extended_search_and_budgets() {
        assert_eq!(extended(r"a\r\n\t\u65e5\x21\\").unwrap(), "a\r\n\t日!\\");
        assert!(extended(r"\uD800").is_err());
        assert!(extended(r"\xZ0").is_err());
        assert!(extended("\\").is_err());
        let request = Request {
            text: String::new(),
            query: String::new(),
            replacement: String::new(),
            mode: "normal".into(),
            match_case: false,
            whole_word: false,
            dot_newline: false,
            replace: false,
            start: 0,
            end: None,
            replace_limit: 0,
            file: None,
            file_encoding: None,
            source_stamp: None,
            reverse: false,
            first_only: false,
            wrap: false,
            require_full_range: false,
        };
        assert!(request.prepare().is_err());
    }
    #[test]
    fn validates_utf8_boundaries_and_lengths() {
        let mut response = Response {
            matches: vec![Match {
                start: 1,
                end: 2,
                line: 1,
                preview: "é".into(),
            }],
            count: 1,
            replacement_text: None,
            source: None,
            source_stamp: None,
            next_position: None,
            source_encoding: None,
        };
        assert!(response.validate("é").is_err());
        response.matches[0].start = 0;
        assert!(response.validate("é").is_ok());
        response.matches[0].end = 999;
        assert!(response.validate("é").is_err());
    }
    #[test]
    fn directory_filters_do_not_include_other_files() {
        let root = tempfile::tempdir().unwrap();
        std::fs::write(root.path().join("one.rs"), "match").unwrap();
        std::fs::write(root.path().join("two.txt"), "ignore").unwrap();
        let response = list_files(FileRequest {
            root: star_io::NativePath::from_path(root.path()),
            patterns: "*.rs".into(),
        })
        .unwrap();
        assert_eq!(response.files.len(), 1);
        assert!(response.files[0].to_path().unwrap().ends_with("one.rs"));
    }
}
