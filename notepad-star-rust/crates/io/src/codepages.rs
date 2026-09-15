use crate::{Result, MAX_FILE_BYTES};
use serde::{Deserialize, Serialize};
use std::{
    collections::HashMap,
    sync::{LazyLock, OnceLock},
};

#[path = "codepages/catalog.rs"]
mod catalog;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(try_from = "u16", into = "u16")]
pub struct CodePage(u16);

impl TryFrom<u16> for CodePage {
    type Error = String;
    fn try_from(id: u16) -> Result<Self> {
        if id == 28604 || catalog::PAGES.iter().any(|page| page.0 == id) {
            Ok(Self(id))
        } else {
            Err(format!("Unsupported code page: {id}"))
        }
    }
}
impl From<CodePage> for u16 {
    fn from(page: CodePage) -> Self {
        page.0
    }
}

type ReverseTable = HashMap<char, u32>;
static ENCODERS: LazyLock<Vec<OnceLock<ReverseTable>>> =
    LazyLock::new(|| catalog::PAGES.iter().map(|_| OnceLock::new()).collect());

fn scalar(offset: usize, index: usize) -> Option<char> {
    let start = (offset + index) * 4;
    char::from_u32(u32::from_le_bytes(
        catalog::TABLES[start..start + 4].try_into().unwrap(),
    ))
}

impl CodePage {
    pub fn all() -> impl Iterator<Item = Self> {
        catalog::PAGES
            .iter()
            .map(|page| Self(page.0))
            .chain(std::iter::once(Self(28604)))
    }
    fn index(self) -> usize {
        catalog::PAGES
            .iter()
            .position(|page| page.0 == self.0)
            .expect("CodePage is validated at construction")
    }
    pub fn label(self) -> &'static str {
        if self.0 == 28604 {
            "ISO 8859-14"
        } else {
            catalog::PAGES[self.index()].1
        }
    }
    pub(crate) fn decode(self, bytes: &[u8]) -> Result<String> {
        if self.0 == 28604 {
            let text = encoding_rs::ISO_8859_14
                .decode_without_bom_handling_and_without_replacement(bytes)
                .ok_or("Invalid ISO 8859-14 bytes.")?;
            return Ok(text.into_owned());
        }
        let (_, _, offset, count) = catalog::PAGES[self.index()];
        let mut text = String::with_capacity(bytes.len().min(MAX_FILE_BYTES));
        let mut cursor = 0;
        while cursor < bytes.len() {
            let start = cursor;
            let byte = bytes[cursor];
            cursor += 1;
            let decoded = scalar(offset, usize::from(byte)).or_else(|| {
                if count == 256 || cursor == bytes.len() {
                    return None;
                }
                let pair = u16::from_be_bytes([byte, bytes[cursor]]);
                cursor += 1;
                scalar(offset, 256 + usize::from(pair))
            });
            let character = decoded.ok_or_else(|| {
                format!("{} has invalid or non-reversible bytes at offset {start}. Use the byte preview; the source was not changed.", self.label())
            })?;
            if text.len() + character.len_utf8() > MAX_FILE_BYTES {
                return Err("Decoded text exceeds the 32 MiB editable-buffer limit.".into());
            }
            text.push(character);
        }
        Ok(text)
    }
    pub(crate) fn encode(self, text: &str) -> Result<Vec<u8>> {
        if self.0 == 28604 {
            let (bytes, _, errors) = encoding_rs::ISO_8859_14.encode(text);
            if errors {
                return Err("Some characters cannot be represented in ISO 8859-14.".into());
            }
            return Ok(bytes.into_owned());
        }
        let index = self.index();
        let (_, _, offset, count) = catalog::PAGES[index];
        let encoder = ENCODERS[index].get_or_init(|| {
            let mut map = ReverseTable::new();
            for key in 0..count {
                if let Some(character) = scalar(offset, key) {
                    assert!(map.insert(character, key as u32).is_none());
                }
            }
            map
        });
        let mut bytes = Vec::with_capacity(text.len());
        for (position, character) in text.char_indices() {
            let key = *encoder.get(&character).ok_or_else(|| {
                format!("Character U+{:04X} at UTF-8 offset {position} cannot be saved exactly as {}. Choose UTF-8; no file was changed.", character as u32, self.label())
            })?;
            if key < 256 {
                bytes.push(key as u8);
            } else {
                bytes.extend_from_slice(&((key - 256) as u16).to_be_bytes());
            }
        }
        Ok(bytes)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_frozen_mapping_is_reversible_and_bounded() {
        let mut end = 0;
        for &(id, _, offset, count) in catalog::PAGES {
            assert_eq!(offset, end);
            assert!(count == 256 || count == 256 + 65536);
            end += count;
            let page = CodePage::try_from(id).unwrap();
            for key in 0..count {
                let Some(character) = scalar(offset, key) else {
                    continue;
                };
                let bytes = if key < 256 {
                    vec![key as u8]
                } else {
                    ((key - 256) as u16).to_be_bytes().to_vec()
                };
                let text = page.decode(&bytes).unwrap();
                assert_eq!(text, character.to_string(), "Code page {id}, key {key}");
                assert_eq!(page.encode(&text).unwrap(), bytes);
            }
        }
        assert_eq!(end * 4, catalog::TABLES.len());
        assert!(CodePage::try_from(0).is_err());
        assert!(serde_json::from_str::<CodePage>("65000").is_err());
    }

    #[test]
    fn iso_labels_do_not_alias_windows_codepages_or_replace_characters() {
        let iso = CodePage::try_from(28591).unwrap();
        let windows = CodePage::try_from(1252).unwrap();
        assert_eq!(iso.decode(&[0x80]).unwrap(), "\u{80}");
        assert_eq!(windows.decode(&[0x80]).unwrap(), "\u{20ac}");
        assert!(iso.encode("\u{20ac}").is_err());
        let celtic = CodePage::try_from(28604).unwrap();
        assert_eq!(celtic.decode(&[0xa1]).unwrap(), "\u{1e02}");
        assert_eq!(celtic.encode("\u{1e02}").unwrap(), [0xa1]);
    }

    #[test]
    fn double_byte_boundaries_and_lossy_aliases_are_refused() {
        let shift = CodePage::try_from(932).unwrap();
        assert!(shift.decode(&[0x82]).is_err());
        assert!(shift.decode(&[0x82, 0x20]).is_err());
        assert!(shift.encode("\u{a5}").is_err());
        assert_eq!(shift.decode(&[0x82, 0xa0]).unwrap(), "\u{3042}");
        assert!(CodePage::try_from(51949)
            .unwrap()
            .encode("\u{ac02}")
            .is_err());
        let korean = CodePage::try_from(949).unwrap();
        assert_eq!(
            korean.decode(&korean.encode("\u{ac02}").unwrap()).unwrap(),
            "\u{ac02}"
        );
    }
}
