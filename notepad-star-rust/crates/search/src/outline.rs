use serde::{Deserialize, Serialize};

pub const MAX_SOURCE: usize = 4 * 1024 * 1024;
pub const MAX_PROTOCOL: usize = 32 * 1024 * 1024;

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Request {
    pub text: String,
    pub parser: String,
}
impl Request {
    pub fn validate(&self) -> Result<(), String> {
        if self.text.len() > MAX_SOURCE || self.text.contains('\0') {
            return Err(
                "Function List supports at most 4 MiB of UTF-8 text without NUL characters.".into(),
            );
        }
        if self.parser.is_empty()
            || self.parser.len() > 128
            || !self
                .parser
                .chars()
                .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '-' | '_'))
        {
            return Err("Invalid function-list parser identifier.".into());
        }
        Ok(())
    }
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Symbol {
    pub name: String,
    pub group: String,
    pub start: u64,
    pub end: u64,
    pub line: u64,
    pub group_start: i64,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Response {
    pub symbols: Vec<Symbol>,
}
impl Response {
    pub fn validate(&self, source: &str) -> Result<(), String> {
        if self.symbols.len() > 10000 {
            return Err("Too many function-list entries.".into());
        }
        let mut names = 0;
        for item in &self.symbols {
            if item.start >= item.end
                || item.end > source.len() as u64
                || !source.is_char_boundary(item.start as usize)
                || !source.is_char_boundary(item.end as usize)
                || item.name.is_empty()
                || item.name.len() > 8192
                || item.group.len() > 8192
                || item.group_start < -1
                || item.group_start > source.len() as i64
            {
                return Err("Function-list worker returned an invalid range or label.".into());
            }
            if item.group_start >= 0 && !source.is_char_boundary(item.group_start as usize) {
                return Err("Function-list group position splits a UTF-8 character.".into());
            }
            names += item.name.len() + item.group.len();
            if names > 2 * 1024 * 1024 {
                return Err("Function-list label budget exceeded.".into());
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_unsafe_keys_and_non_character_boundaries() {
        assert!(Request {
            text: String::new(),
            parser: "../cpp".into()
        }
        .validate()
        .is_err());
        let response = Response {
            symbols: vec![Symbol {
                name: "é".into(),
                group: String::new(),
                start: 1,
                end: 2,
                line: 0,
                group_start: -1,
            }],
        };
        assert!(response.validate("é").is_err());
    }
}
