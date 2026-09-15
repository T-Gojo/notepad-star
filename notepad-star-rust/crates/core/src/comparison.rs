use serde::Serialize;
use similar::{capture_diff_slices_deadline, Algorithm, DiffTag};
use std::time::{Duration, Instant};

pub const MAX_BYTES: usize = 2 * 1024 * 1024;
pub const MAX_LINES: usize = 20000;
pub use star_io::settings::ComparisonPreferences as Options;

#[derive(Debug, Serialize)]
pub struct Hunk {
    pub left_start: usize,
    pub left_len: usize,
    pub right_start: usize,
    pub right_len: usize,
    pub kind: &'static str,
}

#[derive(Debug, Serialize)]
pub struct LineMark {
    pub side: &'static str,
    pub line: usize,
    pub kind: &'static str,
}
#[derive(Debug, Serialize)]
pub struct Span {
    pub side: &'static str,
    pub start: usize,
    pub end: usize,
}
#[derive(Debug, Serialize)]
pub struct Gap {
    pub side: &'static str,
    pub before_line: usize,
    pub rows: usize,
}
#[derive(Debug, Serialize)]
pub struct Comparison {
    pub hunks: Vec<Hunk>,
    pub added: usize,
    pub removed: usize,
    pub marks: Vec<LineMark>,
    pub spans: Vec<Span>,
    pub gaps: Vec<Gap>,
    pub detail_limited: bool,
    pub ignored_differences: bool,
}

struct Line<'a> {
    body: &'a str,
    start: usize,
    number: usize,
    key: String,
}
fn lines<'a>(text: &'a str, options: &Options) -> Result<(Vec<Line<'a>>, usize), String> {
    if text.len() > MAX_BYTES {
        return Err("Comparison supports at most 2 MiB per document.".into());
    }
    let source = crate::advanced::split_lines(text, MAX_LINES).map_err(|e| e.to_string())?;
    let count = source.len();
    let mut result = Vec::new();
    let mut start = 0;
    for (number, line) in source.into_iter().enumerate() {
        let mut key = if options.ignore_whitespace {
            line.body
                .chars()
                .filter(|ch| !ch.is_whitespace())
                .collect::<String>()
        } else {
            line.body.into()
        };
        if options.ignore_case {
            key = key.to_lowercase();
        }
        if !options.ignore_eol {
            key.push_str(line.ending);
        }
        if !options.ignore_blank_lines || !line.body.trim().is_empty() {
            result.push(Line {
                body: line.body,
                start,
                number,
                key,
            });
        }
        start += line.body.len() + line.ending.len();
    }
    Ok((result, count))
}

pub fn compare(left: &str, right: &str, ignore_whitespace: bool) -> Result<Comparison, String> {
    compare_with_options(
        left,
        right,
        &Options {
            ignore_whitespace,
            ignore_eol: ignore_whitespace,
            ..Options::default()
        },
    )
}

fn original_range(
    lines: &[Line<'_>],
    count: usize,
    range: std::ops::Range<usize>,
) -> (usize, usize) {
    let start = lines
        .get(range.start)
        .map(|line| line.number)
        .unwrap_or(count);
    let end = if range.is_empty() {
        start
    } else {
        lines[range.end - 1].number + 1
    };
    (start, end - start)
}

struct Token {
    key: String,
    start: usize,
    end: usize,
}
fn tokens(line: &Line<'_>, options: &Options) -> Vec<Token> {
    let mut ranges: Vec<(usize, usize, u8)> = Vec::new();
    for (start, ch) in line.body.char_indices() {
        let kind = if ch.is_alphanumeric() || ch == '_' {
            1
        } else if ch.is_whitespace() {
            2
        } else {
            0
        };
        if options.ignore_whitespace && kind == 2 {
            continue;
        }
        if options.detail == "words" && kind != 0 {
            if let Some(previous) = ranges.last_mut() {
                if previous.2 == kind && previous.1 == start {
                    previous.1 = start + ch.len_utf8();
                    continue;
                }
            }
        }
        ranges.push((start, start + ch.len_utf8(), kind));
    }
    ranges
        .into_iter()
        .map(|(start, end, _)| Token {
            key: if options.ignore_case {
                line.body[start..end].to_lowercase()
            } else {
                line.body[start..end].into()
            },
            start: line.start + start,
            end: line.start + end,
        })
        .collect()
}

fn detail(
    left: &Line<'_>,
    right: &Line<'_>,
    options: &Options,
    deadline: Instant,
    budget: &mut usize,
    result: &mut Comparison,
) {
    if options.detail == "lines" {
        return;
    }
    if result.spans.len() >= 20000 {
        result.detail_limited = true;
        return;
    }
    if left.body.len() > 16384
        || right.body.len() > 16384
        || *budget > 100000
        || Instant::now() >= deadline
    {
        result.detail_limited = true;
        for (side, line) in [("left", left), ("right", right)] {
            if result.spans.len() >= 20000 {
                break;
            }
            if !line.body.is_empty() {
                result.spans.push(Span {
                    side,
                    start: line.start,
                    end: line.start + line.body.len(),
                });
            }
        }
        return;
    }
    let old = tokens(left, options);
    let new = tokens(right, options);
    *budget += old.len() + new.len();
    let old_keys: Vec<_> = old.iter().map(|token| token.key.as_str()).collect();
    let new_keys: Vec<_> = new.iter().map(|token| token.key.as_str()).collect();
    for operation in
        capture_diff_slices_deadline(Algorithm::Myers, &old_keys, &new_keys, Some(deadline))
    {
        if operation.tag() == DiffTag::Equal {
            continue;
        }
        for (side, tokens, range) in [
            ("left", &old, operation.old_range()),
            ("right", &new, operation.new_range()),
        ] {
            if !range.is_empty() {
                if result.spans.len() >= 20000 {
                    result.detail_limited = true;
                    return;
                }
                result.spans.push(Span {
                    side,
                    start: tokens[range.start].start,
                    end: tokens[range.end - 1].end,
                });
            }
        }
    }
}

pub fn compare_with_options(
    left_text: &str,
    right_text: &str,
    options: &Options,
) -> Result<Comparison, String> {
    if !matches!(options.detail.as_str(), "lines" | "words" | "characters") {
        return Err("Comparison detail must be lines, words or characters.".into());
    }
    let (left, left_count) = lines(left_text, options)?;
    let (right, right_count) = lines(right_text, options)?;
    let left_keys: Vec<_> = left.iter().map(|line| line.key.as_str()).collect();
    let right_keys: Vec<_> = right.iter().map(|line| line.key.as_str()).collect();
    let deadline = Instant::now() + Duration::from_millis(200);
    let operations =
        capture_diff_slices_deadline(Algorithm::Patience, &left_keys, &right_keys, Some(deadline));
    let mut result = Comparison {
        hunks: Vec::new(),
        added: 0,
        removed: 0,
        marks: Vec::new(),
        spans: Vec::new(),
        gaps: Vec::new(),
        detail_limited: false,
        ignored_differences: false,
    };
    let mut matches = Vec::new();
    let mut detail_budget = 0;
    for operation in operations {
        if operation.tag() == DiffTag::Equal {
            for (old, new) in operation.old_range().zip(operation.new_range()) {
                matches.push((left[old].number, right[new].number));
            }
            continue;
        }
        let old = operation.old_range();
        let new = operation.new_range();
        result.added += new.len();
        result.removed += old.len();
        let kind = match operation.tag() {
            DiffTag::Delete => "removed",
            DiffTag::Insert => "added",
            _ => "changed",
        };
        let (left_start, left_len) = original_range(&left, left_count, old.clone());
        let (right_start, right_len) = original_range(&right, right_count, new.clone());
        result.hunks.push(Hunk {
            left_start,
            left_len,
            right_start,
            right_len,
            kind,
        });
        for index in old.clone() {
            result.marks.push(LineMark {
                side: "left",
                line: left[index].number,
                kind,
            });
        }
        for index in new.clone() {
            result.marks.push(LineMark {
                side: "right",
                line: right[index].number,
                kind,
            });
        }
        for (old, new) in old.zip(new) {
            detail(
                &left[old],
                &right[new],
                options,
                deadline,
                &mut detail_budget,
                &mut result,
            );
        }
    }
    result.ignored_differences = result.hunks.is_empty() && left_text != right_text;
    if !matches.is_empty() || !result.hunks.is_empty() {
        // Scintilla retains one editable row even in an empty document.
        matches.push((left_count.max(1), right_count.max(1)));
    }
    let (mut left_padding, mut right_padding) = (0, 0);
    for (old, new) in matches {
        let old_row = old + left_padding;
        let new_row = new + right_padding;
        if old_row < new_row {
            result.gaps.push(Gap {
                side: "left",
                before_line: old,
                rows: new_row - old_row,
            });
            left_padding += new_row - old_row;
        } else if new_row < old_row {
            result.gaps.push(Gap {
                side: "right",
                before_line: new,
                rows: old_row - new_row,
            });
            right_padding += old_row - new_row;
        }
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn alignment_handles_leading_insertions_and_middle_gaps_without_text_edits() {
        let result = compare("same\nend\n", "added\nsame\nmiddle\nend\n", false).unwrap();
        assert!(result
            .gaps
            .iter()
            .any(|gap| gap.side == "left" && gap.before_line == 0 && gap.rows == 1));
        assert!(result
            .gaps
            .iter()
            .any(|gap| gap.side == "left" && gap.before_line == 1 && gap.rows == 1));
        let empty = compare("", "a\nb\nc\nd", false).unwrap();
        assert!(empty
            .gaps
            .iter()
            .any(|gap| gap.side == "left" && gap.before_line == 1 && gap.rows == 3));
        assert!(compare("", "text", false).unwrap().gaps.is_empty());
    }
    #[test]
    fn unicode_character_and_word_details_use_original_utf8_ranges() {
        let left = "say 日本 one\n";
        let right = "say 日本 two\n";
        for mode in ["characters", "words"] {
            let result = compare_with_options(
                left,
                right,
                &Options {
                    detail: mode.into(),
                    ..Options::default()
                },
            )
            .unwrap();
            assert!(!result.spans.is_empty());
            for span in &result.spans {
                let source = if span.side == "left" { left } else { right };
                assert!(source.get(span.start..span.end).is_some());
            }
            if mode == "words" {
                assert!(result
                    .spans
                    .iter()
                    .any(|span| span.side == "left" && &left[span.start..span.end] == "one"));
            }
        }
    }
    #[test]
    fn ignored_lines_keep_original_coordinates_and_report_ignored_differences() {
        let options = Options {
            ignore_blank_lines: true,
            ignore_case: true,
            ignore_whitespace: true,
            ignore_eol: true,
            ..Options::default()
        };
        let result = compare_with_options("\n a B\r\n\n", "AB\n", &options).unwrap();
        assert!(result.hunks.is_empty() && result.ignored_differences);
        let result = compare_with_options("\n same\n old\n", "same\nnew\n", &options).unwrap();
        assert_eq!(result.hunks[0].left_start, 2);
        assert!(result
            .marks
            .iter()
            .all(|mark| mark.side != "left" || mark.line == 2));
    }

    #[test]
    fn detail_budget_keeps_complete_line_differences_with_bounded_highlights() {
        let left = ("ab".repeat(30) + "\n").repeat(4000);
        let right = ("ac".repeat(30) + "\n").repeat(4000);
        let result = compare(&left, &right, false).unwrap();
        assert_eq!((result.removed, result.added), (4000, 4000));
        assert!(result.spans.len() <= 20000);
        assert!(result.detail_limited);
        for span in &result.spans {
            let source = if span.side == "left" { &left } else { &right };
            assert!(source.get(span.start..span.end).is_some());
        }
    }

    #[test]
    fn inserted_removed_and_replaced_lines_have_exact_source_ranges() {
        let diff = compare("same\nold\nend\n", "same\nnew\nextra\nend\n", false).unwrap();
        assert_eq!(diff.hunks.len(), 1);
        let hunk = &diff.hunks[0];
        assert_eq!(
            (
                hunk.left_start,
                hunk.left_len,
                hunk.right_start,
                hunk.right_len
            ),
            (1, 1, 1, 2)
        );
        assert_eq!((diff.added, diff.removed), (2, 1));
        assert_eq!(compare("", "日本🚀", false).unwrap().hunks[0].kind, "added");
        assert_eq!(compare("x", "", false).unwrap().hunks[0].kind, "removed");
        assert!(compare("same", "same", false).unwrap().hunks.is_empty());
    }

    #[test]
    fn mixed_endings_trailing_newline_and_whitespace_are_deliberate() {
        let diff = compare("one\rtwo\r\nthree\n", "one\rtwo\r\nchanged\n", false).unwrap();
        assert_eq!(diff.hunks[0].left_start, 2);
        assert!(!compare("one", "one\n", false).unwrap().hunks.is_empty());
        assert!(!compare("x\r\n", "x\n", false).unwrap().hunks.is_empty());
        assert!(compare(" a\tb\r\n", "ab\n", true).unwrap().hunks.is_empty());
    }

    #[test]
    fn limits_and_very_different_inputs_remain_bounded() {
        assert!(compare(&"x".repeat(MAX_BYTES + 1), "", false).is_err());
        assert!(compare(&"\n".repeat(MAX_LINES + 1), "", false).is_err());
        let left = (0..MAX_LINES)
            .map(|n| format!("left{n}\n"))
            .collect::<String>();
        let right = (0..MAX_LINES)
            .map(|n| format!("right{n}\n"))
            .collect::<String>();
        let result = compare(&left, &right, false).unwrap();
        assert_eq!((result.removed, result.added), (MAX_LINES, MAX_LINES));
    }
}
