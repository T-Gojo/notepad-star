use serde::{Deserialize, Serialize};
use star_io::{NativePath, Stamp};
use std::{
    fs::{self, OpenOptions},
    io::Write,
    path::Path,
};

const MANIFEST_LIMIT: usize = 8 * 1024 * 1024;
pub const TOTAL_TEXT_LIMIT: usize = 64 * 1024 * 1024;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Edit {
    pub path: NativePath,
    pub expected: Stamp,
    pub replacement: String,
    #[serde(default)]
    pub encoding: Option<star_io::Encoding>,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Request {
    pub schema: u32,
    pub backup_directory: NativePath,
    pub edits: Vec<Edit>,
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Outcome {
    pub path: NativePath,
    pub applied: bool,
    pub message: String,
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Response {
    pub schema: u32,
    pub backup_directory: NativePath,
    pub outcomes: Vec<Outcome>,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Backup {
    path: NativePath,
    before: Stamp,
    after: Stamp,
    file: String,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Manifest {
    schema: u32,
    files: Vec<Backup>,
}

fn regular(path: &Path, writable: bool) -> Result<(), String> {
    let metadata = fs::symlink_metadata(path)
        .map_err(|e| format!("Cannot inspect {}: {e}", path.display()))?;
    if !metadata.is_file() || metadata.file_type().is_symlink() {
        return Err("Disk replacement requires regular, non-symlink files.".into());
    }
    if writable && metadata.permissions().readonly() {
        return Err(format!("{} is read-only.", path.display()));
    }
    Ok(())
}

pub fn apply(request: Request) -> Result<Response, String> {
    if request.schema != 1 || request.edits.is_empty() || request.edits.len() > 128 {
        return Err("A replacement batch must contain 1-128 files using schema 1.".into());
    }
    let mut total = 0usize;
    let mut prepared = Vec::new();
    let mut paths: Vec<std::path::PathBuf> = Vec::new();
    let mut staged_bytes = 0usize;
    for edit in &request.edits {
        total = total
            .checked_add(edit.replacement.len())
            .ok_or("Batch size overflow")?;
        if total > TOTAL_TEXT_LIMIT {
            return Err("Replacement output exceeds the 64 MiB batch budget.".into());
        }
        let path = edit.path.to_path()?;
        regular(&path, true)?;
        if paths
            .iter()
            .any(|previous| star_io::same_path(previous, &path))
        {
            return Err("A file appears more than once in the replacement batch.".into());
        }
        let before = star_io::read_limited(&path, star_io::MAX_FILE_BYTES)?;
        if star_io::stamp(&before) != edit.expected {
            return Err(format!(
                "{} changed since preview. No batch files were modified.",
                path.display()
            ));
        }
        let (_, encoding) = star_io::decode(&before, edit.encoding)?;
        let after = star_io::encode(&edit.replacement, encoding)?;
        staged_bytes += before.len() + after.len();
        if staged_bytes > TOTAL_TEXT_LIMIT {
            return Err("Original and replacement data exceed the 64 MiB staging budget.".into());
        }
        prepared.push((before, after));
        paths.push(path);
    }
    let directory = request.backup_directory.to_path()?;
    if let Some(parent) = directory.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    fs::create_dir(&directory).map_err(|e| format!("Cannot create a new backup directory: {e}"))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&directory, fs::Permissions::from_mode(0o700))
            .map_err(|e| e.to_string())?;
    }
    let mut manifest = Manifest {
        schema: 1,
        files: Vec::new(),
    };
    for (index, (before, after)) in prepared.iter().enumerate() {
        let filename = format!("{index}.bin");
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(directory.join(&filename))
            .map_err(|e| e.to_string())?;
        file.write_all(before)
            .and_then(|_| file.sync_all())
            .map_err(|e| format!("Cannot flush replacement backup: {e}"))?;
        manifest.files.push(Backup {
            path: request.edits[index].path.clone(),
            before: star_io::stamp(before),
            after: star_io::stamp(after),
            file: filename,
        });
    }
    let bytes = serde_json::to_vec(&manifest).map_err(|e| e.to_string())?;
    star_io::atomic_write(
        &directory.join("manifest.json"),
        &bytes,
        None,
        MANIFEST_LIMIT,
    )?;
    let mut outcomes = Vec::new();
    let mut failed = false;
    for (index, (_, after)) in prepared.iter().enumerate() {
        let result = if failed {
            Err("Not attempted because an earlier file failed.".into())
        } else {
            star_io::atomic_write(
                &paths[index],
                after,
                Some(&request.edits[index].expected),
                star_io::MAX_FILE_BYTES,
            )
            .map(|_| ())
        };
        let applied = result.is_ok();
        let message = match result {
            Ok(()) => "Replaced; original bytes backed up.".into(),
            Err(error) => {
                failed = true;
                error
            }
        };
        outcomes.push(Outcome {
            path: request.edits[index].path.clone(),
            applied,
            message,
        });
    }
    Ok(Response {
        schema: 1,
        backup_directory: request.backup_directory,
        outcomes,
    })
}

pub fn restore(directory: &Path) -> Result<Response, String> {
    let file = directory.join("manifest.json");
    regular(&file, false)?;
    let manifest: Manifest = serde_json::from_slice(&star_io::read_limited(&file, MANIFEST_LIMIT)?)
        .map_err(|e| format!("Invalid replacement-backup manifest: {e}"))?;
    if manifest.schema != 1 || manifest.files.is_empty() || manifest.files.len() > 128 {
        return Err("Unsupported backup manifest.".into());
    }
    let mut staged = Vec::new();
    let mut total = 0usize;
    for (index, backup) in manifest.files.iter().enumerate() {
        if backup.file != format!("{index}.bin") {
            return Err("Invalid backup filename.".into());
        }
        let path = backup.path.to_path()?;
        let original = directory.join(&backup.file);
        regular(&original, false)?;
        let bytes = star_io::read_limited(&original, star_io::MAX_FILE_BYTES)?;
        if star_io::stamp(&bytes) != backup.before {
            return Err("Replacement backup checksum mismatch.".into());
        }
        total += bytes.len();
        if total > TOTAL_TEXT_LIMIT {
            return Err("Restore data exceeds its staging budget.".into());
        }
        staged.push((path, bytes));
    }

    let mut outcomes = Vec::new();
    for (backup, (path, bytes)) in manifest.files.iter().zip(staged) {
        let result = (|| {
            let current = star_io::target_stamp(&path)?;
            if current.as_ref() == Some(&backup.before) {
                Ok(())
            } else {
                star_io::atomic_write(&path, &bytes, Some(&backup.after), star_io::MAX_FILE_BYTES)
                    .map(|_| ())
            }
        })();
        outcomes.push(Outcome {
            path: backup.path.clone(),
            applied: result.is_ok(),
            message: result
                .err()
                .unwrap_or_else(|| "Original bytes restored (or already present).".into()),
        });
    }
    Ok(Response {
        schema: 1,
        backup_directory: NativePath::from_path(directory),
        outcomes,
    })
}

pub fn inspect(directory: &Path) -> Result<Vec<NativePath>, String> {
    let file = directory.join("manifest.json");
    regular(&file, false)?;
    let manifest: Manifest = serde_json::from_slice(&star_io::read_limited(&file, MANIFEST_LIMIT)?)
        .map_err(|e| e.to_string())?;
    if manifest.schema != 1 || manifest.files.is_empty() || manifest.files.len() > 128 {
        return Err("Invalid backup manifest.".into());
    }
    let mut paths = Vec::new();
    for (index, backup) in manifest.files.into_iter().enumerate() {
        if backup.file != format!("{index}.bin") {
            return Err("Invalid backup entry.".into());
        }
        backup.path.to_path()?;
        paths.push(backup.path);
    }
    Ok(paths)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn replaces_preserving_bom_and_restores_only_unchanged_results() {
        let root = tempfile::tempdir().unwrap();
        let file = root.path().join("one.txt");
        let before = b"\xef\xbb\xbfhello\r\n";
        fs::write(&file, before).unwrap();
        let backup = root.path().join("backup");
        let result = apply(Request {
            schema: 1,
            backup_directory: NativePath::from_path(&backup),
            edits: vec![Edit {
                path: NativePath::from_path(&file),
                expected: star_io::stamp(before),
                replacement: "world\r\n".into(),
                encoding: None,
            }],
        })
        .unwrap();
        assert!(result.outcomes[0].applied);
        assert_eq!(fs::read(&file).unwrap(), b"\xef\xbb\xbfworld\r\n");
        fs::write(&file, b"external").unwrap();
        assert!(!restore(&backup).unwrap().outcomes[0].applied);
        assert_eq!(fs::read(&file).unwrap(), b"external");
        fs::write(&file, b"\xef\xbb\xbfworld\r\n").unwrap();
        assert!(restore(&backup).unwrap().outcomes[0].applied);
        assert_eq!(fs::read(&file).unwrap(), before);
    }
    #[test]
    fn legacy_replacement_preserves_encoding_and_refuses_loss_before_writing() {
        let root = tempfile::tempdir().unwrap();
        let file = root.path().join("latin1.txt");
        let before = b"caf\xe9";
        fs::write(&file, before).unwrap();
        let backup = root.path().join("backup");
        let mut edit = Edit {
            path: NativePath::from_path(&file),
            expected: star_io::stamp(before),
            replacement: "th\u{e9}".into(),
            encoding: Some(star_io::Encoding::from_label("ISO 8859-1").unwrap()),
        };
        let response = apply(Request {
            schema: 1,
            backup_directory: NativePath::from_path(&backup),
            edits: vec![edit.clone()],
        })
        .unwrap();
        assert!(response.outcomes[0].applied);
        assert_eq!(fs::read(&file).unwrap(), b"th\xe9");
        assert!(restore(&backup).unwrap().outcomes[0].applied);
        edit.replacement = "\u{20ac}".into();
        let failed_backup = root.path().join("failed");
        assert!(apply(Request {
            schema: 1,
            backup_directory: NativePath::from_path(&failed_backup),
            edits: vec![edit],
        })
        .is_err());
        assert_eq!(fs::read(&file).unwrap(), before);
        assert!(!failed_backup.exists());
    }

    #[test]
    fn preflight_conflict_does_not_partially_replace_files() {
        let root = tempfile::tempdir().unwrap();
        let one = root.path().join("one.txt");
        let two = root.path().join("two.txt");
        fs::write(&one, "one").unwrap();
        fs::write(&two, "external").unwrap();
        let result = apply(Request {
            schema: 1,
            backup_directory: NativePath::from_path(&root.path().join("backup")),
            edits: vec![
                Edit {
                    path: NativePath::from_path(&one),
                    expected: star_io::stamp(b"one"),
                    replacement: "new".into(),
                    encoding: None,
                },
                Edit {
                    path: NativePath::from_path(&two),
                    expected: star_io::stamp(b"two"),
                    replacement: "new".into(),
                    encoding: None,
                },
            ],
        });
        assert!(result.is_err());
        assert_eq!(fs::read(&one).unwrap(), b"one");
    }
}
