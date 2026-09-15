use super::*;

#[test]
fn unicode_encodings_round_trip_byte_exactly() {
    let text = "café 日本語 🚀\r\none\ntwo\rthree";
    for encoding in [
        Encoding::Utf8,
        Encoding::Utf8Bom,
        Encoding::Utf16Le,
        Encoding::Utf16Be,
    ] {
        let bytes = encode(text, encoding).unwrap();
        let (decoded, actual) = decode(&bytes, None).unwrap();
        assert_eq!(decoded, text);
        assert_eq!(actual, encoding);
        assert_eq!(encode(&decoded, actual).unwrap(), bytes);
    }

    assert!(decode(b"\xC0\xAF", None).is_err());
    assert!(decode(b"\xFF\xFE\x00\xD8", None).is_err());
    assert!(decode(b"\xFF\xFE\x01", None).is_err());
    assert!(decode(b"a\0b", None).is_err());
    assert!(encode("🚀", Encoding::Windows1252).is_err());
    let bytes = encode("café", Encoding::Windows1252).unwrap();
    assert_eq!(
        decode(&bytes, Some(Encoding::Windows1252)).unwrap().0,
        "café"
    );
    let bytes = encode("日本語", Encoding::ShiftJis).unwrap();
    assert_eq!(
        decode(&bytes, Some(Encoding::ShiftJis)).unwrap().0,
        "日本語"
    );
}

#[test]
fn complete_encoding_catalog_round_trips_files_and_session_identifiers() {
    let catalog = Encoding::all();
    assert_eq!(catalog.len(), 52);
    let mut labels = std::collections::HashSet::new();
    for encoding in catalog {
        assert!(labels.insert(encoding.label()));
        assert_eq!(Encoding::from_label(encoding.label()).unwrap(), encoding);
        let json = serde_json::to_string(&encoding).unwrap();
        assert_eq!(serde_json::from_str::<Encoding>(&json).unwrap(), encoding);
        let encoded = encode("ASCII\r\n", encoding).unwrap();
        assert_eq!(
            decode(&encoded, Some(encoding)).unwrap(),
            ("ASCII\r\n".into(), encoding)
        );
    }
    assert_eq!(
        serde_json::to_string(&Encoding::Windows1252).unwrap(),
        "\"windows1252\""
    );
    assert_eq!(
        serde_json::to_string(&Encoding::ShiftJis).unwrap(),
        "\"shift_jis\""
    );
    assert!(serde_json::from_str::<Encoding>("{\"code_page\":0}").is_err());
    assert_eq!(
        decode(b"no marker", Some(Encoding::Utf8Bom)).unwrap().1,
        Encoding::Utf8
    );
    assert_eq!(
        decode(b"a\0", Some(Encoding::Utf16Le)).unwrap().1,
        Encoding::Utf16LeNoBom
    );
    assert_eq!(
        decode(b"\xFF\xFEa\0", Some(Encoding::Utf16LeNoBom))
            .unwrap()
            .1,
        Encoding::Utf16Le
    );
    for encoding in [Encoding::Utf16LeNoBom, Encoding::Utf16BeNoBom] {
        let bytes = encode("text", encoding).unwrap();
        assert!(!bytes.starts_with(b"\xFF\xFE") && !bytes.starts_with(b"\xFE\xFF"));
        assert_eq!(decode(&bytes, Some(encoding)).unwrap().0, "text");
    }
}

#[test]
fn legacy_expansion_cannot_exceed_the_editable_buffer_budget() {
    let bytes = vec![0x80; MAX_FILE_BYTES / 3 + 1];
    assert!(decode(&bytes, Some(Encoding::Windows1252))
        .unwrap_err()
        .contains("editable-buffer limit"));
}

#[test]
fn session_bookmarks_are_bounded_without_changing_legacy_checksums() {
    let mut view = ViewState::default();
    assert!(!serde_json::to_string(&view).unwrap().contains("bookmarks"));
    view.bookmarks = Some(vec![0, 1, 2, 3]);
    view.validate("a\r\nb\rc\n").unwrap();
    view.bookmarks = Some(vec![0, 4]);
    assert!(view.validate("a\r\nb\rc\n").is_err());
    view.bookmarks = Some(vec![1, 1]);
    assert!(view.validate("a\nb").is_err());
    view.bookmarks = Some(vec![1, 0]);
    assert!(view.validate("a\nb").is_err());
}

#[test]
fn saves_preserve_existing_data_on_conflict_or_failure() {
    let root = tempfile::tempdir().unwrap();
    let path = root.path().join("Unicode 日本.txt");
    let first = atomic_write(&path, b"first", None, MAX_FILE_BYTES).unwrap();
    let second = atomic_write(&path, b"second", Some(&first), MAX_FILE_BYTES).unwrap();
    assert!(atomic_write(&path, b"stale", Some(&first), MAX_FILE_BYTES).is_err());
    assert!(atomic_write(&path, b"new", None, MAX_FILE_BYTES).is_err());
    assert_eq!(fs::read(&path).unwrap(), b"second");
    let original_permissions = fs::metadata(&path).unwrap().permissions();
    let mut permissions = original_permissions.clone();
    permissions.set_readonly(true);
    fs::set_permissions(&path, permissions).unwrap();
    assert!(atomic_write(&path, b"overwrite", Some(&second), MAX_FILE_BYTES).is_err());
    fs::set_permissions(&path, original_permissions).unwrap();
    fs::remove_file(&path).unwrap();
    assert!(atomic_write(&path, b"deleted", Some(&second), MAX_FILE_BYTES).is_err());
    assert_eq!(fs::read_dir(root.path()).unwrap().count(), 0);
}

#[test]
fn limits_are_exact_and_sessions_do_not_touch_originals() {
    assert!(decode(&vec![b'a'; MAX_FILE_BYTES], None).is_ok());
    assert!(decode(&vec![b'a'; MAX_FILE_BYTES + 1], None).is_err());
    let root = tempfile::tempdir().unwrap();
    let original = root.path().join("original.txt");
    fs::write(&original, "disk").unwrap();
    let session = Session {
        schema: 1,
        documents: vec![RecoveryDocument {
            path: Some(NativePath::from_path(&original)),
            name: "original.txt".into(),
            text: "unsaved".into(),
            encoding: Encoding::Utf8,
            saved_encoding: None,
            expected: Some(stamp(b"disk")),
            dirty: true,
            view: None,
            tab: None,
        }],
        preferences: Preferences::default(),
        layout: None,
    };
    let recovery = root.path().join("recovery");
    let store = RecoveryStore::create(&recovery).unwrap();
    store.checkpoint(&session).unwrap();
    assert!(RecoveryStore::pending(&recovery).unwrap().is_empty());
    drop(store);
    let recovered = RecoveryStore::pending(&recovery).unwrap();
    assert_eq!(recovered[0].documents[0].text, "unsaved");
    assert_eq!(fs::read_to_string(&original).unwrap(), "disk");
    let path = root.path().join("session.json");
    fs::write(&path, b"{truncated").unwrap();
    assert!(load_session(&path).is_err());
    assert_eq!(fs::read(&path).unwrap(), b"{truncated");
}

#[test]
fn recovery_transfer_and_checksum_failure_preserve_documents() {
    let root = tempfile::tempdir().unwrap();
    let session = Session {
        schema: 1,
        documents: vec![RecoveryDocument {
            path: None,
            name: "new".into(),
            text: "must survive".into(),
            encoding: Encoding::Utf8,
            saved_encoding: None,
            expected: None,
            dirty: true,
            view: None,
            tab: None,
        }],
        preferences: Preferences::default(),
        layout: None,
    };
    let original = RecoveryStore::create(root.path()).unwrap();
    original.checkpoint(&session).unwrap();
    drop(original);
    let batch = RecoveryStore::claim(root.path()).unwrap();
    assert_eq!(batch.sessions.len(), 1);
    assert!(RecoveryStore::pending(root.path()).unwrap().is_empty());
    let next = RecoveryStore::create(root.path()).unwrap();
    next.checkpoint(&batch.sessions[0]).unwrap();
    batch.acknowledge().unwrap();
    drop(next);
    assert_eq!(
        RecoveryStore::pending(root.path()).unwrap()[0].documents[0].text,
        "must survive"
    );
    let path = root.path().join("manual.json");
    save_session(&path, &session).unwrap();
    let mut json: serde_json::Value = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    json["session"]["documents"][0]["text"] = "corrupted".into();
    fs::write(&path, serde_json::to_vec(&json).unwrap()).unwrap();
    assert!(load_session(&path).unwrap_err().contains("checksum"));
    assert!(path.exists());
}

#[test]
fn session_view_ranges_are_validated_without_breaking_legacy_snapshots() {
    let root = tempfile::tempdir().unwrap();
    let file = root.path().join("session.json");
    let mut session = Session {
        schema: 1,
        documents: vec![RecoveryDocument {
            path: None,
            name: "new".into(),
            text: "éabc".into(),
            encoding: Encoding::Utf8,
            saved_encoding: None,
            expected: None,
            dirty: true,
            view: None,
            tab: None,
        }],
        preferences: Preferences::default(),
        layout: None,
    };
    save_session(&file, &session).unwrap();
    let old_json = fs::read_to_string(&file).unwrap();
    assert!(!old_json.contains("\"view\"") && !old_json.contains("\"layout\""));
    assert!(!old_json.contains("\"saved_encoding\"") && !old_json.contains("\"read_only\""));
    assert!(load_session(&file).unwrap().documents[0].view.is_none());
    session.documents[0].view = Some(ViewState {
        caret: 1,
        ..ViewState::default()
    });
    assert!(save_session(&file, &session).is_err());
    session.documents[0].view.as_mut().unwrap().caret = 2;
    session.layout = Some(SessionLayout { active: 0 });
    save_session(&file, &session).unwrap();
    assert_eq!(
        load_session(&file).unwrap().documents[0]
            .view
            .as_ref()
            .unwrap()
            .caret,
        2
    );
}

#[test]
fn automatic_exit_snapshot_survives_unlock_and_failed_replacement() {
    let root = tempfile::tempdir().unwrap();
    let store = RecoveryStore::create(root.path()).unwrap();
    let session = Session {
        schema: 1,
        documents: vec![RecoveryDocument {
            path: None,
            name: "scratch".into(),
            text: "unsaved 日本 🚀".into(),
            encoding: Encoding::Utf16Le,
            saved_encoding: Some(Encoding::Utf8),
            expected: None,
            dirty: true,
            view: None,
            tab: None,
        }],
        preferences: Preferences::default(),
        layout: None,
    };
    assert!(!RecoveryStore::has_snapshots(root.path()).unwrap());
    assert!(!RecoveryStore::automatic_resume_enabled(root.path()).unwrap());
    store.checkpoint(&session).unwrap();
    store.enable_automatic_resume().unwrap();
    store.enable_automatic_resume().unwrap();
    assert!(RecoveryStore::has_snapshots(root.path()).unwrap());
    assert!(RecoveryStore::pending(root.path()).unwrap().is_empty());
    let path = store.directory.join("session.json");
    let permissions = fs::metadata(&path).unwrap().permissions();
    let mut read_only = permissions.clone();
    read_only.set_readonly(true);
    fs::set_permissions(&path, read_only).unwrap();
    let mut updated = session.clone();
    updated.documents[0].text.push_str(" latest");
    assert!(store.checkpoint(&updated).is_err());
    fs::set_permissions(&path, permissions).unwrap();
    assert_eq!(
        load_session(&path).unwrap().documents[0].text,
        "unsaved 日本 🚀"
    );
    drop(store);
    let batch = RecoveryStore::claim(root.path()).unwrap();
    assert_eq!(
        batch.sessions[0].documents[0].saved_encoding,
        Some(Encoding::Utf8)
    );
    let next = RecoveryStore::create(root.path()).unwrap();
    next.checkpoint(&updated).unwrap();
    batch.acknowledge().unwrap();
    assert!(!path.exists());
    drop(next);
    assert_eq!(
        RecoveryStore::pending(root.path()).unwrap()[0].documents[0].text,
        "unsaved 日本 🚀 latest"
    );
}

#[test]
fn automatic_policy_prevents_legacy_resurrection_even_with_empty_workspace() {
    let root = tempfile::tempdir().unwrap();
    let store = RecoveryStore::create(root.path()).unwrap();
    store
        .checkpoint(&Session {
            schema: 1,
            documents: vec![],
            preferences: Preferences::default(),
            layout: None,
        })
        .unwrap();
    store.enable_automatic_resume().unwrap();
    drop(store);
    let batch = RecoveryStore::claim(root.path()).unwrap();
    assert_eq!(batch.sessions.len(), 1);
    assert!(batch.sessions[0].documents.is_empty());
    batch.acknowledge().unwrap();
    assert!(!RecoveryStore::has_snapshots(root.path()).unwrap());
    assert!(RecoveryStore::automatic_resume_enabled(root.path()).unwrap());
    fs::write(root.path().join("automatic-session-v1"), b"invalid").unwrap();
    assert!(RecoveryStore::automatic_resume_enabled(root.path()).is_err());
}

#[test]
fn legacy_view_checksum_and_adjusted_utf8_positions_remain_valid() {
    let root = tempfile::tempdir().unwrap();
    let path = root.path().join("legacy.json");
    let session = Session {
        schema: 1,
        documents: vec![RecoveryDocument {
            path: None,
            name: "legacy".into(),
            text: "é\r\nx\ry\n".into(),
            encoding: Encoding::Utf8,
            saved_encoding: None,
            expected: None,
            dirty: true,
            view: Some(ViewState::default()),
            tab: None,
        }],
        preferences: Preferences::default(),
        layout: None,
    };
    let legacy_payload = serde_json::to_vec(&session).unwrap();
    assert!(!String::from_utf8_lossy(&legacy_payload).contains("saved_encoding"));
    assert!(!String::from_utf8_lossy(&legacy_payload).contains("read_only"));
    fs::write(
        &path,
        serde_json::to_vec(&serde_json::json!({
            "session": session, "checksum": stamp(&legacy_payload),
        }))
        .unwrap(),
    )
    .unwrap();
    assert_eq!(
        load_session(&path).unwrap().documents[0].text,
        "é\r\nx\ry\n"
    );
    let mut view = ViewState {
        caret: 1,
        anchor: 900,
        clone_caret: 1,
        clone_anchor: 900,
        first_visible: 900,
        clone_first_visible: 900,
        bookmarks: Some(vec![0, 1, 2, 3, 4, 900]),
        ..Default::default()
    };
    assert!(view.fit_to_text("é\r\nx\ry\n"));
    view.validate("é\r\nx\ry\n").unwrap();
    assert_eq!(
        (view.caret, view.anchor, view.clone_caret, view.clone_anchor),
        (0, 8, 0, 8)
    );
    assert_eq!((view.first_visible, view.clone_first_visible), (3, 3));
    assert_eq!(view.bookmarks, Some(vec![0, 1, 2, 3]));
    assert!(!view.fit_to_text("é\r\nx\ry\n"));
}
