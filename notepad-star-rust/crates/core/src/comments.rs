use crate::advanced::{split_lines, Line};

pub const MAX_INPUT: usize = 4 * 1024 * 1024;
#[derive(Debug, Eq, PartialEq)]
pub struct Edit {
    pub start: usize,
    pub end: usize,
    pub text: String,
}
pub struct Symbols<'a> {
    pub line: &'a str,
    pub open: &'a str,
    pub close: &'a str,
    pub column_zero: bool,
    pub comment_empty: bool,
    pub space: bool,
}
impl Symbols<'_> {
    fn validate(&self, text: &str) -> Result<(), String> {
        if text.len() > MAX_INPUT {
            return Err("Comment operations are limited to 4 MiB of input.".into());
        }
        for symbol in [self.line, self.open, self.close] {
            if symbol.len() > 127 || symbol.contains(['\0', '\r', '\n']) {
                return Err("Unsupported comment delimiter.".into());
            }
        }
        if self.line.is_empty() && (self.open.is_empty() || self.close.is_empty()) {
            return Err("This language has no usable comment delimiters.".into());
        }
        Ok(())
    }
}

pub fn lines(text: &str, symbols: &Symbols<'_>, mode: &str) -> Result<Vec<Edit>, String> {
    symbols.validate(text)?;
    if !matches!(mode, "toggle" | "comment" | "uncomment") {
        return Err("Unknown line-comment operation.".into());
    }
    let paired = symbols.line.is_empty();
    let prefix = if paired { symbols.open } else { symbols.line };
    let space = if symbols.space { " " } else { "" };
    let mut lines = split_lines(text, 10000).map_err(|e| e.to_string())?;
    if text.is_empty() && symbols.comment_empty {
        lines.push(Line {
            body: "",
            ending: "",
        });
    }
    let mut result = Vec::new();
    let mut offset = 0;
    for line in lines {
        let indentation = line.body.len() - line.body.trim_start_matches([' ', '\t']).len();
        if indentation == line.body.len() && !symbols.comment_empty {
            offset += line.body.len() + line.ending.len();
            continue;
        }
        let position = if symbols.column_zero { 0 } else { indentation };
        let body = &line.body[position..];
        let marked = body
            .get(..prefix.len())
            .is_some_and(|part| part.eq_ignore_ascii_case(prefix));
        if mode == "uncomment" || (mode == "toggle" && marked) {
            if marked {
                let head = prefix.len()
                    + usize::from(
                        symbols.space && body.as_bytes().get(prefix.len()) == Some(&b' '),
                    );
                if paired {
                    let trimmed = body.trim_end_matches([' ', '\t']);
                    if !trimmed.ends_with(symbols.close)
                        || trimmed.len() < head + symbols.close.len()
                    {
                        return Err("The selected line has an incomplete comment pair; nothing was changed.".into());
                    }
                    let end = offset + position + trimmed.len();
                    let mut start = end - symbols.close.len();
                    if symbols.space
                        && start > offset + position + head
                        && text.as_bytes()[start - 1] == b' '
                    {
                        start -= 1;
                    }
                    result.push(Edit {
                        start: offset + position,
                        end: offset + position + head,
                        text: String::new(),
                    });
                    result.push(Edit {
                        start,
                        end,
                        text: String::new(),
                    });
                } else {
                    result.push(Edit {
                        start: offset + position,
                        end: offset + position + head,
                        text: String::new(),
                    });
                }
            }
        } else {
            let mut insertion = format!("{prefix}{space}");
            if paired && position == line.body.len() {
                insertion.push_str(&format!("{space}{}", symbols.close));
            }
            result.push(Edit {
                start: offset + position,
                end: offset + position,
                text: insertion,
            });
            if paired && position != line.body.len() {
                result.push(Edit {
                    start: offset + line.body.len(),
                    end: offset + line.body.len(),
                    text: format!("{space}{}", symbols.close),
                });
            }
        }
        offset += line.body.len() + line.ending.len();
    }
    Ok(result)
}

pub fn block(
    text: &str,
    start: usize,
    end: usize,
    symbols: &Symbols<'_>,
    remove: bool,
) -> Result<Vec<Edit>, String> {
    symbols.validate(text)?;
    if symbols.open.is_empty() || symbols.close.is_empty() {
        return Err("This language has no paired comment delimiters; use line commenting.".into());
    }
    if start > end
        || end > text.len()
        || !text.is_char_boundary(start)
        || !text.is_char_boundary(end)
    {
        return Err("Invalid comment selection.".into());
    }
    if !remove {
        if start == end {
            return Ok(vec![Edit {
                start,
                end,
                text: format!("{}  {}", symbols.open, symbols.close),
            }]);
        }
        return Ok(vec![
            Edit {
                start,
                end: start,
                text: format!("{} ", symbols.open),
            },
            Edit {
                start: end,
                end,
                text: format!(" {}", symbols.close),
            },
        ]);
    }
    let left = if text[start..].starts_with(symbols.open) {
        start
    } else {
        text[..start]
            .rfind(symbols.open)
            .ok_or("No enclosing opening comment delimiter.")?
    };
    let right = if text[..end].ends_with(symbols.close) {
        end - symbols.close.len()
    } else {
        end + text[end..]
            .find(symbols.close)
            .ok_or("No enclosing closing comment delimiter.")?
    };
    let mut head_end = left + symbols.open.len();
    if head_end > right
        || text[head_end..right].contains(symbols.open)
        || text[head_end..right].contains(symbols.close)
    {
        return Err(
            "Nested or ambiguous comment delimiters require an explicit inner selection.".into(),
        );
    }
    if head_end < right && text.as_bytes()[head_end] == b' ' {
        head_end += 1;
    }
    let mut tail_start = right;
    if tail_start > head_end && text.as_bytes()[tail_start - 1] == b' ' {
        tail_start -= 1;
    }
    Ok(vec![
        Edit {
            start: left,
            end: head_end,
            text: String::new(),
        },
        Edit {
            start: tail_start,
            end: right + symbols.close.len(),
            text: String::new(),
        },
    ])
}

#[cfg(test)]
mod tests {
    use super::*;
    fn symbols() -> Symbols<'static> {
        Symbols {
            line: "//",
            open: "/*",
            close: "*/",
            column_zero: false,
            comment_empty: false,
            space: true,
        }
    }
    fn apply(text: &str, edits: Vec<Edit>) -> String {
        let mut result = text.to_owned();
        for edit in edits.into_iter().rev() {
            result.replace_range(edit.start..edit.end, &edit.text);
        }
        result
    }
    #[test]
    fn mixed_lines_toggle_individually_without_touching_eols_or_blank_lines() {
        let input = "  one\r\n\t// two\n \rthree";
        let toggled = apply(input, lines(input, &symbols(), "toggle").unwrap());
        assert_eq!(toggled, "  // one\r\n\ttwo\n \r// three");
        assert_eq!(
            apply(&toggled, lines(&toggled, &symbols(), "toggle").unwrap()),
            input
        );
    }
    #[test]
    fn explicit_comment_stacks_and_ascii_case_insensitive_uncomment_preserves_text() {
        let mut spec = symbols();
        spec.line = "REM";
        assert_eq!(
            apply(" rem x", lines(" rem x", &spec, "uncomment").unwrap()),
            " x"
        );
        assert_eq!(
            apply("// x", lines("// x", &symbols(), "comment").unwrap()),
            "// // x"
        );
        assert!(lines("plain", &symbols(), "uncomment").unwrap().is_empty());
    }
    #[test]
    fn paired_line_comments_and_column_zero_language_rules_are_supported() {
        let mut spec = symbols();
        spec.line = "";
        let marked = apply("  x\n", lines("  x\n", &spec, "toggle").unwrap());
        assert_eq!(marked, "  /* x */\n");
        assert_eq!(
            apply(&marked, lines(&marked, &spec, "toggle").unwrap()),
            "  x\n"
        );
        spec.line = "|";
        spec.column_zero = true;
        spec.comment_empty = true;
        spec.space = false;
        assert_eq!(
            apply("  x\n\n", lines("  x\n\n", &spec, "comment").unwrap()),
            "|  x\n|\n"
        );
    }
    #[test]
    fn block_operations_preserve_unicode_and_refuse_unrelated_or_nested_pairs() {
        let text = "a \u{65e5}\nb";
        let wrapped = apply(text, block(text, 2, 6, &symbols(), false).unwrap());
        assert_eq!(wrapped, "a /* \u{65e5}\n */b");
        assert_eq!(
            apply(&wrapped, block(&wrapped, 5, 9, &symbols(), true).unwrap()),
            text
        );
        assert!(block("/* a */ x /* b */", 8, 9, &symbols(), true).is_err());
        assert!(block("/* a /* b */ c */", 0, 17, &symbols(), true).is_err());
        assert!(block("\u{65e5}", 1, 2, &symbols(), false).is_err());
    }
}
