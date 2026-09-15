use crate::{atomic_write, read_limited, stamp, NativePath, Preferences, Result, Stamp};
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
};

const LIMIT: usize = 1024 * 1024;

#[derive(Clone, Debug, Serialize)]
pub struct RecentFile {
    pub path: NativePath,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub encoding: Option<crate::Encoding>,
}
impl<'de> Deserialize<'de> for RecentFile {
    fn deserialize<D: serde::Deserializer<'de>>(
        deserializer: D,
    ) -> std::result::Result<Self, D::Error> {
        #[derive(Deserialize)]
        #[serde(deny_unknown_fields)]
        struct Record {
            path: NativePath,
            #[serde(default)]
            encoding: Option<crate::Encoding>,
        }
        let value = serde_json::Value::deserialize(deserializer)?;
        if value.get("path").is_some() {
            let record: Record = serde_json::from_value(value).map_err(serde::de::Error::custom)?;
            Ok(Self {
                path: record.path,
                encoding: record.encoding,
            })
        } else {
            let object = value
                .as_object()
                .ok_or_else(|| serde::de::Error::custom("Invalid recent file."))?;
            if object.keys().any(|key| key != "platform" && key != "units") {
                return Err(serde::de::Error::custom("Unknown recent-file field."));
            }
            Ok(Self {
                path: serde_json::from_value(value).map_err(serde::de::Error::custom)?,
                encoding: None,
            })
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct ViewPreferences {
    pub line_numbers: bool,
    pub bookmark_margin: bool,
    pub indent_guides: bool,
    pub show_whitespace: bool,
    pub show_eol: bool,
    pub scroll_past_end: bool,
    pub virtual_space: bool,
    pub multi_selection: bool,
    pub additional_typing: bool,
    pub backspace_unindent: bool,
    pub caret_width: u8,
    pub caret_period: u16,
    pub wrap_indent: String,
}
impl Default for ViewPreferences {
    fn default() -> Self {
        Self {
            line_numbers: true,
            bookmark_margin: true,
            indent_guides: true,
            show_whitespace: false,
            show_eol: false,
            scroll_past_end: false,
            virtual_space: false,
            multi_selection: true,
            additional_typing: true,
            backspace_unindent: false,
            caret_width: 1,
            caret_period: 500,
            wrap_indent: "fixed".into(),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct ComparisonPreferences {
    pub detail: String,
    pub ignore_whitespace: bool,
    pub ignore_case: bool,
    pub ignore_eol: bool,
    pub ignore_blank_lines: bool,
    pub auto_recompare: bool,
    pub align_matches: bool,
    pub sync_scroll: bool,
}
impl Default for ComparisonPreferences {
    fn default() -> Self {
        Self {
            detail: "characters".into(),
            ignore_whitespace: false,
            ignore_case: false,
            ignore_eol: false,
            ignore_blank_lines: false,
            auto_recompare: true,
            align_matches: true,
            sync_scroll: true,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Settings {
    pub schema: u32,
    pub editor: Preferences,
    pub font_family: String,
    pub font_size: u8,
    pub use_tabs: bool,
    pub restore_session: bool,
    pub start_maximized: bool,
    pub comparison: ComparisonPreferences,
    pub auto_indent: bool,
    pub completion: bool,
    pub completion_mode: u8,
    pub completion_min_chars: u8,
    pub completion_ignore_numbers: bool,
    pub calltips: bool,
    pub view: ViewPreferences,
    pub recent: Vec<RecentFile>,
    pub shortcuts: BTreeMap<String, String>,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            schema: 1,
            editor: Preferences::default(),
            font_family: String::new(),
            font_size: 11,
            use_tabs: false,
            restore_session: true,
            start_maximized: true,
            comparison: ComparisonPreferences::default(),
            auto_indent: true,
            completion: true,
            completion_mode: 1,
            completion_min_chars: 2,
            completion_ignore_numbers: false,
            calltips: true,
            view: ViewPreferences::default(),
            recent: Vec::new(),
            shortcuts: BTreeMap::new(),
        }
    }
}

impl Settings {
    pub fn from_json(bytes: &[u8]) -> Result<Self> {
        if bytes.len() > LIMIT {
            return Err("Settings exceed their size limit.".into());
        }
        let json: serde_json::Value = serde_json::from_slice(bytes).map_err(|e| e.to_string())?;
        let legacy_calltips = json.get("calltips").is_none();
        let mut value: Self = serde_json::from_value(json).map_err(|e| e.to_string())?;
        if legacy_calltips {
            value.calltips = value.completion;
        }
        value.validate()?;
        Ok(value)
    }
    pub fn validate(&self) -> Result<()> {
        if !matches!(
            self.comparison.detail.as_str(),
            "lines" | "words" | "characters"
        ) {
            return Err("Invalid comparison detail mode.".into());
        }
        if self.completion_mode > 3
            || !(1..=9).contains(&self.completion_min_chars)
            || self.view.caret_width > 5
            || self.view.caret_period > 1000
            || !matches!(
                self.view.wrap_indent.as_str(),
                "fixed" | "same" | "indent" | "deep_indent"
            )
        {
            return Err("Invalid completion, caret or wrap-indentation preference.".into());
        }
        if self.schema != 1 {
            return Err("Unsupported settings schema. The file was retained.".into());
        }
        if !(1..=16).contains(&self.editor.tab_width) || !(6..=48).contains(&self.font_size) {
            return Err("Tab width must be 1-16 and font size must be 6-48.".into());
        }
        if self.font_family.len() > 128 || self.font_family.contains('\0') || self.recent.len() > 20
        {
            return Err("Settings contain an invalid font or too many recent files.".into());
        }
        for recent in &self.recent {
            let invalid = match &recent.path {
                NativePath::Windows(units) => units.len() > 32768 || units.contains(&0),
                NativePath::Unix(units) => units.len() > 32768 || units.contains(&0),
            };
            if invalid || !recent.path.to_path()?.is_absolute() {
                return Err(
                    "Recent files require absolute bounded native paths without NUL.".into(),
                );
            }
        }
        let mut shortcuts = std::collections::HashSet::new();
        for (id, shortcut) in &self.shortcuts {
            if id.len() > 80 || shortcut.len() > 80 {
                return Err("Shortcut mapping is too long.".into());
            }
            if !shortcut.is_empty() && !shortcuts.insert(shortcut.to_lowercase()) {
                return Err("Two commands have the same shortcut.".into());
            }
        }
        Ok(())
    }
}

pub struct SettingsStore {
    path: PathBuf,
    expected: Option<Stamp>,
    pub value: Settings,
}

impl SettingsStore {
    fn latest_profile(&self) -> Result<Self> {
        let latest = Self::open(&self.path)?;
        if self.expected.is_some() && latest.expected.is_none() {
            return Err(
                "The settings profile was deleted externally; recent history was not written."
                    .into(),
            );
        }
        Ok(latest)
    }
    fn same_preferences(&self, other: &Self) -> Result<bool> {
        let mut before = self.value.clone();
        let mut after = other.value.clone();
        before.recent.clear();
        after.recent.clear();
        Ok(serde_json::to_vec(&before).map_err(|e| e.to_string())?
            == serde_json::to_vec(&after).map_err(|e| e.to_string())?)
    }
    fn adopt_history(&mut self, other: Self, same_preferences: bool) {
        if same_preferences {
            *self = other;
        }
        // Retain the old write stamp when external preferences differ.
        else {
            self.value.recent = other.value.recent;
        }
    }
    pub fn recent_files(&mut self) -> Result<Vec<RecentFile>> {
        let latest = self.latest_profile()?;
        let same = self.same_preferences(&latest)?;
        self.adopt_history(latest, same);
        Ok(self.value.recent.clone())
    }
    fn edit_history(&mut self, edit: impl Fn(&mut Vec<RecentFile>) -> Result<()>) -> Result<()> {
        for attempt in 0..3 {
            let mut latest = self.latest_profile()?;
            let same = self.same_preferences(&latest)?;
            let expected = latest.expected.clone();
            let mut updated = latest.value.clone();
            edit(&mut updated.recent)?;
            match latest.save(updated) {
                Ok(()) => {
                    self.adopt_history(latest, same);
                    return Ok(());
                }
                Err(error) => {
                    let actual = match fs::metadata(&self.path) {
                        Ok(_) => Some(stamp(&read_limited(&self.path, LIMIT)?)),
                        Err(e) if e.kind() == std::io::ErrorKind::NotFound => None,
                        Err(_) => return Err(error),
                    };
                    if attempt == 2 || actual == expected {
                        return Err(error);
                    }
                }
            }
        }
        unreachable!("bounded retry always returns")
    }
    pub fn remember_file(
        &mut self,
        path: &Path,
        encoding: Option<crate::Encoding>,
        previous: Option<&Path>,
    ) -> Result<()> {
        let absolute = std::path::absolute(path).map_err(|e| e.to_string())?;
        self.edit_history(|entries| {
            let mut retained = Vec::new();
            for entry in entries.iter() {
                let existing = entry.path.to_path()?;
                if !crate::same_path(&existing, &absolute)
                    && !previous.is_some_and(|old| crate::same_path(&existing, old))
                {
                    retained.push(entry.clone());
                }
            }
            retained.insert(
                0,
                RecentFile {
                    path: NativePath::from_path(&absolute),
                    encoding,
                },
            );
            retained.truncate(20);
            *entries = retained;
            Ok(())
        })
    }
    pub fn forget_file(&mut self, path: Option<&Path>) -> Result<()> {
        self.edit_history(|entries| {
            let mut retained = Vec::new();
            if let Some(path) = path {
                for entry in entries.iter() {
                    if !crate::same_path(&entry.path.to_path()?, path) {
                        retained.push(entry.clone());
                    }
                }
            }
            *entries = retained;
            Ok(())
        })
    }
    pub fn reset_with_backup(path: &Path) -> Result<(Self, PathBuf)> {
        let expected = match fs::symlink_metadata(path) {
            Ok(metadata) => {
                if !metadata.is_file() || metadata.file_type().is_symlink() {
                    return Err("Settings reset requires a regular, non-symlink file.".into());
                }
                Some(stamp(&read_limited(path, LIMIT)?))
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => None,
            Err(e) => return Err(format!("Cannot inspect the settings profile: {e}")),
        };
        let mut store = Self {
            path: path.to_owned(),
            expected,
            value: Settings::default(),
        };
        let backup = store.import(Settings::default())?;
        Ok((store, backup))
    }
    pub fn open(path: &Path) -> Result<Self> {
        let (value, expected) = match fs::metadata(path) {
            Ok(_) => {
                let bytes = read_limited(path, LIMIT)?;
                let value = Settings::from_json(&bytes).map_err(|e| {
                    format!(
                        "Cannot parse settings {}: {e}. The original file was retained.",
                        path.display()
                    )
                })?;
                value.validate()?;
                (value, Some(stamp(&bytes)))
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => (Settings::default(), None),
            Err(e) => return Err(format!("Cannot inspect settings: {e}")),
        };
        Ok(Self {
            path: path.to_owned(),
            expected,
            value,
        })
    }
    pub fn save(&mut self, value: Settings) -> Result<()> {
        value.validate()?;
        if let Some(parent) = self.path.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let bytes = serde_json::to_vec_pretty(&value).map_err(|e| e.to_string())?;
        let written = atomic_write(&self.path, &bytes, self.expected.as_ref(), LIMIT)?;
        self.expected = Some(written);
        self.value = value;
        Ok(())
    }
    pub fn import(&mut self, value: Settings) -> Result<PathBuf> {
        use std::io::Write;
        value.validate()?;
        crate::verify(&self.path, self.expected.as_ref(), LIMIT)?;
        let parent = self
            .path
            .parent()
            .ok_or("Settings need a parent directory.")?;
        fs::create_dir_all(parent).map_err(|e| format!("Cannot create settings directory: {e}"))?;
        let original = if self.expected.is_some() {
            let bytes = read_limited(&self.path, LIMIT)?;
            if Some(stamp(&bytes)).as_ref() != self.expected.as_ref() {
                return Err("Settings changed before backup; import was cancelled.".into());
            }
            bytes
        } else {
            serde_json::to_vec_pretty(&self.value).map_err(|e| e.to_string())?
        };
        let mut backup = tempfile::Builder::new()
            .prefix("settings-before-import-")
            .suffix(".json")
            .tempfile_in(parent)
            .map_err(|e| format!("Cannot create settings backup: {e}"))?;
        backup
            .write_all(&original)
            .and_then(|_| backup.as_file().sync_all())
            .map_err(|e| format!("Cannot flush settings backup: {e}"))?;
        let path = backup
            .into_temp_path()
            .keep()
            .map_err(|e| format!("Cannot retain settings backup: {e}"))?;
        self.save(value)
            .map_err(|e| format!("{e} Backup retained at {}.", path.display()))?;
        Ok(path)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn recent_history_is_bounded_ordered_and_preserves_encoding_hints() {
        let root = tempfile::tempdir().unwrap();
        let profile = root.path().join("settings.json");
        let mut store = SettingsStore::open(&profile).unwrap();
        for index in 0..25 {
            store
                .remember_file(
                    &root.path().join(format!("{index}.txt")),
                    Some(crate::Encoding::Utf8),
                    None,
                )
                .unwrap();
        }
        assert_eq!(store.value.recent.len(), 20);
        let latest = root.path().join("24.txt");
        store
            .remember_file(&latest, Some(crate::Encoding::Windows1252), None)
            .unwrap();
        assert_eq!(store.value.recent.len(), 20);
        assert_eq!(store.value.recent[0].path.to_path().unwrap(), latest);
        assert_eq!(
            store.value.recent[0].encoding,
            Some(crate::Encoding::Windows1252)
        );
        let renamed = root.path().join("renamed.txt");
        store
            .remember_file(&renamed, Some(crate::Encoding::Windows1252), Some(&latest))
            .unwrap();
        assert_eq!(store.value.recent.len(), 20);
        assert!(!store
            .value
            .recent
            .iter()
            .any(|file| file.path.to_path().unwrap() == latest));
        store.forget_file(Some(&renamed)).unwrap();
        assert_eq!(store.value.recent.len(), 19);
        store.forget_file(None).unwrap();
        assert!(SettingsStore::open(&profile)
            .unwrap()
            .value
            .recent
            .is_empty());
        fs::write(&profile, b"external").unwrap();
        assert!(store.remember_file(&latest, None, None).is_err());
        assert!(store.value.recent.is_empty());
        assert_eq!(fs::read(&profile).unwrap(), b"external");
    }
    #[test]
    fn legacy_recent_path_profiles_are_read_without_losing_native_paths() {
        let root = tempfile::tempdir().unwrap();
        let path = NativePath::from_path(&root.path().join("legacy.txt"));
        let legacy = serde_json::json!({"recent": [path]});
        let settings = Settings::from_json(&serde_json::to_vec(&legacy).unwrap()).unwrap();
        assert_eq!(
            settings.recent[0].path.to_path().unwrap(),
            root.path().join("legacy.txt")
        );
        assert!(settings.recent[0].encoding.is_none());
        let detailed = serde_json::to_vec(&settings).unwrap();
        Settings::from_json(&detailed).unwrap();
        let invalid = serde_json::json!({"recent": [{"path": NativePath::from_path(&root.path().join("x")), "execute": true}]});
        assert!(Settings::from_json(&serde_json::to_vec(&invalid).unwrap()).is_err());
    }
    #[test]
    fn history_merges_across_instances_without_overwriting_external_preferences() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("settings.json");
        let mut first = SettingsStore::open(&path).unwrap();
        first.save(Settings::default()).unwrap();
        let mut second = SettingsStore::open(&path).unwrap();
        let one = root.path().join("one.txt");
        let two = root.path().join("two.txt");
        first.remember_file(&one, None, None).unwrap();
        second.remember_file(&two, None, None).unwrap();
        assert_eq!(first.recent_files().unwrap().len(), 2);
        assert_eq!(first.value.recent[0].path.to_path().unwrap(), two);
        let mut changed = second.value.clone();
        changed.font_size = 18;
        second.save(changed).unwrap();
        first.remember_file(&one, None, None).unwrap();
        assert_eq!(SettingsStore::open(&path).unwrap().value.font_size, 18);
        assert_eq!(first.value.font_size, 11);
        assert!(first.save(first.value.clone()).is_err());
        fs::remove_file(&path).unwrap();
        assert!(first.remember_file(&one, None, None).is_err());
        assert!(!path.exists());
    }
    #[test]
    fn settings_round_trip_and_conflicts() {
        let directory = tempfile::tempdir().unwrap();
        let file = directory.path().join("settings.json");
        let mut store = SettingsStore::open(&file).unwrap();
        assert_eq!(store.value.font_size, 11);
        let mut settings = store.value.clone();
        settings.editor.dark = true;
        settings.editor.tab_width = 8;
        store.save(settings.clone()).unwrap();
        let mut other = SettingsStore::open(&file).unwrap();
        assert!(other.value.editor.dark);
        settings.font_size = 13;
        store.save(settings.clone()).unwrap();
        assert!(other.save(settings).is_err());
        assert_eq!(SettingsStore::open(&file).unwrap().value.font_size, 13);
        fs::write(&file, "{broken").unwrap();
        assert!(SettingsStore::open(&file).is_err());
        assert_eq!(fs::read_to_string(&file).unwrap(), "{broken");
    }
    #[test]
    fn importing_preserves_exact_profile_bytes_and_refuses_invalid_or_stale_changes() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("settings.json");
        let original = b"{\n  \"font_size\": 12\n}\n";
        fs::write(&path, original).unwrap();
        let mut store = SettingsStore::open(&path).unwrap();
        let mut changed = store.value.clone();
        changed.view.caret_width = 2;
        let backup = store.import(changed.clone()).unwrap();
        assert_eq!(fs::read(&backup).unwrap(), original);
        assert_eq!(
            SettingsStore::open(&path).unwrap().value.view.caret_width,
            2
        );
        changed.view.caret_width = 99;
        assert!(store.import(changed.clone()).is_err());
        assert_eq!(fs::read_dir(root.path()).unwrap().count(), 2);
        changed.view.caret_width = 1;
        fs::write(&path, b"external").unwrap();
        assert!(store.import(changed).is_err());
        assert_eq!(fs::read(&path).unwrap(), b"external");
        assert_eq!(fs::read_dir(root.path()).unwrap().count(), 2);
    }
    #[test]
    fn resetting_corrupt_settings_retains_original_bytes_and_legacy_defaults() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("settings.json");
        let broken = b"\xff{broken";
        fs::write(&path, broken).unwrap();
        assert!(SettingsStore::open(&path).is_err());
        let (store, backup) = SettingsStore::reset_with_backup(&path).unwrap();
        assert_eq!(fs::read(backup).unwrap(), broken);
        assert_eq!(store.value.font_size, Settings::default().font_size);
        SettingsStore::open(&path).unwrap();
        assert!(
            !Settings::from_json(br#"{"completion":false}"#)
                .unwrap()
                .calltips
        );
        assert!(
            Settings::from_json(br#"{"completion":false,"calltips":true}"#)
                .unwrap()
                .calltips
        );
        assert!(SettingsStore::reset_with_backup(root.path()).is_err());
    }
    #[test]
    fn rejects_invalid_and_ambiguous_configuration() {
        let mut value = Settings {
            font_size: 255,
            ..Settings::default()
        };
        assert!(value.validate().is_err());
        value.font_size = 11;
        value.shortcuts.insert("open".into(), "Ctrl+O".into());
        value.shortcuts.insert("save".into(), "ctrl+o".into());
        assert!(value.validate().is_err());
    }
}
