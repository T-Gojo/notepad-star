use serde::Serialize;

#[derive(Debug, Serialize, PartialEq)]
pub struct Statistics {
    pub utf8_bytes: usize,
    pub unicode_scalars: usize,
    pub utf16_units: usize,
    pub whitespace_delimited_words: usize,
    pub lines: usize,
    pub longest_line_scalars: usize,
    pub crlf: usize,
    pub lf: usize,
    pub cr: usize,
    pub tabs: usize,
}

pub fn summarize(text: &str) -> Result<Statistics, String> {
    if text.len() > star_io::MAX_FILE_BYTES {
        return Err("Summary exceeds the editable-buffer limit.".into());
    }
    let mut result = Statistics {
        utf8_bytes: text.len(),
        unicode_scalars: 0,
        utf16_units: 0,
        whitespace_delimited_words: 0,
        lines: 1,
        longest_line_scalars: 0,
        crlf: 0,
        lf: 0,
        cr: 0,
        tabs: 0,
    };
    let mut previous_cr = false;
    let mut word = false;
    let mut line_length = 0;
    for character in text.chars() {
        result.unicode_scalars += 1;
        result.utf16_units += character.len_utf16();
        if !character.is_whitespace() && !word {
            result.whitespace_delimited_words += 1;
        }
        word = !character.is_whitespace();
        match character {
            '\r' => {
                result.cr += 1;
                result.lines += 1;
                line_length = 0;
            }
            '\n' if previous_cr => {
                result.cr -= 1;
                result.crlf += 1;
            }
            '\n' => {
                result.lf += 1;
                result.lines += 1;
                line_length = 0;
            }
            _ => {
                if character == '\t' {
                    result.tabs += 1;
                }
                line_length += 1;
                result.longest_line_scalars = result.longest_line_scalars.max(line_length);
            }
        }
        previous_cr = character == '\r';
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn unicode_and_mixed_endings_are_counted_without_line_allocations() {
        assert_eq!(
            summarize("\u{e9} \u{1f680}\r\na\rb\n").unwrap(),
            Statistics {
                utf8_bytes: 13,
                unicode_scalars: 9,
                utf16_units: 10,
                whitespace_delimited_words: 4,
                lines: 4,
                longest_line_scalars: 3,
                crlf: 1,
                lf: 1,
                cr: 1,
                tabs: 0,
            }
        );
        let blank = summarize("").unwrap();
        assert_eq!(
            (
                blank.lines,
                blank.longest_line_scalars,
                blank.whitespace_delimited_words
            ),
            (1, 0, 0)
        );
        let tabs = summarize("\t\t\r\r\n").unwrap();
        assert_eq!((tabs.tabs, tabs.lines, tabs.cr, tabs.crlf), (2, 3, 1, 1));
        assert!(summarize(&"a".repeat(star_io::MAX_FILE_BYTES + 1)).is_err());
    }
}
