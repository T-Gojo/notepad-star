use crate::Result;
use std::{
    fs::File,
    io::{Read, Seek, SeekFrom},
    path::Path,
};

pub const PAGE_BYTES: usize = 64 * 1024;

pub struct Page {
    pub text: String,
    pub offset: u64,
    pub next: u64,
    pub size: u64,
    pub hex: bool,
}

pub fn read_page(path: &Path, offset: u64) -> Result<Page> {
    let mut file = File::open(path).map_err(|e| format!("Cannot open large-file preview: {e}"))?;
    let metadata = file.metadata().map_err(|e| e.to_string())?;
    if !metadata.is_file() {
        return Err("Large-file preview requires a regular file.".into());
    }
    let size = metadata.len();
    if offset > size {
        return Err("Preview offset is beyond the end of the file.".into());
    }
    file.seek(SeekFrom::Start(offset))
        .map_err(|e| e.to_string())?;
    let mut bytes = Vec::with_capacity(PAGE_BYTES);
    (&mut file)
        .take(PAGE_BYTES as u64)
        .read_to_end(&mut bytes)
        .map_err(|e| e.to_string())?;
    if bytes.is_empty() && offset < size {
        return Err("File changed while reading. Reload the preview.".into());
    }
    let mut start = 0usize;
    if offset == 0 && bytes.starts_with(b"\xef\xbb\xbf") {
        start = 3;
    } else if offset > 0 {
        while start < bytes.len().min(3) && bytes[start] & 0xc0 == 0x80 {
            start += 1;
        }
    }
    let mut end = bytes.len();
    if !bytes.contains(&0) {
        loop {
            match std::str::from_utf8(&bytes[start..end]) {
                Ok(text) if end > 0 || bytes.is_empty() => {
                    return Ok(Page {
                        text: text.into(),
                        offset: offset + start as u64,
                        next: offset + end as u64,
                        size,
                        hex: false,
                    })
                }
                Err(error)
                    if error.error_len().is_none()
                        && end > start
                        && bytes.len() - end < 3
                        && offset + (end as u64) < size =>
                {
                    end -= 1
                }
                _ => break,
            }
        }
    }
    let mut text = String::new();
    use std::fmt::Write;
    for (line, chunk) in bytes.chunks(16).enumerate() {
        write!(&mut text, "{:016x}  ", offset + line as u64 * 16).map_err(|e| e.to_string())?;
        for byte in chunk {
            write!(&mut text, "{byte:02x} ").map_err(|e| e.to_string())?;
        }
        text.push('\n');
    }
    Ok(Page {
        text,
        offset,
        next: offset + bytes.len() as u64,
        size,
        hex: true,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    #[test]
    fn one_gib_sparse_file_is_read_in_bounded_pages() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("large.txt");
        let mut file = File::create(&path).unwrap();
        let size = 1024_u64 * 1024 * 1024;
        file.set_len(size).unwrap();
        file.seek(SeekFrom::Start(size - 4)).unwrap();
        file.write_all(b"end\n").unwrap();
        drop(file);
        let first = read_page(&path, 0).unwrap();
        assert_eq!(first.size, size);
        assert_eq!(first.next, PAGE_BYTES as u64);
        assert!(first.hex);
        assert!(first.text.len() < PAGE_BYTES * 5);
        let last = read_page(&path, size - 4).unwrap();
        assert_eq!(last.text, "end\n");
        assert_eq!(last.next, size);
    }
    #[test]
    fn utf8_characters_are_not_lost_at_page_boundaries() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("unicode.txt");
        let text = "a".repeat(PAGE_BYTES - 1) + "日x";
        std::fs::write(&path, text.as_bytes()).unwrap();
        let first = read_page(&path, 0).unwrap();
        let second = read_page(&path, first.next).unwrap();
        assert!(!first.hex && !second.hex);
        assert_eq!(first.text + &second.text, text);
    }
}
