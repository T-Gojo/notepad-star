use star_core::star_io::settings::{Settings, SettingsStore};
use star_core::star_io::{
    self, Encoding, NativePath, Preferences, RecoveryBatch, RecoveryStore, Session, Stamp,
};
use star_core::{Application, CommandId, DocumentId};
use std::cell::{Cell, RefCell};
use std::path::PathBuf;

#[cxx::bridge(namespace = "star")]
pub mod ffi {
    struct Action {
        id: String,
        menu: String,
        label: String,
        shortcut: String,
        checkable: bool,
    }
    struct FilePath {
        windows: Vec<u16>,
        unix: Vec<u8>,
    }
    struct RecentEntry {
        path: FilePath,
        encoding: String,
    }
    struct Opened {
        id: u64,
        text: String,
        encoding: String,
        existing: bool,
        dirty: bool,
        view: String,
    }
    struct Buffer {
        id: u64,
        text: String,
        view: String,
        active: bool,
    }
    struct SearchHit {
        start: u64,
        end: u64,
        line: u64,
        preview: String,
    }
    struct SearchJob {
        text: String,
        query: String,
        replacement: String,
        regex: bool,
        match_case: bool,
        whole_word: bool,
        dot_newline: bool,
        replace: bool,
        start: u64,
        end: u64,
        replace_limit: u32,
        reverse: bool,
        first_only: bool,
        wrap: bool,
        require_full_range: bool,
    }
    struct SearchOptions {
        mode: String,
        match_case: bool,
        whole_word: bool,
        dot_newline: bool,
        replace: bool,
    }
    struct SearchOutput {
        hits: Vec<SearchHit>,
        count: u64,
        text: String,
        replaced: bool,
        stamp: String,
        encoding: String,
        next_position: u64,
    }
    struct SearchSource {
        text: String,
        stamp: String,
        encoding: String,
    }
    struct FileList {
        files: Vec<FilePath>,
        warnings: Vec<String>,
    }
    struct DiskProposal {
        path: FilePath,
        stamp: String,
        text: String,
        encoding: String,
    }
    struct DiskOutcome {
        path: FilePath,
        applied: bool,
        message: String,
    }
    struct OutlineSymbol {
        name: String,
        group: String,
        start: u64,
        end: u64,
        line: u64,
        group_start: i64,
    }
    struct TabPresentation {
        pinned: bool,
        color: u8,
    }
    struct LaunchSettings {
        preview: bool,
        smoke_test: bool,
        screenshot: String,
        read_only: bool,
        no_session: bool,
        line: u64,
        column: u64,
        language: String,
        reuse_instance: bool,
        profile: FilePath,
    }
    struct ForwardLaunch {
        settings: LaunchSettings,
        files: Vec<FilePath>,
    }
    struct LargePage {
        text: String,
        offset: u64,
        next: u64,
        size: u64,
        hex: bool,
    }
    struct CommentOptions {
        line: String,
        open: String,
        close: String,
        column_zero: bool,
        comment_empty: bool,
        space: bool,
    }
    struct TextEdit {
        start: u64,
        end: u64,
        text: String,
    }
    struct TextOperationOptions {
        tab_width: u32,
        first_column: u32,
        numeric_kind: String,
        descending: bool,
        blanks: String,
    }
    struct ColumnOptions {
        start: String,
        step: String,
        rows: u32,
        repeat: u32,
        base: String,
        padding: String,
        width: u32,
    }

    extern "Rust" {
        type Controller;
        fn create_controller() -> Box<Controller>;
        fn actions(controller: &Controller) -> Result<Vec<Action>>;
        fn create_document(controller: &Controller, preview: bool) -> Result<u64>;
        fn document_title(controller: &Controller, id: u64) -> Result<String>;
        fn document_changed(controller: &Controller, id: u64, dirty: bool) -> Result<()>;
        fn document_savepoint_changed(controller: &Controller, id: u64, dirty: bool) -> Result<()>;
        fn close_document(controller: &Controller, id: u64, discard: bool) -> Result<()>;
        fn validate_command(controller: &Controller, id: &str) -> Result<()>;
        fn welcome_text() -> String;
        fn application_version() -> String;
        fn development_build() -> bool;
        fn open_document(
            controller: &Controller,
            path: &FilePath,
            encoding: &str,
        ) -> Result<Opened>;
        fn document_path(controller: &Controller, id: u64) -> Result<FilePath>;
        fn encoding_labels() -> Vec<String>;
        fn document_encoding(controller: &Controller, id: u64) -> Result<String>;
        fn document_saved_encoding(controller: &Controller, id: u64) -> Result<String>;
        fn recent_files(controller: &Controller) -> Result<Vec<RecentEntry>>;
        fn remember_recent(
            controller: &Controller,
            path: &FilePath,
            encoding: &str,
            previous: &FilePath,
        ) -> Result<()>;
        fn forget_recent(controller: &Controller, path: &FilePath) -> Result<()>;
        fn document_dirty(controller: &Controller, id: u64) -> Result<bool>;
        fn prepare_destination(path: &FilePath) -> Result<String>;
        fn save_document(
            controller: &Controller,
            id: u64,
            text: &str,
            path: &FilePath,
            prepared: &str,
            copy: bool,
        ) -> Result<()>;
        fn reload_document(controller: &Controller, id: u64) -> Result<Opened>;
        fn change_encoding(
            controller: &Controller,
            id: u64,
            text: &str,
            encoding: &str,
        ) -> Result<()>;
        fn initialize_recovery(
            controller: &Controller,
            root: &FilePath,
            recover: bool,
        ) -> Result<u32>;
        fn checkpoint(
            controller: &Controller,
            buffers: Vec<Buffer>,
            dark: bool,
            wrap: bool,
        ) -> Result<()>;
        fn finish_recovery(controller: &Controller) -> Result<()>;
        fn restore_recovery(controller: &Controller) -> Result<Vec<Opened>>;
        fn resume_workspace(controller: &Controller) -> Result<Vec<Opened>>;
        fn resume_notices(controller: &Controller) -> Result<String>;
        fn legacy_session_allowed(controller: &Controller) -> bool;
        fn retain_workspace(
            controller: &Controller,
            buffers: Vec<Buffer>,
            dark: bool,
            wrap: bool,
        ) -> Result<()>;
        fn save_session(
            controller: &Controller,
            buffers: Vec<Buffer>,
            path: &FilePath,
            dark: bool,
            wrap: bool,
        ) -> Result<()>;
        fn load_session(controller: &Controller, path: &FilePath) -> Result<Vec<Opened>>;
        fn transform_text(operation: &str, text: &str) -> Result<String>;
        fn document_revision(controller: &Controller, id: u64) -> Result<u64>;
        fn document_changed_on_disk(controller: &Controller, id: u64) -> Result<bool>;
        fn load_settings(controller: &Controller, path: &FilePath) -> Result<String>;
        fn store_settings(controller: &Controller, json: &str) -> Result<()>;
        fn current_settings(controller: &Controller) -> Result<String>;
        fn validate_settings_json(json: &str) -> Result<()>;
        fn normalize_settings_json(json: &str) -> Result<String>;
        fn import_settings(controller: &Controller, json: &str) -> Result<FilePath>;
        fn reset_settings(controller: &Controller, path: &FilePath) -> Result<FilePath>;
        fn search_request(
            text: &str,
            query: &str,
            replacement: &str,
            options: &SearchOptions,
        ) -> Result<String>;
        fn search_response(json: &str, source: &str) -> Result<SearchOutput>;
        fn scan_request(root: &FilePath, patterns: &str) -> Result<String>;
        fn scan_response(json: &str) -> Result<FileList>;
        fn read_search_file(path: &FilePath, encoding: &str) -> Result<SearchSource>;
        fn search_file_encoding(json: &str, encoding: &str) -> Result<String>;
        fn search_file_input(json: &str, path: &FilePath) -> Result<String>;
        fn validate_macro(json: &str) -> Result<()>;
        fn macro_repeat_steps(json: &str, repeats: u32) -> Result<u32>;
        fn document_statistics(text: &str) -> Result<String>;
        fn format_json(json: &str) -> Result<String>;
        fn minify_json(json: &str) -> Result<String>;
        fn json_preview(json: &str) -> Result<String>;
        fn compare_text(left: &str, right: &str, ignore_whitespace: bool) -> Result<String>;
        fn compare_documents(left: &str, right: &str, options: &str) -> Result<String>;
        fn imported_layout(controller: &Controller) -> Result<String>;
        fn reopen_session(controller: &Controller, path: &FilePath) -> Result<Vec<Opened>>;
        fn extension_inspection_request(path: &FilePath) -> Result<String>;
        fn extension_request(metadata: &str, selected: &str) -> Result<String>;
        fn extension_response(json: &str) -> Result<String>;
        fn management_request(action: &str) -> Result<String>;
        fn management_response(json: &str) -> Result<String>;
        fn managed_extension_request(root: &FilePath, id: &str, selected: &str) -> Result<String>;
        fn create_extension_example(path: &FilePath) -> Result<String>;
        fn disk_request(
            controller: &Controller,
            proposals: Vec<DiskProposal>,
            backup: &FilePath,
        ) -> Result<String>;
        fn disk_response(json: &str) -> Result<Vec<DiskOutcome>>;
        fn normalize_replacement(mode: &str, value: &str) -> Result<String>;
        fn disk_updated_documents(controller: &Controller, path: &FilePath) -> Result<Vec<u64>>;
        fn outline_request(text: &str, parser: &str) -> Result<String>;
        fn outline_response(json: &str, source: &str) -> Result<Vec<OutlineSymbol>>;
        fn document_tab(controller: &Controller, id: u64) -> Result<TabPresentation>;
        fn document_renamed(controller: &Controller, id: u64, path: &FilePath) -> Result<()>;
        fn needs_large_preview(controller: &Controller, path: &FilePath) -> Result<bool>;
        fn read_large_page(path: &FilePath, offset: u64) -> Result<LargePage>;
        fn advanced_transform(
            operation: &str,
            text: &str,
            options: &TextOperationOptions,
        ) -> Result<String>;
        fn column_values(options: &ColumnOptions) -> Result<Vec<String>>;
        fn forward_request(settings: &LaunchSettings, files: &[FilePath]) -> Result<String>;
        fn decode_forward_request(json: &str) -> Result<ForwardLaunch>;
        fn check_forward_file(path: &FilePath) -> Result<()>;
        fn forward_response(opened: u32, errors: Vec<String>) -> Result<String>;
        fn accept_forward_response(json: &str) -> Result<()>;
        fn comment_edits(
            text: &str,
            start: u64,
            end: u64,
            mode: &str,
            options: &CommentOptions,
        ) -> Result<Vec<TextEdit>>;
        fn set_document_tab(
            controller: &Controller,
            id: u64,
            pinned: bool,
            color: u8,
        ) -> Result<()>;
    }

    unsafe extern "C++" {
        include!("shell.h");
        fn run_desktop(settings: &LaunchSettings, files: Vec<FilePath>) -> Result<i32>;
        fn instance_client(endpoint: &str, request: &str) -> Result<String>;
        fn search_snapshot(job: &SearchJob) -> Result<SearchOutput>;
        fn lexer_exists(name: &str) -> bool;
        fn valid_udl(xml: &str) -> bool;
        fn outline_keys() -> Vec<String>;
        fn outline_snapshot(text: &str, parser: &str) -> Result<Vec<OutlineSymbol>>;
    }
}

pub struct Controller {
    app: RefCell<Application>,
    recovery: RefCell<Option<RecoveryStore>>,
    pending: RefCell<Option<RecoveryBatch>>,
    recovered: Cell<bool>,
    settings: RefCell<Option<SettingsStore>>,
    imported_layout: RefCell<String>,
    legacy_session_allowed: Cell<bool>,
    resume_notices: RefCell<Vec<String>>,
}

fn create_controller() -> Box<Controller> {
    Box::new(Controller {
        app: RefCell::new(Application::default()),
        recovery: RefCell::new(None),
        pending: RefCell::new(None),
        recovered: Cell::new(false),
        settings: RefCell::new(None),
        imported_layout: RefCell::new(String::new()),
        legacy_session_allowed: Cell::new(false),
        resume_notices: RefCell::new(Vec::new()),
    })
}

fn actions(_: &Controller) -> Result<Vec<ffi::Action>, String> {
    star_core::commands()
        .map_err(|error| error.to_string())?
        .into_iter()
        .map(|command| {
            let value = serde_json::to_value(command.id).map_err(|error| error.to_string())?;
            Ok(ffi::Action {
                id: value.as_str().ok_or("Command ID must be a string")?.into(),
                menu: command.menu,
                label: command.label,
                shortcut: command.shortcut,
                checkable: command.checkable,
            })
        })
        .collect()
}

fn create_document(controller: &Controller, preview: bool) -> Result<u64, String> {
    let mut app = controller.app.try_borrow_mut().map_err(|e| e.to_string())?;
    if app.len() >= 64 {
        return Err("This preview supports at most 64 open documents.".into());
    }
    Ok(app.new_document(preview).0)
}

fn document_title(controller: &Controller, id: u64) -> Result<String, String> {
    let app = controller.app.try_borrow().map_err(|e| e.to_string())?;
    let doc = app.document(DocumentId(id))?;
    Ok(format!("{}{}", doc.name, if doc.dirty { " *" } else { "" }))
}

fn document_changed(controller: &Controller, id: u64, dirty: bool) -> Result<(), String> {
    controller
        .app
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .changed(DocumentId(id), dirty)
}

fn document_savepoint_changed(controller: &Controller, id: u64, dirty: bool) -> Result<(), String> {
    controller
        .app
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .savepoint_changed(DocumentId(id), dirty)
}
fn close_document(controller: &Controller, id: u64, discard: bool) -> Result<(), String> {
    controller
        .app
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .close(DocumentId(id), discard)
}

fn validate_command(_: &Controller, id: &str) -> Result<(), String> {
    serde_json::from_value::<CommandId>(serde_json::Value::String(id.into()))
        .map(|_| ())
        .map_err(|_| format!("Unknown command: {id}"))
}

fn welcome_text() -> String {
    include_str!("../../../resources/welcome.rs.txt").replace("@VERSION@", star_core::VERSION)
}

fn path_from(value: &ffi::FilePath) -> Result<PathBuf, String> {
    #[cfg(windows)]
    let path = NativePath::Windows(value.windows.clone());
    #[cfg(unix)]
    let path = NativePath::Unix(value.unix.clone());
    path.to_path()
}
fn path_value(path: &std::path::Path) -> ffi::FilePath {
    let mut value = ffi::FilePath {
        windows: Vec::new(),
        unix: Vec::new(),
    };
    match NativePath::from_path(path) {
        NativePath::Windows(units) => value.windows = units,
        NativePath::Unix(bytes) => value.unix = bytes,
    }
    value
}
fn parse_encoding(value: &str) -> Result<Encoding, String> {
    Encoding::from_label(value)
}
fn encoding_labels() -> Vec<String> {
    Encoding::all()
        .into_iter()
        .map(|encoding| encoding.label().to_owned())
        .collect()
}
fn open_document(
    controller: &Controller,
    path: &ffi::FilePath,
    encoding: &str,
) -> Result<ffi::Opened, String> {
    let encoding = if encoding.is_empty() {
        None
    } else {
        Some(parse_encoding(encoding)?)
    };
    let mut app = controller.app.try_borrow_mut().map_err(|e| e.to_string())?;
    let (id, text) = app.open(&path_from(path)?, encoding)?;
    Ok(ffi::Opened {
        id: id.0,
        existing: text.is_none(),
        text: text.unwrap_or_default(),
        encoding: app.document(id)?.encoding.label().into(),
        dirty: false,
        view: String::new(),
    })
}
fn document_path(controller: &Controller, id: u64) -> Result<ffi::FilePath, String> {
    let app = controller.app.try_borrow().map_err(|e| e.to_string())?;
    Ok(path_value(
        app.document(DocumentId(id))?
            .path
            .as_deref()
            .unwrap_or(std::path::Path::new("")),
    ))
}
fn document_encoding(controller: &Controller, id: u64) -> Result<String, String> {
    Ok(controller
        .app
        .try_borrow()
        .map_err(|e| e.to_string())?
        .document(DocumentId(id))?
        .encoding
        .label()
        .into())
}
fn application_version() -> String {
    star_core::VERSION.into()
}
fn development_build() -> bool {
    star_core::DEVELOPMENT_BUILD
}
fn document_saved_encoding(controller: &Controller, id: u64) -> Result<String, String> {
    Ok(controller
        .app
        .try_borrow()
        .map_err(|e| e.to_string())?
        .document(DocumentId(id))?
        .saved_encoding
        .label()
        .into())
}
fn recent_files(controller: &Controller) -> Result<Vec<ffi::RecentEntry>, String> {
    controller
        .settings
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .as_mut()
        .ok_or("Recent history is unavailable until settings are repaired.")?
        .recent_files()?
        .iter()
        .map(|entry| {
            Ok(ffi::RecentEntry {
                path: path_value(&entry.path.to_path()?),
                encoding: entry
                    .encoding
                    .map(|encoding| encoding.label().to_owned())
                    .unwrap_or_default(),
            })
        })
        .collect()
}
fn remember_recent(
    controller: &Controller,
    path: &ffi::FilePath,
    encoding: &str,
    previous: &ffi::FilePath,
) -> Result<(), String> {
    let path = path_from(path)?;
    let previous = if previous.windows.is_empty() && previous.unix.is_empty() {
        None
    } else {
        Some(std::path::absolute(path_from(previous)?).map_err(|e| e.to_string())?)
    };
    let encoding = if encoding.is_empty() {
        None
    } else {
        Some(parse_encoding(encoding)?)
    };
    controller
        .settings
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .as_mut()
        .ok_or("Recent history is unavailable until settings are repaired.")?
        .remember_file(&path, encoding, previous.as_deref())
}
fn forget_recent(controller: &Controller, path: &ffi::FilePath) -> Result<(), String> {
    let path = if path.windows.is_empty() && path.unix.is_empty() {
        None
    } else {
        Some(path_from(path)?)
    };
    controller
        .settings
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .as_mut()
        .ok_or("Recent history is unavailable until settings are repaired.")?
        .forget_file(path.as_deref())
}
fn document_dirty(controller: &Controller, id: u64) -> Result<bool, String> {
    Ok(controller
        .app
        .try_borrow()
        .map_err(|e| e.to_string())?
        .document(DocumentId(id))?
        .dirty)
}
fn prepare_destination(path: &ffi::FilePath) -> Result<String, String> {
    let stamp = star_io::target_stamp(&path_from(path)?)?;
    stamp
        .map(|stamp| serde_json::to_string(&stamp).map_err(|e| e.to_string()))
        .transpose()
        .map(|s| s.unwrap_or_default())
}
fn save_document(
    controller: &Controller,
    id: u64,
    text: &str,
    path: &ffi::FilePath,
    prepared: &str,
    copy: bool,
) -> Result<(), String> {
    let path = path_from(path)?;
    let destination = if path.as_os_str().is_empty() {
        None
    } else {
        Some(path.as_path())
    };
    let confirmed: Option<Stamp> = if prepared.is_empty() {
        None
    } else {
        Some(serde_json::from_str(prepared).map_err(|e| e.to_string())?)
    };
    controller
        .app
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .save(DocumentId(id), text, destination, confirmed.as_ref(), copy)
}
fn reload_document(controller: &Controller, id: u64) -> Result<ffi::Opened, String> {
    let mut app = controller.app.try_borrow_mut().map_err(|e| e.to_string())?;
    let text = app.reload(DocumentId(id))?;
    Ok(ffi::Opened {
        id,
        text,
        encoding: app.document(DocumentId(id))?.encoding.label().into(),
        existing: false,
        dirty: false,
        view: String::new(),
    })
}
fn change_encoding(
    controller: &Controller,
    id: u64,
    text: &str,
    encoding: &str,
) -> Result<(), String> {
    controller
        .app
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .set_encoding(DocumentId(id), text, parse_encoding(encoding)?)
}
fn initialize_recovery(
    controller: &Controller,
    root: &ffi::FilePath,
    recover: bool,
) -> Result<u32, String> {
    let root = path_from(root)?;
    controller.legacy_session_allowed.set(
        recover
            && !RecoveryStore::automatic_resume_enabled(&root)?
            && !RecoveryStore::has_snapshots(&root)?,
    );
    let pending = if recover {
        Some(RecoveryStore::claim(&root)?)
    } else {
        None
    };
    let count = pending
        .as_ref()
        .map(|batch| {
            batch
                .sessions
                .iter()
                .map(|session| session.documents.len() as u32)
                .sum()
        })
        .unwrap_or(0);
    *controller
        .pending
        .try_borrow_mut()
        .map_err(|e| e.to_string())? = pending;
    *controller
        .recovery
        .try_borrow_mut()
        .map_err(|e| e.to_string())? = Some(RecoveryStore::create(&root)?);
    Ok(count)
}
fn make_session(
    controller: &Controller,
    buffers: Vec<ffi::Buffer>,
    dark: bool,
    wrap: bool,
) -> Result<Session, String> {
    let app = controller.app.try_borrow().map_err(|e| e.to_string())?;
    if buffers.len() != app.len() {
        return Err(
            "Recovery snapshot is incomplete. Existing recovery data has been retained.".into(),
        );
    }
    let active = buffers.iter().position(|buffer| buffer.active).unwrap_or(0);
    let documents = buffers
        .into_iter()
        .map(|buffer| {
            let mut document = app.snapshot(DocumentId(buffer.id), buffer.text)?;
            if !buffer.view.is_empty() {
                let state: star_io::ViewState =
                    serde_json::from_str(&buffer.view).map_err(|e| e.to_string())?;
                state.validate(&document.text)?;
                document.view = Some(state);
            }
            Ok::<_, String>(document)
        })
        .collect::<Result<_, _>>()?;
    let tab_width = controller
        .settings
        .try_borrow()
        .map_err(|e| e.to_string())?
        .as_ref()
        .map(|store| store.value.editor.tab_width)
        .unwrap_or(4);
    Ok(Session {
        schema: 1,
        documents,
        preferences: Preferences {
            dark,
            wrap,
            tab_width,
        },
        layout: Some(star_io::SessionLayout { active }),
    })
}
fn checkpoint(
    controller: &Controller,
    buffers: Vec<ffi::Buffer>,
    dark: bool,
    wrap: bool,
) -> Result<(), String> {
    let session = make_session(controller, buffers, dark, wrap)?;
    let recovery = controller
        .recovery
        .try_borrow()
        .map_err(|e| e.to_string())?;
    let store = recovery
        .as_ref()
        .ok_or("Recovery storage is not initialized.")?;
    store.checkpoint(&session)?;
    if controller.recovered.get() {
        if let Some(pending) = controller
            .pending
            .try_borrow_mut()
            .map_err(|e| e.to_string())?
            .take()
        {
            pending.acknowledge()?;
        }
    }
    Ok(())
}
fn retain_workspace(
    controller: &Controller,
    buffers: Vec<ffi::Buffer>,
    dark: bool,
    wrap: bool,
) -> Result<(), String> {
    checkpoint(controller, buffers, dark, wrap)?;
    controller
        .recovery
        .try_borrow()
        .map_err(|e| e.to_string())?
        .as_ref()
        .ok_or("Recovery storage is not initialized.")?
        .enable_automatic_resume()
}
fn legacy_session_allowed(controller: &Controller) -> bool {
    controller.legacy_session_allowed.get()
}
fn resume_notices(controller: &Controller) -> Result<String, String> {
    Ok(controller
        .resume_notices
        .try_borrow()
        .map_err(|e| e.to_string())?
        .join("\n\n"))
}
fn finish_recovery(controller: &Controller) -> Result<(), String> {
    let mut recovery = controller
        .recovery
        .try_borrow_mut()
        .map_err(|e| e.to_string())?;
    if let Some(store) = recovery.as_ref() {
        store.finish()?;
    }
    recovery.take();
    Ok(())
}
fn restore_records(
    controller: &Controller,
    records: Vec<star_io::RecoveryDocument>,
    automatic: bool,
) -> Result<Vec<ffi::Opened>, String> {
    let mut app = controller.app.try_borrow_mut().map_err(|e| e.to_string())?;
    let mut trial = app.clone();
    let mut opened = Vec::new();
    let mut notices = Vec::new();
    let mut total = 0;
    for mut record in records {
        if let Some(view) = &record.view {
            if view
                .udl_xml_base64
                .as_ref()
                .is_some_and(|xml| !ffi::valid_udl(xml))
            {
                return Err("Stored UDL profile is invalid or unsupported.".into());
            }
            if !view.lexer.is_empty() && !ffi::lexer_exists(&view.lexer) {
                return Err(format!("Unknown session lexer: {}", view.lexer));
            }
        }
        let id = if automatic {
            let (id, warnings) = trial.resume(&mut record)?;
            notices.extend(warnings);
            id
        } else {
            // Manual imports require a deliberate save or discard.
            record.dirty = true;
            trial.restore(&record)?
        };
        total += record.text.len()
            + record
                .view
                .as_ref()
                .and_then(|view| view.udl_xml_base64.as_ref())
                .map(String::len)
                .unwrap_or(0);
        if total > star_io::MAX_RECOVERY_BYTES {
            return Err(
                "Reopened workspace exceeds the recovery budget. Original snapshots were retained."
                    .into(),
            );
        }
        opened.push(ffi::Opened {
            id: id.0,
            text: record.text,
            encoding: record.encoding.label().into(),
            existing: false,
            dirty: record.dirty,
            view: record
                .view
                .map(|view| serde_json::to_string(&view))
                .transpose()
                .map_err(|e| e.to_string())?
                .unwrap_or_default(),
        });
    }
    *controller
        .resume_notices
        .try_borrow_mut()
        .map_err(|e| e.to_string())? = notices;
    *app = trial;
    Ok(opened)
}
fn restore_recovery(controller: &Controller) -> Result<Vec<ffi::Opened>, String> {
    restore_pending(controller, false)
}
fn resume_workspace(controller: &Controller) -> Result<Vec<ffi::Opened>, String> {
    restore_pending(controller, true)
}
fn restore_pending(controller: &Controller, automatic: bool) -> Result<Vec<ffi::Opened>, String> {
    let pending = controller.pending.try_borrow().map_err(|e| e.to_string())?;
    let records = pending
        .as_ref()
        .map(|batch| {
            batch
                .sessions
                .iter()
                .flat_map(|session| session.documents.clone())
                .collect()
        })
        .unwrap_or_default();
    let opened = restore_records(controller, records, automatic)?;
    if let Some(session) = pending.as_ref().and_then(|batch| batch.sessions.first()) {
        set_imported_layout(controller, session)?;
    }
    controller.recovered.set(true);
    Ok(opened)
}
fn save_session(
    controller: &Controller,
    buffers: Vec<ffi::Buffer>,
    path: &ffi::FilePath,
    dark: bool,
    wrap: bool,
) -> Result<(), String> {
    star_io::save_session(
        &path_from(path)?,
        &make_session(controller, buffers, dark, wrap)?,
    )
}
fn load_session(controller: &Controller, path: &ffi::FilePath) -> Result<Vec<ffi::Opened>, String> {
    let session = star_io::load_session(&path_from(path)?)?;
    let opened = restore_records(controller, session.documents.clone(), false)?;
    set_imported_layout(controller, &session)?;
    Ok(opened)
}
fn set_imported_layout(controller: &Controller, session: &Session) -> Result<(), String> {
    *controller.imported_layout.try_borrow_mut().map_err(|e| e.to_string())? = serde_json::to_string(
        &serde_json::json!({"active": session.layout.as_ref().map(|v| v.active).unwrap_or(0), "editor": session.preferences})
    ).map_err(|e| e.to_string())?;
    Ok(())
}
fn imported_layout(controller: &Controller) -> Result<String, String> {
    Ok(controller
        .imported_layout
        .try_borrow()
        .map_err(|e| e.to_string())?
        .clone())
}
fn reopen_session(
    controller: &Controller,
    path: &ffi::FilePath,
) -> Result<Vec<ffi::Opened>, String> {
    let session = star_io::load_session(&path_from(path)?)?;
    let opened = restore_records(controller, session.documents.clone(), true)?;
    set_imported_layout(controller, &session)?;
    Ok(opened)
}
fn transform_text(operation: &str, text: &str) -> Result<String, String> {
    star_core::transform::transform(operation, text)
}
fn validate_macro(json: &str) -> Result<(), String> {
    if json.len() > 2 * 1024 * 1024 {
        return Err("Macro file exceeds its size limit.".into());
    }
    serde_json::from_str::<star_core::macros::Macro>(json)
        .map_err(|e| e.to_string())?
        .validate()
}
fn macro_repeat_steps(json: &str, repeats: u32) -> Result<u32, String> {
    if json.len() > 2 * 1024 * 1024 {
        return Err("Macro file exceeds its size limit.".into());
    }
    serde_json::from_str::<star_core::macros::Macro>(json)
        .map_err(|e| e.to_string())?
        .playback_steps(repeats)
}
fn format_json(json: &str) -> Result<String, String> {
    star_core::json_tools::format(json, true)
}
fn minify_json(json: &str) -> Result<String, String> {
    star_core::json_tools::format(json, false)
}
fn json_preview(json: &str) -> Result<String, String> {
    serde_json::to_string(&star_core::json_tools::preview(json)?).map_err(|e| e.to_string())
}
fn compare_text(left: &str, right: &str, ignore_whitespace: bool) -> Result<String, String> {
    serde_json::to_string(&star_core::comparison::compare(
        left,
        right,
        ignore_whitespace,
    )?)
    .map_err(|e| e.to_string())
}
fn compare_documents(left: &str, right: &str, options: &str) -> Result<String, String> {
    if options.len() > 4096 {
        return Err("Comparison options exceed their limit.".into());
    }
    let options =
        serde_json::from_str(options).map_err(|e| format!("Invalid comparison options: {e}"))?;
    serde_json::to_string(&star_core::comparison::compare_with_options(
        left, right, &options,
    )?)
    .map_err(|e| e.to_string())
}
fn document_statistics(text: &str) -> Result<String, String> {
    serde_json::to_string(&star_core::statistics::summarize(text)?).map_err(|e| e.to_string())
}
fn document_revision(controller: &Controller, id: u64) -> Result<u64, String> {
    Ok(controller
        .app
        .try_borrow()
        .map_err(|e| e.to_string())?
        .document(DocumentId(id))?
        .revision)
}
fn document_changed_on_disk(controller: &Controller, id: u64) -> Result<bool, String> {
    let app = controller.app.try_borrow().map_err(|e| e.to_string())?;
    let document = app.document(DocumentId(id))?;
    match &document.path {
        Some(path) => Ok(star_io::target_stamp(path)? != document.expected),
        None => Ok(false),
    }
}
fn load_settings(controller: &Controller, path: &ffi::FilePath) -> Result<String, String> {
    let store = SettingsStore::open(&path_from(path)?)?;
    validate_settings_commands(&store.value)?;
    let json = serde_json::to_string(&store.value).map_err(|e| e.to_string())?;
    *controller
        .settings
        .try_borrow_mut()
        .map_err(|e| e.to_string())? = Some(store);
    Ok(json)
}
fn current_settings(controller: &Controller) -> Result<String, String> {
    let store = controller
        .settings
        .try_borrow()
        .map_err(|e| e.to_string())?;
    serde_json::to_string(&store.as_ref().ok_or("Settings are not initialized.")?.value)
        .map_err(|e| e.to_string())
}
fn validate_settings_json(json: &str) -> Result<(), String> {
    if json.len() > 1024 * 1024 {
        return Err("Settings exceed their size limit.".into());
    }
    let value = Settings::from_json(json.as_bytes())?;
    validate_settings_commands(&value)
}
fn normalize_settings_json(json: &str) -> Result<String, String> {
    validate_settings_json(json)?;
    let settings = Settings::from_json(json.as_bytes())?;
    serde_json::to_string(&settings).map_err(|e| e.to_string())
}
fn reset_settings(controller: &Controller, path: &ffi::FilePath) -> Result<ffi::FilePath, String> {
    let mut settings = controller
        .settings
        .try_borrow_mut()
        .map_err(|e| e.to_string())?;
    let (store, backup) = SettingsStore::reset_with_backup(&path_from(path)?)?;
    *settings = Some(store);
    Ok(path_value(&backup))
}
fn import_settings(controller: &Controller, json: &str) -> Result<ffi::FilePath, String> {
    validate_settings_json(json)?;
    let value = Settings::from_json(json.as_bytes())?;
    let path = controller
        .settings
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .as_mut()
        .ok_or("Settings are unavailable; no profile was overwritten.")?
        .import(value)?;
    Ok(path_value(&path))
}
fn store_settings(controller: &Controller, json: &str) -> Result<(), String> {
    if json.len() > 1024 * 1024 {
        return Err("Settings exceed their size limit.".into());
    }
    let value = Settings::from_json(json.as_bytes())?;
    validate_settings_commands(&value)?;
    controller
        .settings
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .as_mut()
        .ok_or("Settings are unavailable; the existing settings file has been retained.")?
        .save(value)
}
fn validate_settings_commands(value: &Settings) -> Result<(), String> {
    value.validate()?;
    let definitions = star_core::commands().map_err(|e| e.to_string())?;
    let mut effective = std::collections::HashSet::new();
    for id in value.shortcuts.keys() {
        serde_json::from_value::<CommandId>(serde_json::Value::String(id.clone()))
            .map_err(|_| format!("Unknown shortcut command: {id}"))?;
    }
    for command in definitions {
        let id = serde_json::to_value(command.id).map_err(|e| e.to_string())?;
        let key = id.as_str().ok_or("Invalid command ID")?;
        let shortcut = value.shortcuts.get(key).unwrap_or(&command.shortcut);
        if !shortcut.is_empty() && !effective.insert(shortcut.to_lowercase()) {
            return Err(format!("Shortcut conflict: {shortcut}"));
        }
    }
    Ok(())
}
fn search_request(
    text: &str,
    query: &str,
    replacement: &str,
    options: &ffi::SearchOptions,
) -> Result<String, String> {
    let request = star_search::Request {
        text: text.into(),
        query: query.into(),
        replacement: replacement.into(),
        mode: options.mode.clone(),
        match_case: options.match_case,
        whole_word: options.whole_word,
        dot_newline: options.dot_newline,
        replace: options.replace,
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
    }
    .prepare()?;
    serde_json::to_string(&request).map_err(|e| e.to_string())
}
fn search_response(json: &str, source: &str) -> Result<ffi::SearchOutput, String> {
    if json.len() > star_search::MAX_PROTOCOL {
        return Err("Search response is too large.".into());
    }
    let response: star_search::Response =
        serde_json::from_str(json).map_err(|e| format!("Invalid worker response: {e}"))?;
    response.validate(response.source.as_deref().unwrap_or(source))?;
    Ok(ffi::SearchOutput {
        hits: response
            .matches
            .into_iter()
            .map(|hit| ffi::SearchHit {
                start: hit.start,
                end: hit.end,
                line: hit.line,
                preview: hit.preview,
            })
            .collect(),
        count: response.count,
        replaced: response.replacement_text.is_some(),
        text: response.replacement_text.unwrap_or_default(),
        stamp: response.source_stamp.unwrap_or_default(),
        encoding: response
            .source_encoding
            .map(|value| value.label().to_owned())
            .unwrap_or_default(),
        next_position: response.next_position.unwrap_or(0),
    })
}
pub fn worker(list: bool) -> Result<(), String> {
    use std::io::{Read, Write};
    let mut input = Vec::new();
    std::io::stdin()
        .lock()
        .take(star_search::MAX_PROTOCOL as u64 + 1)
        .read_to_end(&mut input)
        .map_err(|e| e.to_string())?;
    if input.len() > star_search::MAX_PROTOCOL {
        return Err("Worker input exceeds its limit.".into());
    }
    if list {
        let request: star_search::FileRequest =
            serde_json::from_slice(&input).map_err(|e| e.to_string())?;
        let response = star_search::list_files(request)?;
        let bytes = serde_json::to_vec(&response).map_err(|e| e.to_string())?;
        return std::io::stdout()
            .lock()
            .write_all(&bytes)
            .map_err(|e| e.to_string());
    }
    let request: star_search::Request =
        serde_json::from_slice(&input).map_err(|e| e.to_string())?;
    let request = request.prepare()?;
    let end = request.end.unwrap_or(request.text.len() as u64);
    let source = if request.file.is_some() {
        Some(request.text.clone())
    } else {
        None
    };
    let result = ffi::search_snapshot(&ffi::SearchJob {
        text: request.text,
        query: request.query,
        replacement: request.replacement,
        regex: request.mode == "regex",
        match_case: request.match_case,
        whole_word: request.whole_word,
        dot_newline: request.dot_newline,
        replace: request.replace,
        start: request.start,
        end,
        replace_limit: request.replace_limit,
        reverse: request.reverse,
        first_only: request.first_only,
        wrap: request.wrap,
        require_full_range: request.require_full_range,
    })
    .map_err(|e| e.to_string())?;
    let response = star_search::Response {
        matches: result
            .hits
            .into_iter()
            .map(|hit| star_search::Match {
                start: hit.start,
                end: hit.end,
                line: hit.line,
                preview: hit.preview,
            })
            .collect(),
        count: result.count,
        replacement_text: if result.replaced {
            Some(result.text)
        } else {
            None
        },
        source,
        source_stamp: request.source_stamp,
        source_encoding: request.file_encoding,
        next_position: if result.replaced {
            Some(result.next_position)
        } else {
            None
        },
    };
    let bytes = serde_json::to_vec(&response).map_err(|e| e.to_string())?;
    if bytes.len() > star_search::MAX_PROTOCOL {
        return Err("Worker output exceeds its limit.".into());
    }
    std::io::stdout()
        .lock()
        .write_all(&bytes)
        .map_err(|e| e.to_string())
}
fn scan_request(root: &ffi::FilePath, patterns: &str) -> Result<String, String> {
    serde_json::to_string(&star_search::FileRequest {
        root: NativePath::from_path(&path_from(root)?),
        patterns: patterns.into(),
    })
    .map_err(|e| e.to_string())
}
fn scan_response(json: &str) -> Result<ffi::FileList, String> {
    if json.len() > 1024 * 1024 * 4 {
        return Err("Directory response exceeds its budget.".into());
    }
    let list: star_search::FileResponse = serde_json::from_str(json).map_err(|e| e.to_string())?;
    Ok(ffi::FileList {
        files: list
            .files
            .iter()
            .map(|path| path.to_path().map(|p| path_value(&p)))
            .collect::<Result<_, _>>()?,
        warnings: list.warnings,
    })
}
fn read_search_file(path: &ffi::FilePath, encoding: &str) -> Result<ffi::SearchSource, String> {
    let bytes = star_io::read_limited(&path_from(path)?, star_io::MAX_FILE_BYTES)?;
    let choice = if encoding.is_empty() {
        None
    } else {
        Some(parse_encoding(encoding)?)
    };
    let (text, actual) = star_io::decode(&bytes, choice)?;
    Ok(ffi::SearchSource {
        text,
        stamp: serde_json::to_string(&star_io::stamp(&bytes)).map_err(|e| e.to_string())?,
        encoding: actual.label().into(),
    })
}
fn search_file_encoding(json: &str, encoding: &str) -> Result<String, String> {
    let mut request: star_search::Request =
        serde_json::from_str(json).map_err(|e| e.to_string())?;
    request.file_encoding = if encoding.is_empty() {
        None
    } else {
        Some(parse_encoding(encoding)?)
    };
    serde_json::to_string(&request).map_err(|e| e.to_string())
}
fn search_file_input(json: &str, path: &ffi::FilePath) -> Result<String, String> {
    let mut request: star_search::Request =
        serde_json::from_str(json).map_err(|e| e.to_string())?;
    request.file = Some(NativePath::from_path(&path_from(path)?));
    serde_json::to_string(&request).map_err(|e| e.to_string())
}
fn extension_inspection_request(path: &ffi::FilePath) -> Result<String, String> {
    serde_json::to_string(&NativePath::from_path(&path_from(path)?)).map_err(|e| e.to_string())
}
fn extension_request(metadata: &str, selected: &str) -> Result<String, String> {
    if metadata.len() > star_extensions::MAX_RESPONSE_BYTES
        || selected.len() > star_extensions::MAX_INPUT_BYTES
    {
        return Err("Extension metadata or selection exceeds its limit.".into());
    }
    let package: star_extensions::PackageMetadata =
        serde_json::from_str(metadata).map_err(|e| e.to_string())?;
    serde_json::to_string(&star_extensions::WorkerRequest {
        protocol_version: star_extensions::PROTOCOL_VERSION,
        package: package.directory,
        expected_manifest_sha256: package.manifest_sha256,
        granted_capabilities: vec![star_extensions::Capability::SelectedText],
        selected_text: selected.into(),
    })
    .map_err(|e| e.to_string())
}
fn extension_response(json: &str) -> Result<String, String> {
    if json.len() > star_extensions::MAX_RESPONSE_BYTES {
        return Err("Extension response exceeds its limit.".into());
    }
    match serde_json::from_str::<star_extensions::WorkerResponse>(json)
        .map_err(|e| e.to_string())?
    {
        star_extensions::WorkerResponse::Success {
            protocol_version,
            proposal,
        } => {
            if protocol_version != star_extensions::PROTOCOL_VERSION
                || proposal.replacement.len() > star_extensions::MAX_OUTPUT_BYTES
                || proposal.replacement.contains('\0')
            {
                return Err("Unsupported extension response or non-text output.".into());
            }
            Ok(proposal.replacement)
        }
        star_extensions::WorkerResponse::Error { error, .. } => Err(error.message),
    }
}
fn create_extension_example(path: &ffi::FilePath) -> Result<String, String> {
    let metadata = star_extensions::create_example(&path_from(path)?).map_err(|e| e.to_string())?;
    serde_json::to_string(&metadata).map_err(|e| e.to_string())
}
fn management_request(action: &str) -> Result<String, String> {
    use star_extensions::managed::{
        ManagementAction, ManagementRequest, MANAGEMENT_PROTOCOL_VERSION,
    };
    if action.len() > star_extensions::MAX_REQUEST_BYTES {
        return Err("Extension management request exceeds its limit.".into());
    }
    let action: ManagementAction = serde_json::from_str(action).map_err(|e| e.to_string())?;
    serde_json::to_string(&ManagementRequest {
        protocol_version: MANAGEMENT_PROTOCOL_VERSION,
        action,
    })
    .map_err(|e| e.to_string())
}
fn management_response(json: &str) -> Result<String, String> {
    use star_extensions::managed::{ManagementResponse, MANAGEMENT_PROTOCOL_VERSION};
    if json.len() > star_extensions::MAX_RESPONSE_BYTES {
        return Err("Extension management response exceeds its limit.".into());
    }
    match serde_json::from_str::<ManagementResponse>(json).map_err(|e| e.to_string())? {
        ManagementResponse::Success {
            protocol_version,
            result,
        } if protocol_version == MANAGEMENT_PROTOCOL_VERSION => {
            serde_json::to_string(&result).map_err(|e| e.to_string())
        }
        ManagementResponse::Error {
            protocol_version,
            error,
        } if protocol_version == MANAGEMENT_PROTOCOL_VERSION => Err(error.to_string()),
        _ => Err("Unsupported extension management protocol version.".into()),
    }
}
fn managed_extension_request(
    root: &ffi::FilePath,
    id: &str,
    selected: &str,
) -> Result<String, String> {
    use star_extensions::managed::{ManagedId, ManagedRunRequest};
    if selected.len() > star_extensions::MAX_INPUT_BYTES {
        return Err("Extension selection exceeds its limit.".into());
    }
    serde_json::to_string(&ManagedRunRequest {
        protocol_version: star_extensions::PROTOCOL_VERSION,
        managed_root: NativePath::from_path(&path_from(root)?),
        managed_id: ManagedId::parse(id).map_err(|e| e.to_string())?,
        granted_capabilities: vec![star_extensions::Capability::SelectedText],
        selected_text: selected.into(),
    })
    .map_err(|e| e.to_string())
}
pub fn extension_management_worker() -> Result<(), String> {
    star_extensions::managed::management_worker().map_err(|e| e.to_string())
}
pub fn extension_worker(inspect: bool) -> Result<(), String> {
    use std::io::{Read, Write};
    if !inspect {
        return star_extensions::worker().map_err(|e| e.to_string());
    }
    let mut input = Vec::new();
    std::io::stdin()
        .lock()
        .take(16385)
        .read_to_end(&mut input)
        .map_err(|e| e.to_string())?;
    if input.len() > 16384 {
        return Err("Extension inspection request exceeds its limit.".into());
    }
    let path: NativePath = serde_json::from_slice(&input).map_err(|e| e.to_string())?;
    let metadata = star_extensions::inspect_package(&path.to_path()?).map_err(|e| e.to_string())?;
    let output = serde_json::to_vec(&metadata).map_err(|e| e.to_string())?;
    std::io::stdout()
        .lock()
        .write_all(&output)
        .map_err(|e| e.to_string())
}
fn disk_request(
    controller: &Controller,
    proposals: Vec<ffi::DiskProposal>,
    backup: &ffi::FilePath,
) -> Result<String, String> {
    let app = controller.app.try_borrow().map_err(|e| e.to_string())?;
    let mut edits = Vec::new();
    for proposal in proposals {
        let path = path_from(&proposal.path)?;
        app.disk_replacement_documents(&path)?;
        edits.push(star_search::batch_replace::Edit {
            path: NativePath::from_path(&path),
            expected: serde_json::from_str(&proposal.stamp).map_err(|e| e.to_string())?,
            replacement: proposal.text,
            encoding: if proposal.encoding.is_empty() {
                None
            } else {
                Some(parse_encoding(&proposal.encoding)?)
            },
        });
    }
    serde_json::to_string(&star_search::batch_replace::Request {
        schema: 1,
        backup_directory: NativePath::from_path(&path_from(backup)?),
        edits,
    })
    .map_err(|e| e.to_string())
}
fn disk_response(json: &str) -> Result<Vec<ffi::DiskOutcome>, String> {
    if json.len() > 8 * 1024 * 1024 {
        return Err("Disk operation response exceeds its limit.".into());
    }
    let result: star_search::batch_replace::Response =
        serde_json::from_str(json).map_err(|e| e.to_string())?;
    if result.schema != 1 || result.outcomes.len() > 128 {
        return Err("Invalid disk-operation response.".into());
    }
    result
        .outcomes
        .into_iter()
        .map(|outcome| {
            Ok(ffi::DiskOutcome {
                path: path_value(&outcome.path.to_path()?),
                applied: outcome.applied,
                message: outcome.message,
            })
        })
        .collect()
}
fn normalize_replacement(mode: &str, value: &str) -> Result<String, String> {
    if mode == "extended" {
        star_search::extended(value)
    } else {
        Ok(value.into())
    }
}
fn disk_updated_documents(
    controller: &Controller,
    path: &ffi::FilePath,
) -> Result<Vec<u64>, String> {
    Ok(controller
        .app
        .try_borrow()
        .map_err(|e| e.to_string())?
        .disk_replacement_documents(&path_from(path)?)?
        .into_iter()
        .map(|id| id.0)
        .collect())
}
pub fn disk_worker(mode: &str) -> Result<(), String> {
    use std::io::{Read, Write};
    let mut bytes = Vec::new();
    std::io::stdin()
        .lock()
        .take(star_search::MAX_PROTOCOL as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|e| e.to_string())?;
    if bytes.len() > star_search::MAX_PROTOCOL {
        return Err("Disk operation exceeds its protocol limit.".into());
    }
    if mode == "--disk-inspect-worker" {
        let directory: NativePath = serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
        let response = star_search::FileResponse {
            files: star_search::batch_replace::inspect(&directory.to_path()?)?,
            warnings: Vec::new(),
        };
        let output = serde_json::to_vec(&response).map_err(|e| e.to_string())?;
        return std::io::stdout()
            .lock()
            .write_all(&output)
            .map_err(|e| e.to_string());
    }
    let result = if mode == "--disk-restore-worker" {
        let directory: NativePath = serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
        star_search::batch_replace::restore(&directory.to_path()?)?
    } else {
        let request: star_search::batch_replace::Request =
            serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
        star_search::batch_replace::apply(request)?
    };
    let output = serde_json::to_vec(&result).map_err(|e| e.to_string())?;
    std::io::stdout()
        .lock()
        .write_all(&output)
        .map_err(|e| e.to_string())
}
fn outline_request(text: &str, parser: &str) -> Result<String, String> {
    let request = star_search::outline::Request {
        text: text.into(),
        parser: parser.into(),
    };
    request.validate()?;
    serde_json::to_string(&request).map_err(|e| e.to_string())
}
fn outline_response(json: &str, source: &str) -> Result<Vec<ffi::OutlineSymbol>, String> {
    if json.len() > 8 * 1024 * 1024 {
        return Err("Function-list response exceeds its budget.".into());
    }
    let response: star_search::outline::Response =
        serde_json::from_str(json).map_err(|e| e.to_string())?;
    response.validate(source)?;
    Ok(response
        .symbols
        .into_iter()
        .map(|item| ffi::OutlineSymbol {
            name: item.name,
            group: item.group,
            start: item.start,
            end: item.end,
            line: item.line,
            group_start: item.group_start,
        })
        .collect())
}
pub fn outline_worker() -> Result<(), String> {
    use std::io::{Read, Write};
    let mut input = Vec::new();
    std::io::stdin()
        .lock()
        .take(star_search::outline::MAX_PROTOCOL as u64 + 1)
        .read_to_end(&mut input)
        .map_err(|e| e.to_string())?;
    if input.len() > star_search::outline::MAX_PROTOCOL {
        return Err("Function-list input exceeds its budget.".into());
    }
    let request: star_search::outline::Request =
        serde_json::from_slice(&input).map_err(|e| e.to_string())?;
    request.validate()?;
    let symbols =
        ffi::outline_snapshot(&request.text, &request.parser).map_err(|e| e.to_string())?;
    let response = star_search::outline::Response {
        symbols: symbols
            .into_iter()
            .map(|item| star_search::outline::Symbol {
                name: item.name,
                group: item.group,
                start: item.start,
                end: item.end,
                line: item.line,
                group_start: item.group_start,
            })
            .collect(),
    };
    response.validate(&request.text)?;
    let output = serde_json::to_vec(&response).map_err(|e| e.to_string())?;
    if output.len() > 8 * 1024 * 1024 {
        return Err("Function-list output exceeds its budget.".into());
    }
    std::io::stdout()
        .lock()
        .write_all(&output)
        .map_err(|e| e.to_string())
}
fn document_tab(controller: &Controller, id: u64) -> Result<ffi::TabPresentation, String> {
    let state = controller
        .app
        .try_borrow()
        .map_err(|e| e.to_string())?
        .document(DocumentId(id))?
        .tab;
    Ok(ffi::TabPresentation {
        pinned: state.pinned,
        color: state.color,
    })
}
fn set_document_tab(
    controller: &Controller,
    id: u64,
    pinned: bool,
    color: u8,
) -> Result<(), String> {
    controller
        .app
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .set_tab(DocumentId(id), pinned, color)
}
fn document_renamed(controller: &Controller, id: u64, path: &ffi::FilePath) -> Result<(), String> {
    controller
        .app
        .try_borrow_mut()
        .map_err(|e| e.to_string())?
        .renamed(DocumentId(id), path_from(path)?)
}
fn needs_large_preview(controller: &Controller, path: &ffi::FilePath) -> Result<bool, String> {
    let path = std::path::absolute(path_from(path)?).map_err(|e| e.to_string())?;
    if controller
        .app
        .try_borrow()
        .map_err(|e| e.to_string())?
        .is_open(&path)
    {
        return Ok(false);
    }
    let metadata = std::fs::metadata(&path).map_err(|e| e.to_string())?;
    if !metadata.is_file() {
        return Err("Choose a regular file.".into());
    }
    Ok(metadata.len() > star_io::MAX_FILE_BYTES as u64)
}
fn read_large_page(path: &ffi::FilePath, offset: u64) -> Result<ffi::LargePage, String> {
    let page = star_io::large_file::read_page(&path_from(path)?, offset)?;
    Ok(ffi::LargePage {
        text: page.text,
        offset: page.offset,
        next: page.next,
        size: page.size,
        hex: page.hex,
    })
}
fn advanced_transform(
    operation: &str,
    text: &str,
    options: &ffi::TextOperationOptions,
) -> Result<String, String> {
    use star_core::advanced::*;
    let tabs = TabOptions {
        width: options.tab_width as usize,
        first_column: options.first_column as usize,
    };
    let operation = match operation {
        "proper_case" => Operation::Case(CaseMode::ProperForce),
        "proper_blend" => Operation::Case(CaseMode::ProperBlend),
        "sentence_case" => Operation::Case(CaseMode::SentenceForce),
        "sentence_blend" => Operation::Case(CaseMode::SentenceBlend),
        "invert_case" => Operation::Case(CaseMode::Invert),
        "reverse_lines" => Operation::ReverseLines,
        "remove_consecutive_duplicates" => Operation::RemoveConsecutiveDuplicates,
        "remove_empty_lines" => Operation::RemoveEmptyLines {
            whitespace: false,
            final_newline: FinalNewline::Remove,
        },
        "remove_blank_lines" => Operation::RemoveEmptyLines {
            whitespace: true,
            final_newline: FinalNewline::Remove,
        },
        "trim_leading" => Operation::TrimLeading,
        "trim_both" => Operation::TrimBoth,
        "tabs_to_spaces" => Operation::TabsToSpaces(tabs),
        "leading_spaces_to_tabs" => Operation::LeadingSpacesToTabs(tabs),
        "numeric_sort" => {
            let kind = match options.numeric_kind.as_str() {
                "integer" => NumericKind::Integer,
                "dot" => NumericKind::DecimalDot,
                "comma" => NumericKind::DecimalComma,
                _ => return Err("Unknown numeric sort kind.".into()),
            };
            let blanks = match options.blanks.as_str() {
                "reject" => BlankNumbers::Reject,
                "first" => BlankNumbers::First,
                "last" => BlankNumbers::Last,
                _ => return Err("Unknown blank-number policy.".into()),
            };
            Operation::SortNumeric(NumericSortOptions {
                kind,
                order: if options.descending {
                    SortOrder::Descending
                } else {
                    SortOrder::Ascending
                },
                blanks,
            })
        }
        _ => return Err("Unknown text operation.".into()),
    };
    transform(operation, text, Limits::default()).map_err(|error| error.to_string())
}
fn comment_edits(
    text: &str,
    start: u64,
    end: u64,
    mode: &str,
    options: &ffi::CommentOptions,
) -> Result<Vec<ffi::TextEdit>, String> {
    let symbols = star_core::comments::Symbols {
        line: &options.line,
        open: &options.open,
        close: &options.close,
        column_zero: options.column_zero,
        comment_empty: options.comment_empty,
        space: options.space,
    };
    let edits = if mode == "block" || mode == "unblock" {
        star_core::comments::block(
            text,
            usize::try_from(start).map_err(|e| e.to_string())?,
            usize::try_from(end).map_err(|e| e.to_string())?,
            &symbols,
            mode == "unblock",
        )?
    } else {
        star_core::comments::lines(text, &symbols, mode)?
    };
    Ok(edits
        .into_iter()
        .map(|edit| ffi::TextEdit {
            start: edit.start as u64,
            end: edit.end as u64,
            text: edit.text,
        })
        .collect())
}
fn column_values(options: &ffi::ColumnOptions) -> Result<Vec<String>, String> {
    use star_core::advanced::*;
    let start = options
        .start
        .parse::<i128>()
        .map_err(|_| "Invalid signed 128-bit starting number.")?;
    let step = options
        .step
        .parse::<i128>()
        .map_err(|_| "Invalid signed 128-bit increment.")?;
    let base = match options.base.as_str() {
        "decimal" => NumberBase::Decimal,
        "hex" => NumberBase::HexLower,
        "HEX" => NumberBase::HexUpper,
        "octal" => NumberBase::Octal,
        "binary" => NumberBase::Binary,
        _ => return Err("Unknown number base.".into()),
    };
    let padding = match options.padding.as_str() {
        "none" => Padding::None,
        "zero" => Padding::Minimum {
            width: options.width as usize,
            fill: PaddingFill::Zero,
        },
        "space" => Padding::Minimum {
            width: options.width as usize,
            fill: PaddingFill::Space,
        },
        "auto-zero" => Padding::Auto {
            fill: PaddingFill::Zero,
        },
        "auto-space" => Padding::Auto {
            fill: PaddingFill::Space,
        },
        _ => return Err("Unknown padding mode.".into()),
    };
    column_numbers(
        ColumnNumberOptions {
            start,
            step,
            rows: options.rows as usize,
            repeat: options.repeat as usize,
            base,
            padding,
        },
        Limits::default(),
    )
    .map_err(|error| error.to_string())
}
fn forward_request(
    settings: &ffi::LaunchSettings,
    files: &[ffi::FilePath],
) -> Result<String, String> {
    let files = files
        .iter()
        .map(|path| {
            let absolute = std::path::absolute(path_from(path)?).map_err(|e| e.to_string())?;
            Ok(NativePath::from_path(&absolute))
        })
        .collect::<Result<Vec<_>, String>>()?;
    let request = star_core::instance::Request {
        schema: 1,
        files,
        line: settings.line,
        column: settings.column,
        language: settings.language.clone(),
        read_only: settings.read_only,
    };
    request.validate()?;
    let json = serde_json::to_string(&request).map_err(|e| e.to_string())?;
    if json.len() > star_core::instance::MAX_REQUEST {
        return Err("Launch request exceeds 1 MiB.".into());
    }
    Ok(json)
}
fn decode_forward_request(json: &str) -> Result<ffi::ForwardLaunch, String> {
    let request = star_core::instance::Request::decode(json)?;
    Ok(ffi::ForwardLaunch {
        settings: ffi::LaunchSettings {
            preview: false,
            smoke_test: false,
            screenshot: String::new(),
            no_session: false,
            read_only: request.read_only,
            line: request.line,
            column: request.column,
            language: request.language,
            reuse_instance: true,
            profile: ffi::FilePath {
                windows: Vec::new(),
                unix: Vec::new(),
            },
        },
        files: request
            .files
            .iter()
            .map(|path| path.to_path().map(|p| path_value(&p)))
            .collect::<Result<_, _>>()?,
    })
}
fn check_forward_file(path: &ffi::FilePath) -> Result<(), String> {
    let metadata = std::fs::symlink_metadata(path_from(path)?).map_err(|e| e.to_string())?;
    if !metadata.is_file()
        || metadata.file_type().is_symlink()
        || metadata.len() > star_io::MAX_FILE_BYTES as u64
    {
        return Err("Forwarding requires a regular local editable file (up to 32 MiB). Use an independent window for a byte preview.".into());
    }
    Ok(())
}
fn forward_response(opened: u32, errors: Vec<String>) -> Result<String, String> {
    star_core::instance::Response {
        schema: 1,
        opened,
        errors,
    }
    .encode()
}
fn accept_forward_response(json: &str) -> Result<(), String> {
    star_core::instance::Response::accept(json)
}
pub fn instance_client_worker() -> Result<(), String> {
    use std::io::{Read, Write};
    let mut bytes = Vec::new();
    std::io::stdin()
        .lock()
        .take(2 * 1024 * 1024 + 1)
        .read_to_end(&mut bytes)
        .map_err(|e| e.to_string())?;
    if bytes.len() > 2 * 1024 * 1024 {
        return Err("Instance client input exceeds its limit.".into());
    }
    let input: star_core::instance::ClientRequest =
        serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
    let response =
        ffi::instance_client(&input.endpoint, &input.payload).map_err(|e| e.to_string())?;
    std::io::stdout()
        .lock()
        .write_all(response.as_bytes())
        .map_err(|e| e.to_string())
}
pub fn run(options: &star_core::launch::Options) -> Result<i32, cxx::Exception> {
    ffi::run_desktop(
        &ffi::LaunchSettings {
            preview: options.preview,
            smoke_test: options.smoke_test,
            screenshot: options.screenshot.clone(),
            read_only: options.read_only,
            no_session: options.no_session,
            line: options.line,
            column: options.column,
            language: options.language.clone(),
            reuse_instance: options.reuse_instance,
            profile: options
                .profile
                .as_ref()
                .map(|path| path_value(path))
                .unwrap_or(ffi::FilePath {
                    windows: Vec::new(),
                    unix: Vec::new(),
                }),
        },
        options.files.iter().map(|path| path_value(path)).collect(),
    )
}
