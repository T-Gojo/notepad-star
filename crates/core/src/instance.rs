use serde::{Deserialize, Serialize};
use star_io::NativePath;

pub const MAX_REQUEST: usize = 1024 * 1024;
pub const MAX_RESPONSE: usize = 256 * 1024;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ClientRequest {
    pub endpoint: String,
    pub payload: String,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Request {
    pub schema: u32,
    pub files: Vec<NativePath>,
    pub line: u64,
    pub column: u64,
    pub language: String,
    pub read_only: bool,
}
impl Request {
    pub fn validate(&self) -> Result<(), String> {
        if self.schema != 1
            || self.files.len() > 64
            || self.line > 1_000_000_000
            || self.column > 1_000_000_000
            || self.language.len() > 128
            || self.language.contains('\0')
        {
            return Err("Invalid instance-forwarding request or limits.".into());
        }
        if self.files.is_empty()
            && (self.line != 0 || self.column != 0 || !self.language.is_empty() || self.read_only)
        {
            return Err("File-specific forwarding options need at least one file.".into());
        }
        for file in &self.files {
            let invalid = match file {
                NativePath::Windows(units) => units.len() > 32768 || units.contains(&0),
                NativePath::Unix(units) => units.len() > 32768 || units.contains(&0),
            };
            let path = file.to_path()?;
            #[cfg(windows)]
            if path.components().any(|part| {
                matches!(part, std::path::Component::Prefix(prefix)
                if !matches!(prefix.kind(), std::path::Prefix::Disk(_)))
            }) {
                return Err("Instance forwarding accepts ordinary local drive paths, not UNC or device namespaces.".into());
            }
            if invalid || !path.is_absolute() {
                return Err(
                    "Forwarded files need absolute, bounded native paths without NUL.".into(),
                );
            }
        }
        Ok(())
    }
    pub fn decode(json: &str) -> Result<Self, String> {
        if json.len() > MAX_REQUEST {
            return Err("Instance request exceeds 1 MiB.".into());
        }
        let request: Self = serde_json::from_str(json).map_err(|e| e.to_string())?;
        request.validate()?;
        Ok(request)
    }
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Response {
    pub schema: u32,
    pub opened: u32,
    pub errors: Vec<String>,
}
impl Response {
    pub fn encode(&self) -> Result<String, String> {
        if self.schema != 1
            || self.opened > 64
            || self.errors.len() > 64
            || self.errors.iter().any(|e| e.len() > 3072)
        {
            return Err("Invalid instance response.".into());
        }
        let json = serde_json::to_string(self).map_err(|e| e.to_string())?;
        if json.len() > MAX_RESPONSE {
            return Err("Instance response exceeds its limit.".into());
        }
        Ok(json)
    }
    pub fn accept(json: &str) -> Result<(), String> {
        if json.len() > MAX_RESPONSE {
            return Err("Oversized instance response.".into());
        }
        let response: Self = serde_json::from_str(json).map_err(|e| e.to_string())?;
        response.encode()?;
        if !response.errors.is_empty() {
            return Err(format!(
                "Opened {} file(s); forwarding errors:\n{}",
                response.opened,
                response.errors.join("\n")
            ));
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn forwarding_cannot_carry_commands_or_relative_paths() {
        let empty =
            r#"{"schema":1,"files":[],"line":0,"column":0,"language":"","read_only":false}"#;
        Request::decode(empty).unwrap();
        assert!(Request::decode(
            &empty.replace("\"schema\":1", "\"schema\":1,\"command\":\"run\"")
        )
        .is_err());
        assert!(crate::launch::parse(["--reuse-instance", "--read-only"].map(Into::into)).is_err());
        assert!(crate::launch::parse(["--profile-dir", ""].map(Into::into)).is_err());
        assert!(
            crate::launch::parse(["--profile-dir", "dedicated-profile"].map(Into::into))
                .unwrap()
                .profile
                .unwrap()
                .is_absolute()
        );
        let mut request = Request::decode(empty).unwrap();
        request
            .files
            .push(NativePath::from_path(std::path::Path::new("relative.txt")));
        assert!(request.validate().is_err());
        assert!(
            crate::launch::parse(["--reuse-instance", "--no-session"].map(Into::into)).is_err()
        );
        assert!(
            crate::launch::parse(["--reuse-instance", "--new-instance"].map(Into::into)).is_err()
        );
        assert!(
            crate::launch::parse(["--reuse-instance"].map(Into::into))
                .unwrap()
                .reuse_instance
        );
    }
    #[test]
    fn partial_failures_are_not_success_shaped() {
        let response = Response {
            schema: 1,
            opened: 1,
            errors: vec!["Second file could not be opened.".into()],
        };
        assert!(Response::accept(&response.encode().unwrap()).is_err());
        assert!(Response::accept(r#"{"schema":2,"opened":0,"errors":[]}"#).is_err());
    }
}
