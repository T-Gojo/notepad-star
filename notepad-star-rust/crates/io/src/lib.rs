use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
};

pub type Result<T> = std::result::Result<T, String>;
pub mod codepages;
pub mod large_file;
pub mod settings;
pub const MAX_FILE_BYTES: usize = 32 * 1024 * 1024;
pub const MAX_RECOVERY_BYTES: usize = 128 * 1024 * 1024;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Encoding {
    #[default]
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
    Windows1252,
    ShiftJis,
    Utf16LeNoBom,
    Utf16BeNoBom,
    CodePage(codepages::CodePage),
}

impl Encoding {
    pub fn label(self) -> &'static str {
        match self {
            Self::Utf8 => "UTF-8",
            Self::Utf8Bom => "UTF-8 BOM",
            Self::Utf16Le => "UTF-16 LE BOM",
            Self::Utf16Be => "UTF-16 BE BOM",
            Self::Windows1252 => "Windows-1252",
            Self::ShiftJis => "Shift-JIS",
            Self::Utf16LeNoBom => "UTF-16 LE (no BOM)",
            Self::Utf16BeNoBom => "UTF-16 BE (no BOM)",
            Self::CodePage(page) => page.label(),
        }
    }
    pub fn all() -> Vec<Self> {
        let mut values = vec![
            Self::Utf8,
            Self::Utf8Bom,
            Self::Utf16Le,
            Self::Utf16Be,
            Self::Utf16LeNoBom,
            Self::Utf16BeNoBom,
            Self::Windows1252,
            Self::ShiftJis,
        ];
        values.extend(
            codepages::CodePage::all()
                .filter(|page| !matches!(u16::from(*page), 1252 | 932))
                .map(Self::CodePage),
        );
        values
    }
    pub fn from_label(label: &str) -> Result<Self> {
        match label {
            "UTF-16 LE" => return Ok(Self::Utf16Le),
            "UTF-16 BE" => return Ok(Self::Utf16Be),
            _ => {}
        }
        Self::all()
            .into_iter()
            .find(|encoding| encoding.label() == label)
            .ok_or_else(|| format!("Unsupported encoding: {label}"))
    }
}

pub fn decode(bytes: &[u8], override_encoding: Option<Encoding>) -> Result<(String, Encoding)> {
    if bytes.len() > MAX_FILE_BYTES {
        return Err("This preview supports files up to 32 MiB.".into());
    }
    let encoding = override_encoding.unwrap_or_else(|| {
        if bytes.starts_with(b"\xEF\xBB\xBF") {
            Encoding::Utf8Bom
        } else if bytes.starts_with(b"\xFF\xFE") {
            Encoding::Utf16Le
        } else if bytes.starts_with(b"\xFE\xFF") {
            Encoding::Utf16Be
        } else {
            Encoding::Utf8
        }
    });
    let encoding = match encoding {
        Encoding::Utf8 | Encoding::Utf8Bom => {
            if bytes.starts_with(b"\xEF\xBB\xBF") {
                Encoding::Utf8Bom
            } else {
                Encoding::Utf8
            }
        }
        Encoding::Utf16Le | Encoding::Utf16LeNoBom => {
            if bytes.starts_with(b"\xFF\xFE") {
                Encoding::Utf16Le
            } else {
                Encoding::Utf16LeNoBom
            }
        }
        Encoding::Utf16Be | Encoding::Utf16BeNoBom => {
            if bytes.starts_with(b"\xFE\xFF") {
                Encoding::Utf16Be
            } else {
                Encoding::Utf16BeNoBom
            }
        }
        other => other,
    };
    let body = match encoding {
        Encoding::Utf8Bom => bytes
            .strip_prefix(b"\xEF\xBB\xBF")
            .ok_or("The selected UTF-8 BOM is absent. Choose UTF-8 for a no-BOM file.")?,
        Encoding::Utf16Le => bytes
            .strip_prefix(b"\xFF\xFE")
            .ok_or("The selected UTF-16 LE BOM is absent. Choose its no-BOM mode.")?,
        Encoding::Utf16Be => bytes
            .strip_prefix(b"\xFE\xFF")
            .ok_or("The selected UTF-16 BE BOM is absent. Choose its no-BOM mode.")?,
        _ => bytes,
    };
    let text = match encoding {
        Encoding::Utf8 | Encoding::Utf8Bom => std::str::from_utf8(body)
            .map_err(|_| "Invalid UTF-8. Use Open with Encoding for a known legacy encoding; no bytes were changed.")?.into(),
        Encoding::Utf16Le | Encoding::Utf16Be | Encoding::Utf16LeNoBom | Encoding::Utf16BeNoBom => {
            if body.len() % 2 != 0 { return Err("Truncated UTF-16 file.".into()); }
            let units: Vec<_> = body.chunks_exact(2).map(|pair| {
                if matches!(encoding, Encoding::Utf16Le | Encoding::Utf16LeNoBom) { u16::from_le_bytes([pair[0], pair[1]]) }
                else { u16::from_be_bytes([pair[0], pair[1]]) }
            }).collect();
            String::from_utf16(&units).map_err(|_| "Invalid UTF-16 surrogate sequence.")?
        }
        Encoding::Windows1252 | Encoding::ShiftJis => {
            let id = if encoding == Encoding::Windows1252 { 1252 } else { 932 };
            codepages::CodePage::try_from(id)?.decode(body)?
        }
        Encoding::CodePage(page) => page.decode(body)?,
    };
    if text.len() > MAX_FILE_BYTES {
        return Err("Decoded text exceeds the 32 MiB editable-buffer limit.".into());
    }
    if text.contains('\0') {
        return Err("The file contains NUL bytes and may be binary. It was not opened.".into());
    }
    Ok((text, encoding))
}

pub fn encode(text: &str, encoding: Encoding) -> Result<Vec<u8>> {
    if text.contains('\0') {
        return Err("NUL-containing binary text cannot be saved by this preview.".into());
    }
    if text.len() > MAX_FILE_BYTES {
        return Err("Text exceeds the 32 MiB preview limit.".into());
    }
    let bytes = match encoding {
        Encoding::Utf8 => text.as_bytes().to_vec(),
        Encoding::Utf8Bom => [b"\xEF\xBB\xBF".as_slice(), text.as_bytes()].concat(),
        Encoding::Utf16Le | Encoding::Utf16Be | Encoding::Utf16LeNoBom | Encoding::Utf16BeNoBom => {
            let mut output = match encoding {
                Encoding::Utf16Le => vec![0xff, 0xfe],
                Encoding::Utf16Be => vec![0xfe, 0xff],
                _ => Vec::new(),
            };
            for unit in text.encode_utf16() {
                output.extend(
                    if matches!(encoding, Encoding::Utf16Le | Encoding::Utf16LeNoBom) {
                        unit.to_le_bytes()
                    } else {
                        unit.to_be_bytes()
                    },
                );
            }
            output
        }
        Encoding::Windows1252 | Encoding::ShiftJis => {
            let id = if encoding == Encoding::Windows1252 {
                1252
            } else {
                932
            };
            codepages::CodePage::try_from(id)?.encode(text)?
        }
        Encoding::CodePage(page) => page.encode(text)?,
    };
    if bytes.len() > MAX_FILE_BYTES {
        return Err("Encoded file exceeds the 32 MiB preview limit.".into());
    }
    Ok(bytes)
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Stamp {
    pub length: usize,
    pub digest: [u8; 32],
}

pub fn stamp(bytes: &[u8]) -> Stamp {
    Stamp {
        length: bytes.len(),
        digest: Sha256::digest(bytes).into(),
    }
}

pub fn read_limited(path: &Path, limit: usize) -> Result<Vec<u8>> {
    let file = File::open(path).map_err(|e| format!("Cannot open {}: {e}", path.display()))?;
    if file.metadata().map_err(|e| e.to_string())?.len() > limit as u64 {
        return Err(format!(
            "{} exceeds the supported size limit.",
            path.display()
        ));
    }
    let mut bytes = Vec::new();
    file.take(limit as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|e| format!("Cannot read {}: {e}", path.display()))?;
    if bytes.len() > limit {
        return Err("File grew beyond the size limit during reading.".into());
    }
    Ok(bytes)
}

pub fn target_stamp(path: &Path) -> Result<Option<Stamp>> {
    match fs::symlink_metadata(path) {
        Ok(metadata) => {
            if !metadata.is_file() || metadata.file_type().is_symlink() {
                return Err("Saving through a symbolic link or to a non-regular file is not supported; choose a regular destination.".into());
            }
            read_limited(path, MAX_FILE_BYTES).map(|bytes| Some(stamp(&bytes)))
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(format!("Cannot inspect {}: {error}", path.display())),
    }
}

pub fn same_path(a: &Path, b: &Path) -> bool {
    a == b || same_file::is_same_file(a, b).unwrap_or(false)
}

fn verify(path: &Path, expected: Option<&Stamp>, limit: usize) -> Result<()> {
    let actual = match fs::symlink_metadata(path) {
        Ok(metadata) => {
            if !metadata.is_file() || metadata.file_type().is_symlink() {
                return Err("The destination is not a regular file; it was not replaced.".into());
            }
            if metadata.permissions().readonly() {
                return Err("The destination is read-only.".into());
            }
            Some(stamp(&read_limited(path, limit)?))
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
        Err(error) => return Err(format!("Cannot inspect save destination: {error}")),
    };
    if actual.as_ref() != expected {
        return Err("The file changed or was deleted on disk. It was not overwritten. Reopen it, or use Save As to preserve your edits.".into());
    }
    Ok(())
}

fn preserve_metadata(path: &Path, temporary: &Path) -> Result<()> {
    fs::set_permissions(
        temporary,
        fs::metadata(path).map_err(|e| e.to_string())?.permissions(),
    )
    .map_err(|e| format!("Cannot preserve file permissions: {e}"))?;
    #[cfg(target_os = "macos")]
    for name in xattr::list(path).map_err(|e| format!("Cannot read extended attributes: {e}"))? {
        if let Some(value) = xattr::get(path, &name).map_err(|e| e.to_string())? {
            xattr::set(temporary, &name, &value)
                .map_err(|e| format!("Cannot preserve extended attribute: {e}"))?;
        }
    }
    Ok(())
}

pub fn atomic_write(
    path: &Path,
    bytes: &[u8],
    expected: Option<&Stamp>,
    limit: usize,
) -> Result<Stamp> {
    if bytes.len() > limit {
        return Err("Output exceeds the supported size limit.".into());
    }
    verify(path, expected, limit)?;
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let mut temporary = tempfile::Builder::new()
        .prefix(".notepad-star-")
        .tempfile_in(parent)
        .map_err(|e| format!("Cannot create a save file in {}: {e}", parent.display()))?;
    temporary
        .write_all(bytes)
        .map_err(|e| format!("Cannot write save file: {e}"))?;
    if expected.is_some() {
        preserve_metadata(path, temporary.path())?;
    }
    temporary
        .as_file()
        .sync_all()
        .map_err(|e| format!("Cannot flush save file: {e}"))?;
    verify(path, expected, limit)?;
    if expected.is_none() {
        temporary
            .persist_noclobber(path)
            .map_err(|e| format!("Cannot create destination without overwriting: {e}"))?;
    } else {
        #[cfg(windows)]
        {
            use std::os::windows::ffi::OsStrExt;
            let temporary = temporary.into_temp_path();
            let target: Vec<_> = path.as_os_str().encode_wide().chain(Some(0)).collect();
            let source: Vec<_> = temporary.as_os_str().encode_wide().chain(Some(0)).collect();
            // Both paths are NUL-terminated and live across the call. ReplaceFile preserves target ACLs.
            let replaced = unsafe {
                windows_sys::Win32::Storage::FileSystem::ReplaceFileW(
                    target.as_ptr(),
                    source.as_ptr(),
                    std::ptr::null(),
                    0,
                    std::ptr::null(),
                    std::ptr::null(),
                )
            };
            if replaced == 0 {
                return Err(format!(
                    "Cannot replace file: {}",
                    std::io::Error::last_os_error()
                ));
            }
        }
        #[cfg(not(windows))]
        temporary
            .persist(path)
            .map_err(|e| format!("Cannot replace file: {e}"))?;
    }
    #[cfg(unix)]
    File::open(parent)
        .and_then(|file| file.sync_all())
        .map_err(|e| {
            format!("The file was replaced, but directory durability could not be confirmed: {e}")
        })?;
    Ok(stamp(bytes))
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "platform", content = "units")]
pub enum NativePath {
    Windows(Vec<u16>),
    Unix(Vec<u8>),
}

impl NativePath {
    pub fn from_path(path: &Path) -> Self {
        #[cfg(windows)]
        {
            use std::os::windows::ffi::OsStrExt;
            Self::Windows(path.as_os_str().encode_wide().collect())
        }
        #[cfg(unix)]
        {
            use std::os::unix::ffi::OsStrExt;
            Self::Unix(path.as_os_str().as_bytes().to_vec())
        }
    }
    pub fn to_path(&self) -> Result<PathBuf> {
        match self {
            #[cfg(windows)]
            Self::Windows(units) => {
                use std::os::windows::ffi::OsStringExt;
                if units.contains(&0) {
                    return Err("A path contains a NUL unit.".into());
                }
                Ok(std::ffi::OsString::from_wide(units).into())
            }
            #[cfg(unix)]
            Self::Unix(bytes) => {
                use std::os::unix::ffi::OsStringExt;
                if bytes.contains(&0) {
                    return Err("A path contains a NUL byte.".into());
                }
                Ok(std::ffi::OsString::from_vec(bytes.clone()).into())
            }
            _ => Err("This saved path belongs to a different operating system.".into()),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct RecoveryDocument {
    pub path: Option<NativePath>,
    pub name: String,
    pub text: String,
    pub encoding: Encoding,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub saved_encoding: Option<Encoding>,
    pub expected: Option<Stamp>,
    pub dirty: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub view: Option<ViewState>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tab: Option<TabState>,
}

#[derive(Clone, Copy, Debug, Default, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TabState {
    pub pinned: bool,
    pub color: u8,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct ViewState {
    pub caret: usize,
    pub anchor: usize,
    pub first_visible: u32,
    pub x_offset: u32,
    pub zoom: i32,
    pub clone_visible: bool,
    pub clone_caret: usize,
    pub clone_anchor: usize,
    pub clone_first_visible: u32,
    pub lexer: String,
    pub eol: u8,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub language_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub udl_xml_base64: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub function_key: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub bookmarks: Option<Vec<u32>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub read_only: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub view_group: Option<u8>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub group_active: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub first_document_line: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub clone_first_document_line: Option<u32>,
}
impl ViewState {
    pub fn fit_to_text(&mut self, text: &str) -> bool {
        let mut changed = false;
        for position in [
            &mut self.caret,
            &mut self.anchor,
            &mut self.clone_caret,
            &mut self.clone_anchor,
        ] {
            let old = *position;
            *position = (*position).min(text.len());
            while !text.is_char_boundary(*position) {
                *position -= 1;
            }
            changed |= old != *position;
        }
        let mut lines = 1u32;
        let mut cr = false;
        for byte in text.bytes() {
            if byte == b'\r' || (byte == b'\n' && !cr) {
                lines += 1;
            }
            cr = byte == b'\r';
        }
        for first in [&mut self.first_visible, &mut self.clone_first_visible] {
            let old = *first;
            *first = (*first).min(lines - 1);
            changed |= old != *first;
        }
        for first in [
            &mut self.first_document_line,
            &mut self.clone_first_document_line,
        ]
        .into_iter()
        .flatten()
        {
            let old = *first;
            *first = (*first).min(lines - 1);
            changed |= old != *first;
        }
        if let Some(bookmarks) = &mut self.bookmarks {
            let old = bookmarks.len();
            bookmarks.retain(|line| *line < lines);
            changed |= old != bookmarks.len();
        }
        changed
    }
    pub fn validate(&self, text: &str) -> Result<()> {
        if self.view_group.is_some_and(|group| group > 1) {
            return Err("Stored document view group must be left or right.".into());
        }
        if let Some(bookmarks) = &self.bookmarks {
            if bookmarks.len() > 100000 || bookmarks.windows(2).any(|pair| pair[0] >= pair[1]) {
                return Err("Stored bookmarks must be sorted, unique and bounded.".into());
            }
            if let Some(&last) = bookmarks.last() {
                let mut line = 0;
                let mut previous_cr = false;
                for byte in text.bytes() {
                    if line >= last {
                        break;
                    }
                    if byte == b'\r' || (byte == b'\n' && !previous_cr) {
                        line += 1;
                    }
                    previous_cr = byte == b'\r';
                }
                if line < last {
                    return Err("A stored bookmark is outside the document.".into());
                }
            }
        }
        if self.function_key.as_ref().is_some_and(|key| {
            key.len() > 128
                || !key
                    .chars()
                    .all(|ch| ch.is_ascii_alphanumeric() || matches!(ch, '.' | '-' | '_'))
        }) {
            return Err("Invalid stored function-list parser key.".into());
        }
        if self
            .language_id
            .as_ref()
            .is_some_and(|id| id.len() > 256 || id.contains('\0'))
            || self
                .udl_xml_base64
                .as_ref()
                .is_some_and(|xml| xml.len() > 12 * 1024 * 1024 || xml.contains('\0'))
        {
            return Err("Session language profile exceeds its bounds.".into());
        }
        if [self.caret, self.anchor, self.clone_caret, self.clone_anchor]
            .iter()
            .any(|position| *position > text.len() || !text.is_char_boundary(*position))
            || !(-10..=100).contains(&self.zoom)
            || self.lexer.len() > 128
            || self.lexer.contains('\0')
            || self.eol > 2
        {
            return Err("Session view contains invalid positions, zoom, lexer or EOL mode.".into());
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SessionLayout {
    pub active: usize,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Preferences {
    pub dark: bool,
    pub wrap: bool,
    pub tab_width: u8,
}
impl Default for Preferences {
    fn default() -> Self {
        Self {
            dark: false,
            wrap: false,
            tab_width: 4,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Session {
    pub schema: u32,
    pub documents: Vec<RecoveryDocument>,
    pub preferences: Preferences,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub layout: Option<SessionLayout>,
}

impl Session {
    pub fn validate(&self) -> Result<()> {
        if self.schema != 1 {
            return Err("Unsupported session schema.".into());
        }
        if self.documents.len() > 64 {
            return Err("Session contains too many documents.".into());
        }
        if self.layout.as_ref().is_some_and(|layout| {
            layout.active >= self.documents.len() && !self.documents.is_empty()
        }) {
            return Err("Session active document is out of range.".into());
        }
        let mut total = 0;
        for doc in &self.documents {
            if doc.tab.is_some_and(|tab| tab.color > 5) {
                return Err("Invalid session tab color.".into());
            }
            if let Some(view) = &doc.view {
                view.validate(&doc.text)?;
            }
            if doc.text.len() > MAX_FILE_BYTES {
                return Err("Session document exceeds the supported size.".into());
            }
            if let Some(path) = &doc.path {
                path.to_path()?;
            }
            total += doc.text.len();
            total += doc
                .view
                .as_ref()
                .and_then(|view| view.udl_xml_base64.as_ref())
                .map(String::len)
                .unwrap_or(0);
        }
        if total > MAX_RECOVERY_BYTES {
            return Err("Session exceeds the recovery size limit.".into());
        }
        Ok(())
    }
}

pub fn save_session(path: &Path, session: &Session) -> Result<()> {
    session.validate()?;
    let payload = serde_json::to_vec(session).map_err(|e| e.to_string())?;
    let bytes = serde_json::to_vec(&SessionEnvelope {
        session: session.clone(),
        checksum: stamp(&payload),
    })
    .map_err(|e| e.to_string())?;
    let expected = match fs::metadata(path) {
        Ok(_) => Some(stamp(&read_limited(path, MAX_RECOVERY_BYTES)?)),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => None,
        Err(e) => return Err(e.to_string()),
    };
    atomic_write(path, &bytes, expected.as_ref(), MAX_RECOVERY_BYTES)?;
    Ok(())
}

pub fn load_session(path: &Path) -> Result<Session> {
    let envelope: SessionEnvelope =
        serde_json::from_slice(&read_limited(path, MAX_RECOVERY_BYTES)?).map_err(|e| {
            format!(
                "Cannot parse session {}: {e}. The original file has been kept.",
                path.display()
            )
        })?;
    let payload = serde_json::to_vec(&envelope.session).map_err(|e| e.to_string())?;
    if stamp(&payload) != envelope.checksum {
        return Err(format!(
            "Session checksum mismatch for {}. The original has been kept.",
            path.display()
        ));
    }
    let session = envelope.session;
    session.validate()?;
    Ok(session)
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct SessionEnvelope {
    session: Session,
    checksum: Stamp,
}

pub struct RecoveryStore {
    directory: PathBuf,
    _lock: File,
}

pub struct RecoveryBatch {
    pub sessions: Vec<Session>,
    files: Vec<(PathBuf, File)>,
}

impl RecoveryBatch {
    pub fn acknowledge(self) -> Result<()> {
        for (path, _lock) in &self.files {
            fs::remove_file(path).map_err(|e| {
                format!("Cannot clear transferred recovery {}: {e}", path.display())
            })?;
        }
        Ok(())
    }
}

impl RecoveryStore {
    pub fn has_snapshots(root: &Path) -> Result<bool> {
        if !root.exists() {
            return Ok(false);
        }
        for entry in fs::read_dir(root).map_err(|e| e.to_string())? {
            let entry = entry.map_err(|e| e.to_string())?;
            if entry.file_type().map_err(|e| e.to_string())?.is_dir()
                && entry.path().join("session.json").is_file()
            {
                return Ok(true);
            }
        }
        Ok(false)
    }
    pub fn automatic_resume_enabled(root: &Path) -> Result<bool> {
        let path = root.join("automatic-session-v1");
        match fs::symlink_metadata(&path) {
            Ok(metadata) => {
                if !metadata.is_file()
                    || metadata.file_type().is_symlink()
                    || read_limited(&path, 128)? != b"Notepad Star automatic session snapshots v1\n"
                {
                    return Err(
                        "Invalid automatic-session marker; existing session data was retained."
                            .into(),
                    );
                }
                Ok(true)
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(false),
            Err(e) => Err(format!("Cannot inspect automatic-session policy: {e}")),
        }
    }
    pub fn enable_automatic_resume(&self) -> Result<()> {
        let root = self
            .directory
            .parent()
            .ok_or("Recovery store has no parent.")?;
        if Self::automatic_resume_enabled(root)? {
            return Ok(());
        }
        let mut temporary = tempfile::Builder::new()
            .prefix(".session-policy-")
            .tempfile_in(root)
            .map_err(|e| e.to_string())?;
        temporary
            .write_all(b"Notepad Star automatic session snapshots v1\n")
            .map_err(|e| e.to_string())?;
        temporary.as_file().sync_all().map_err(|e| e.to_string())?;
        match temporary.persist_noclobber(root.join("automatic-session-v1")) {
            Ok(_) => Ok(()),
            Err(error) if error.error.kind() == std::io::ErrorKind::AlreadyExists => {
                if Self::automatic_resume_enabled(root)? {
                    Ok(())
                } else {
                    Err("Automatic-session policy was not retained.".into())
                }
            }
            Err(error) => Err(format!("Cannot retain automatic-session policy: {error}")),
        }
    }
    pub fn create(root: &Path) -> Result<Self> {
        fs::create_dir_all(root).map_err(|e| format!("Cannot create recovery directory: {e}"))?;
        let directory = tempfile::Builder::new()
            .prefix("instance-")
            .tempdir_in(root)
            .map_err(|e| e.to_string())?
            .keep();
        let lock = OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .open(directory.join("lock"))
            .map_err(|e| e.to_string())?;
        fs2::FileExt::try_lock_exclusive(&lock)
            .map_err(|e| format!("Cannot lock recovery store: {e}"))?;
        Ok(Self {
            directory,
            _lock: lock,
        })
    }
    pub fn checkpoint(&self, session: &Session) -> Result<()> {
        save_session(&self.directory.join("session.json"), session)
    }
    pub fn finish(&self) -> Result<()> {
        match fs::remove_file(self.directory.join("session.json")) {
            Ok(()) => Ok(()),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(()),
            Err(e) => Err(format!("Cannot clear clean recovery state: {e}")),
        }
    }
    pub fn pending(root: &Path) -> Result<Vec<Session>> {
        Ok(Self::claim(root)?.sessions)
    }
    pub fn claim(root: &Path) -> Result<RecoveryBatch> {
        let mut sessions = Vec::new();
        let mut files = Vec::new();
        let mut pending_bytes = 0u64;
        let mut pending_documents = 0usize;
        if !root.exists() {
            return Ok(RecoveryBatch { sessions, files });
        }
        let mut directories = Vec::new();
        for entry in fs::read_dir(root).map_err(|e| e.to_string())? {
            let entry = entry.map_err(|e| e.to_string())?;
            if !entry.file_type().map_err(|e| e.to_string())?.is_dir() {
                continue;
            }
            let path = entry.path().join("session.json");
            match fs::metadata(&path) {
                Ok(metadata) if metadata.is_file() => directories.push((
                    metadata.modified().map_err(|e| e.to_string())?,
                    entry.path(),
                )),
                Ok(_) => {}
                Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
                Err(e) => return Err(format!("Cannot inspect recovery snapshot: {e}")),
            }
        }
        directories.sort_by(|a, b| b.cmp(a));
        for (_, directory) in directories {
            let path = directory.join("session.json");
            if !path.is_file() {
                continue;
            }
            let lock = OpenOptions::new()
                .read(true)
                .write(true)
                .open(directory.join("lock"))
                .map_err(|e| e.to_string())?;
            match fs2::FileExt::try_lock_exclusive(&lock) {
                Ok(()) => {
                    let metadata = match fs::metadata(&path) {
                        Ok(metadata) => metadata,
                        Err(e) if e.kind() == std::io::ErrorKind::NotFound => continue,
                        Err(e) => return Err(format!("Cannot inspect claimed recovery: {e}")),
                    };
                    pending_bytes = pending_bytes
                        .checked_add(metadata.len())
                        .ok_or("Recovery size overflow")?;
                    if pending_bytes > MAX_RECOVERY_BYTES as u64 || sessions.len() >= 64 {
                        return Err("Pending recovery exceeds its budget. Existing files were retained; load individual session files explicitly.".into());
                    }
                    let session = load_session(&path)?;
                    pending_documents += session.documents.len();
                    if pending_documents > 64 {
                        return Err("More than 64 documents await recovery. Load individual retained session files.".into());
                    }
                    sessions.push(session);
                    files.push((path, lock));
                }
                Err(e) if e.raw_os_error() == fs2::lock_contended_error().raw_os_error() => {
                    continue
                }
                Err(e) => return Err(format!("Cannot inspect recovery lock: {e}")),
            }
        }
        Ok(RecoveryBatch { sessions, files })
    }
}

#[cfg(test)]
mod tests;
