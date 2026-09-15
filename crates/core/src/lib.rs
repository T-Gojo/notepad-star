#![forbid(unsafe_code)]

use serde::{Deserialize, Serialize};
use star_io::{Encoding, NativePath, RecoveryDocument, Stamp};
use std::path::{Path, PathBuf};

pub use star_io;
pub mod advanced;
pub mod comments;
pub mod comparison;
pub mod instance;
pub mod json_tools;
pub mod launch;
pub mod macros;
pub mod statistics;
pub const VERSION: &str = env!("CARGO_PKG_VERSION");
pub const DEVELOPMENT_BUILD: bool = cfg!(debug_assertions);
pub mod transform;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CommandId {
    New,
    Open,
    OpenEncoding,
    Save,
    SaveAs,
    SaveCopy,
    SaveAll,
    Reload,
    SaveSession,
    LoadSession,
    Recover,
    Close,
    CloseAll,
    Exit,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    SelectAll,
    Find,
    FindNext,
    FindPrevious,
    Replace,
    ReplaceAll,
    GoToLine,
    WordWrap,
    DarkTheme,
    CloneView,
    DocumentList,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    About,
    NextTab,
    PreviousTab,
    DuplicateLine,
    DeleteLine,
    MoveLineUp,
    MoveLineDown,
    Uppercase,
    Lowercase,
    TrimTrailing,
    SortAscending,
    SortDescending,
    RemoveDuplicates,
    FoldAll,
    UnfoldAll,
    ShowWhitespace,
    ReadOnly,
    ToggleBookmark,
    NextBookmark,
    PreviousBookmark,
    ClearBookmarks,
    EolCrLf,
    EolLf,
    EolCr,
    Encoding,
    Workspace,
    Preferences,
    ShortcutMapper,
    FindInFiles,
    CancelSearch,
    MacroStart,
    MacroStop,
    MacroPlay,
    MacroSave,
    MacroLoad,
    Print,
    PrintPreview,
    RunProgram,
    Hash,
    Base64Encode,
    Base64Decode,
    UrlEncode,
    UrlDecode,
    JsonFormat,
    JsonMinify,
    JsonPreview,
    JsonGraph,
    JsonInspector,
    JsonViewTree,
    JsonViewGraph,
    JsonViewPretty,
    ToggleComparison,
    CompareTabs,
    CompareRight,
    StopComparison,
    ComparisonSettings,
    NextDifference,
    PreviousDifference,
    SwapComparison,
    MoveOtherView,
    MoveLeftView,
    MoveRightView,
    MoveAllOtherView,
    FindAllCurrent,
    FindAllOpen,
    SearchCount,
    RunExtension,
    CreateExampleExtension,
    ImportUdl,
    ExportUdl,
    CompleteWord,
    Calltip,
    SelectLanguage,
    ReplaceInFiles,
    RestoreDiskBackup,
    ExportHtml,
    CopyHtml,
    CopyRtf,
    DocumentMap,
    SyncVertical,
    SyncHorizontal,
    ProjectPanel1,
    ProjectPanel2,
    ProjectPanel3,
    Monitoring,
    FunctionList,
    PinTab,
    TabColor,
    CloseOthers,
    CloseLeft,
    CloseRight,
    CloseUnchanged,
    CloseUnpinned,
    MoveTabLeft,
    MoveTabRight,
    MoveTabStart,
    MoveTabEnd,
    SortTabs,
    FullScreen,
    AlwaysOnTop,
    DistractionFree,
    FocusOtherView,
    ZoomSync,
    RenameFile,
    TrashFile,
    RevealFolder,
    ProperCase,
    ProperBlend,
    SentenceCase,
    SentenceBlend,
    InvertCase,
    ReverseLines,
    RemoveConsecutiveDuplicates,
    RemoveEmptyLines,
    RemoveBlankLines,
    TrimLeading,
    TrimBoth,
    TabsToSpaces,
    LeadingSpacesToTabs,
    NumericSort,
    ColumnEditor,
    ImportNotepadMacro,
    ManageExtensions,
    OpenBytePreview,
    ToggleLineComment,
    AddLineComment,
    RemoveLineComment,
    BlockComment,
    BlockUncomment,
    MatchingBrace,
    SelectBraces,
    CopyBookmarks,
    CutBookmarks,
    PasteBookmarks,
    DeleteBookmarks,
    DeleteUnmarked,
    InvertBookmarks,
    ImportPreferences,
    ResetPreferences,
    DuplicateSelection,
    JoinLines,
    SplitLines,
    BlankLineAbove,
    BlankLineBelow,
    MacroRepeat,
    DocumentSummary,
    DateTimeShort,
    DateTimeLong,
    DateTimeCustom,
    OpenRecent,
    OpenAllRecent,
    ClearRecent,
    ReopenClosed,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Command {
    pub id: CommandId,
    pub menu: String,
    pub label: String,
    pub shortcut: String,
    pub upstream: Option<String>,
    pub checkable: bool,
}

pub fn commands() -> Result<Vec<Command>, serde_json::Error> {
    #[derive(Deserialize)]
    struct Section {
        menu: String,
        commands: Vec<CommandId>,
    }
    let mut remaining: Vec<Command> =
        serde_json::from_str(include_str!("../../../resources/commands/commands.json"))?;
    let layout: Vec<Section> =
        serde_json::from_str(include_str!("../../../resources/commands/menu-layout.json"))?;
    let mut ordered = Vec::new();
    for section in layout {
        for id in section.commands {
            let index = remaining
                .iter()
                .position(|command| command.id == id)
                .ok_or_else(|| {
                    serde_json::Error::io(std::io::Error::new(
                        std::io::ErrorKind::InvalidData,
                        "Menu layout contains an unknown or repeated command",
                    ))
                })?;
            let mut command = remaining.remove(index);
            command.menu = section.menu.clone();
            ordered.push(command);
        }
    }
    ordered.extend(remaining);
    Ok(ordered)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct DocumentId(pub u64);

#[derive(Clone, Debug)]
pub struct Document {
    pub id: DocumentId,
    pub name: String,
    pub dirty: bool,
    pub revision: u64,
    pub path: Option<PathBuf>,
    pub encoding: Encoding,
    pub saved_encoding: Encoding,
    pub expected: Option<Stamp>,
    pub tab: star_io::TabState,
    text_dirty: bool,
    recovery_dirty: bool,
}

#[derive(Clone, Default)]
pub struct Application {
    documents: Vec<Document>,
    next_id: u64,
}

impl Application {
    pub fn new_document(&mut self, preview: bool) -> DocumentId {
        self.next_id += 1;
        let id = DocumentId(self.next_id);
        self.documents.push(Document {
            id,
            name: if preview {
                "Welcome.rs".into()
            } else {
                format!("new {}", id.0)
            },
            dirty: false,
            revision: 0,
            path: None,
            encoding: Encoding::Utf8,
            saved_encoding: Encoding::Utf8,
            expected: None,
            tab: star_io::TabState::default(),
            text_dirty: false,
            recovery_dirty: false,
        });
        id
    }

    pub fn document(&self, id: DocumentId) -> Result<&Document, String> {
        self.documents
            .iter()
            .find(|doc| doc.id == id)
            .ok_or_else(|| format!("Document {} no longer exists.", id.0))
    }

    pub fn changed(&mut self, id: DocumentId, dirty: bool) -> Result<(), String> {
        self.savepoint_changed(id, dirty)?;
        self.document_mut(id)?.revision += 1;
        Ok(())
    }

    pub fn savepoint_changed(&mut self, id: DocumentId, dirty: bool) -> Result<(), String> {
        let doc = self
            .documents
            .iter_mut()
            .find(|doc| doc.id == id)
            .ok_or_else(|| format!("Document {} no longer exists.", id.0))?;
        doc.text_dirty = dirty;
        doc.dirty = dirty || doc.recovery_dirty || doc.encoding != doc.saved_encoding;
        Ok(())
    }

    pub fn close(&mut self, id: DocumentId, discard: bool) -> Result<(), String> {
        let doc = self.document(id)?;
        if doc.dirty && !discard {
            return Err("Unsaved document: explicitly discard changes before closing.".into());
        }
        self.documents.retain(|doc| doc.id != id);
        Ok(())
    }

    fn document_mut(&mut self, id: DocumentId) -> Result<&mut Document, String> {
        self.documents
            .iter_mut()
            .find(|doc| doc.id == id)
            .ok_or_else(|| format!("Document {} no longer exists.", id.0))
    }

    pub fn open(
        &mut self,
        path: &Path,
        encoding: Option<Encoding>,
    ) -> Result<(DocumentId, Option<String>), String> {
        let path = std::path::absolute(path).map_err(|e| e.to_string())?;
        if let Some(doc) = self.documents.iter().find(|doc| {
            doc.path
                .as_ref()
                .is_some_and(|p| star_io::same_path(p, &path))
        }) {
            return Ok((doc.id, None));
        }
        if self.len() >= 64 {
            return Err("This preview supports 64 open documents.".into());
        }
        let bytes = star_io::read_limited(&path, star_io::MAX_FILE_BYTES)?;
        let (text, encoding) = star_io::decode(&bytes, encoding)?;
        let id = self.new_document(false);
        let doc = self.document_mut(id)?;
        doc.name = path
            .file_name()
            .ok_or("Missing filename")?
            .to_string_lossy()
            .into_owned();
        doc.path = Some(path);
        doc.encoding = encoding;
        doc.saved_encoding = encoding;
        doc.expected = Some(star_io::stamp(&bytes));
        Ok((id, Some(text)))
    }

    pub fn save(
        &mut self,
        id: DocumentId,
        text: &str,
        destination: Option<&Path>,
        confirmed: Option<&Stamp>,
        copy: bool,
    ) -> Result<(), String> {
        let doc = self.document(id)?;
        let path = destination
            .or(doc.path.as_deref())
            .ok_or("A save destination is required.")?
            .to_owned();
        if self.documents.iter().any(|other| {
            other.id != id
                && other
                    .path
                    .as_ref()
                    .is_some_and(|p| star_io::same_path(p, &path))
        }) {
            return Err("That file is open in another tab. Save from that tab instead.".into());
        }
        let same = doc
            .path
            .as_ref()
            .is_some_and(|p| star_io::same_path(p, &path));
        let expected = if same {
            doc.expected.as_ref()
        } else {
            confirmed
        };
        let bytes = star_io::encode(text, doc.encoding)?;
        let saved = star_io::atomic_write(&path, &bytes, expected, star_io::MAX_FILE_BYTES)?;
        if !copy || same {
            let doc = self.document_mut(id)?;
            doc.name = path
                .file_name()
                .ok_or("Missing filename")?
                .to_string_lossy()
                .into_owned();
            doc.path = Some(path);
            doc.expected = Some(saved);
            doc.saved_encoding = doc.encoding;
            doc.dirty = false;
            doc.text_dirty = false;
            doc.recovery_dirty = false;
        }
        Ok(())
    }

    pub fn reload(&mut self, id: DocumentId) -> Result<String, String> {
        let doc = self.document(id)?;
        let path = doc
            .path
            .as_ref()
            .ok_or("Untitled documents cannot be reloaded.")?;
        let bytes = star_io::read_limited(path, star_io::MAX_FILE_BYTES)?;
        let (text, encoding) = star_io::decode(&bytes, Some(doc.saved_encoding))?;
        let doc = self.document_mut(id)?;
        doc.encoding = encoding;
        doc.saved_encoding = encoding;
        doc.expected = Some(star_io::stamp(&bytes));
        doc.dirty = false;
        doc.text_dirty = false;
        doc.recovery_dirty = false;
        doc.revision += 1;
        Ok(text)
    }

    pub fn set_encoding(
        &mut self,
        id: DocumentId,
        text: &str,
        encoding: Encoding,
    ) -> Result<(), String> {
        star_io::encode(text, encoding)?;
        let doc = self.document_mut(id)?;
        doc.encoding = encoding;
        doc.dirty = doc.text_dirty || doc.recovery_dirty || doc.encoding != doc.saved_encoding;
        Ok(())
    }

    pub fn snapshot(&self, id: DocumentId, text: String) -> Result<RecoveryDocument, String> {
        let doc = self.document(id)?;
        Ok(RecoveryDocument {
            path: doc.path.as_deref().map(NativePath::from_path),
            name: doc.name.clone(),
            text,
            encoding: doc.encoding,
            saved_encoding: Some(doc.saved_encoding),
            expected: doc.expected.clone(),
            dirty: doc.dirty,
            view: None,
            tab: Some(doc.tab),
        })
    }

    pub fn restore(&mut self, record: &RecoveryDocument) -> Result<DocumentId, String> {
        if self.len() >= 64 {
            return Err("Session exceeds the 64-document preview limit.".into());
        }
        let path = record.path.as_ref().map(NativePath::to_path).transpose()?;
        // Restored buffers never silently replace another open document.
        if path.as_ref().is_some_and(|path| {
            self.documents.iter().any(|doc| {
                doc.path
                    .as_ref()
                    .is_some_and(|other| star_io::same_path(path, other))
            })
        }) {
            return Err(
                "A session file is already open. Close it before restoring this session.".into(),
            );
        }
        let id = self.new_document(false);
        let doc = self.document_mut(id)?;
        doc.path = path;
        doc.name = record.name.clone();
        doc.encoding = record.encoding;
        doc.saved_encoding = record.saved_encoding.unwrap_or(record.encoding);
        doc.expected = record.expected.clone();
        doc.tab = record.tab.unwrap_or_default();
        doc.dirty = record.dirty;
        doc.text_dirty = record.dirty;
        doc.recovery_dirty = record.dirty;
        Ok(id)
    }
    pub fn resume(
        &mut self,
        record: &mut RecoveryDocument,
    ) -> Result<(DocumentId, Vec<String>), String> {
        let mut notices = Vec::new();
        if let Some(path) = record.path.as_ref().map(NativePath::to_path).transpose()? {
            if self.is_open(&path) {
                notices.push(format!(
                    "{} was open in multiple windows. Its additional snapshot is an unsaved restored copy.",
                    path.display()
                ));
                record.path = None;
                record.expected = None;
                record.name.push_str(" (restored copy)");
                record.dirty = true;
            } else if !record.dirty {
                let disk =
                    star_io::read_limited(&path, star_io::MAX_FILE_BYTES).and_then(|bytes| {
                        let (text, encoding) = star_io::decode(
                            &bytes,
                            Some(record.saved_encoding.unwrap_or(record.encoding)),
                        )?;
                        Ok((text, encoding, star_io::stamp(&bytes)))
                    });
                match disk {
                    Ok((text, encoding, expected)) => {
                        record.text = text;
                        record.encoding = encoding;
                        record.saved_encoding = Some(encoding);
                        record.expected = Some(expected);
                        if let Some(view) = &mut record.view {
                            view.fit_to_text(&record.text);
                        }
                    }
                    Err(error) => {
                        record.dirty = true;
                        notices.push(format!(
                            "{} could not be reopened; its checkpoint text was retained as unsaved.\n{error}",
                            path.display()
                        ));
                    }
                }
            }
        }
        if let Some(view) = &record.view {
            view.validate(&record.text)?;
        }
        Ok((self.restore(record)?, notices))
    }
    pub fn disk_replacement_documents(&self, path: &Path) -> Result<Vec<DocumentId>, String> {
        let mut ids = Vec::new();
        for doc in &self.documents {
            if doc
                .path
                .as_ref()
                .is_some_and(|open| star_io::same_path(open, path))
            {
                if doc.dirty {
                    return Err(format!(
                        "{} has unsaved changes. Save or close it before disk replacement.",
                        doc.name
                    ));
                }
                ids.push(doc.id);
            }
        }
        Ok(ids)
    }
    pub fn set_tab(&mut self, id: DocumentId, pinned: bool, color: u8) -> Result<(), String> {
        if color > 5 {
            return Err("Tab color must be between 0 and 5.".into());
        }
        self.document_mut(id)?.tab = star_io::TabState { pinned, color };
        Ok(())
    }
    pub fn renamed(&mut self, id: DocumentId, path: PathBuf) -> Result<(), String> {
        let name = path
            .file_name()
            .ok_or("Missing renamed filename")?
            .to_string_lossy()
            .into_owned();
        let document = self.document_mut(id)?;
        document.path = Some(path);
        document.name = name;
        Ok(())
    }
    pub fn is_open(&self, path: &Path) -> bool {
        self.documents.iter().any(|document| {
            document
                .path
                .as_ref()
                .is_some_and(|open| star_io::same_path(open, path))
        })
    }
    pub fn len(&self) -> usize {
        self.documents.len()
    }

    pub fn is_empty(&self) -> bool {
        self.documents.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tab_presentation_round_trips_without_dirtying_text() {
        let mut app = Application::default();
        let id = app.new_document(false);
        app.set_tab(id, true, 3).unwrap();
        assert!(!app.document(id).unwrap().dirty);
        let snapshot = app.snapshot(id, "saved view".into()).unwrap();
        let mut restored = Application::default();
        let next = restored.restore(&snapshot).unwrap();
        assert!(restored.document(next).unwrap().tab.pinned);
        assert_eq!(restored.document(next).unwrap().tab.color, 3);
        assert!(restored.set_tab(next, false, 9).is_err());
    }

    #[test]
    fn document_lifecycle_rejects_implicit_data_loss() {
        let mut app = Application::default();
        let a = app.new_document(false);
        let b = app.new_document(false);
        assert_ne!(a, b);
        app.changed(a, true).unwrap();
        assert!(app.close(a, false).is_err());
        assert_eq!(app.document(a).unwrap().revision, 1);
        assert!(!app.document(b).unwrap().dirty);
        app.close(a, true).unwrap();
        assert!(app.document(a).is_err());
        assert!(app.changed(a, true).is_err());
        assert_eq!(app.len(), 1);
        app.close(b, false).unwrap();
        assert!(app.is_empty());
    }

    #[test]
    fn command_ids_and_shortcuts_are_unique() {
        let commands = commands().unwrap();
        let mut ids = std::collections::HashSet::new();
        let mut shortcuts = std::collections::HashSet::new();
        for command in commands {
            assert!(ids.insert(serde_json::to_string(&command.id).unwrap()));
            if !command.shortcut.is_empty() {
                assert!(shortcuts.insert(command.shortcut));
            }
            assert!(!command.menu.is_empty());
            assert!(!command.label.is_empty());
        }
    }

    #[test]
    fn file_round_trip_conflict_and_encoding_dirty_state() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("original.txt");
        std::fs::write(&path, b"\xef\xbb\xbfcaf\xc3\xa9\r\n").unwrap();
        let mut app = Application::default();
        let (id, text) = app.open(&path, None).unwrap();
        let text = text.unwrap();
        assert_eq!(app.open(&path, None).unwrap().0, id);
        assert_eq!(app.len(), 1);
        app.set_encoding(id, &text, Encoding::Utf16Be).unwrap();
        assert!(app.document(id).unwrap().dirty);
        app.changed(id, false).unwrap();
        assert!(app.document(id).unwrap().dirty);
        app.save(id, &text, None, None, false).unwrap();
        assert!(!app.document(id).unwrap().dirty);
        assert_eq!(
            star_io::decode(&std::fs::read(&path).unwrap(), None)
                .unwrap()
                .0,
            text
        );
        std::fs::write(&path, b"external").unwrap();
        app.changed(id, true).unwrap();
        assert!(app.save(id, "mine", None, None, false).is_err());
        assert!(app.document(id).unwrap().dirty);
        assert_eq!(std::fs::read(&path).unwrap(), b"external");
        let snapshot = app.snapshot(id, "recovered".into()).unwrap();
        let mut restored = Application::default();
        let recovered_id = restored.restore(&snapshot).unwrap();
        assert!(restored
            .save(recovered_id, "recovered", None, None, false)
            .is_err());
    }

    #[test]
    fn resume_preserves_dirty_text_encoding_and_original_save_stamp() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("original.txt");
        std::fs::write(&path, b"caf\xe9").unwrap();
        let mut app = Application::default();
        let (id, _) = app.open(&path, Some(Encoding::Windows1252)).unwrap();
        app.changed(id, true).unwrap();
        app.set_encoding(id, "café unsaved 🚀", Encoding::Utf16Be)
            .unwrap();
        let mut record = app.snapshot(id, "café unsaved 🚀".into()).unwrap();
        record.view = Some(star_io::ViewState {
            caret: record.text.len(),
            read_only: Some(true),
            ..Default::default()
        });
        std::fs::write(&path, b"external").unwrap();
        let mut next = Application::default();
        let (restored, notices) = next.resume(&mut record).unwrap();
        assert!(notices.is_empty());
        assert_eq!(record.text, "café unsaved 🚀");
        assert_eq!(next.document(restored).unwrap().encoding, Encoding::Utf16Be);
        assert_eq!(
            next.document(restored).unwrap().saved_encoding,
            Encoding::Windows1252
        );
        assert_eq!(record.view.unwrap().read_only, Some(true));
        next.savepoint_changed(restored, false).unwrap();
        assert!(next.document(restored).unwrap().dirty);
        assert!(next
            .save(restored, &record.text, None, None, false)
            .is_err());
        assert_eq!(std::fs::read(&path).unwrap(), b"external");
        assert_eq!(next.reload(restored).unwrap(), "external");
        assert_eq!(
            next.document(restored).unwrap().encoding,
            Encoding::Windows1252
        );
    }

    #[test]
    fn resume_refreshes_clean_files_and_keeps_missing_file_snapshots() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("clean.txt");
        std::fs::write(&path, "original\nsecond\nthird").unwrap();
        let mut app = Application::default();
        let (id, text) = app.open(&path, None).unwrap();
        let mut record = app.snapshot(id, text.unwrap()).unwrap();
        record.view = Some(star_io::ViewState {
            caret: 20,
            anchor: 1,
            clone_caret: 4,
            first_visible: 2,
            bookmarks: Some(vec![0, 1, 2]),
            ..Default::default()
        });
        std::fs::write(&path, "é").unwrap();
        let mut next = Application::default();
        let (restored, notices) = next.resume(&mut record).unwrap();
        assert!(notices.is_empty());
        assert_eq!(record.text, "é");
        assert!(!next.document(restored).unwrap().dirty);
        let view = record.view.as_ref().unwrap();
        assert_eq!(
            (
                view.caret,
                view.anchor,
                view.clone_caret,
                view.first_visible
            ),
            (2, 0, 2, 0)
        );
        assert_eq!(view.bookmarks, Some(vec![0]));
        assert_eq!(
            next.document(restored).unwrap().expected,
            Some(star_io::stamp("é".as_bytes()))
        );
        std::fs::remove_file(&path).unwrap();
        let mut missing = Application::default();
        let (restored, notices) = missing.resume(&mut record).unwrap();
        assert_eq!(notices.len(), 1);
        assert_eq!(record.text, "é");
        assert!(missing.document(restored).unwrap().dirty);
        assert!(missing
            .save(restored, &record.text, None, None, false)
            .is_err());
        assert!(!path.exists());
    }

    #[test]
    fn resume_keeps_conflicting_windows_as_untitled_copies() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("same.txt");
        std::fs::write(&path, "disk").unwrap();
        let mut original = Application::default();
        let (id, _) = original.open(&path, None).unwrap();
        original.changed(id, true).unwrap();
        let mut first = original.snapshot(id, "first window".into()).unwrap();
        let mut second = original.snapshot(id, "second window".into()).unwrap();
        let mut next = Application::default();
        next.resume(&mut first).unwrap();
        let (copy, notices) = next.resume(&mut second).unwrap();
        assert_eq!(next.len(), 2);
        assert_eq!(notices.len(), 1);
        assert!(next.document(copy).unwrap().path.is_none());
        assert!(next.document(copy).unwrap().dirty);
        assert!(second.expected.is_none());
        assert_eq!(second.text, "second window");
        assert_eq!(std::fs::read(&path).unwrap(), b"disk");
        assert!(next.restore(&first).is_err());
    }

    #[test]
    fn resume_untitled_unicode_and_empty_tabs_without_a_file_target() {
        let mut source = Application::default();
        let first = source.new_document(false);
        source.changed(first, true).unwrap();
        let second = source.new_document(false);
        let mut next = Application::default();
        for (id, text, dirty) in [
            (first, "日本 🚀\r\nscratch\0text", true),
            (second, "", false),
        ] {
            let mut record = source.snapshot(id, text.into()).unwrap();
            let (restored, notices) = next.resume(&mut record).unwrap();
            assert!(notices.is_empty());
            assert_eq!(record.text, text);
            assert!(next.document(restored).unwrap().path.is_none());
            assert_eq!(next.document(restored).unwrap().dirty, dirty);
        }
        assert_eq!(next.len(), 2);
    }
}
