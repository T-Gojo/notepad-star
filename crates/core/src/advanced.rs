// SPDX-License-Identifier: GPL-3.0-or-later
//! Bounded, deterministic text operations and column-number proposals.
//!
//! This module is std-only and can be tested directly with `rustc --test`.
//! It never mutates a document/file, performs I/O, uses randomness, or runs tools.
//! The caller owns selection expansion, revision checking, and undo grouping.
//!
//! # Text semantics
//!
//! A physical line is content plus its CRLF/LF/CR marker, if any. A terminal
//! marker does not create an extra phantom line; empty input has zero lines.
//! Case, trim and tab operations preserve every marker byte. Reversal/sorting
//! reorder contents but leave the original sequence of marker slots in place.
//! Moving an empty content to the final unterminated slot can necessarily make
//! the resulting text end in an existing line break (`"\na"` -> `"a\n"`).
//! Filtering retains each survivor's separator, except the final survivor uses
//! the original terminal marker under [`FinalNewline::Preserve`]. With no
//! survivors the result is empty. Duplicate comparison is exact content-only.
//!
//! Whitespace for trimming, blank filtering and numeric margins is ASCII space
//! and tab, matching Notepad++'s commands, not all Unicode whitespace.
//!
//! # Case semantics
//!
//! Proper/sentence force and blend modes follow the state rules in Notepad++'s
//! `ScintillaEditView::changeCase`, including internal straight/smart apostrophes,
//! punctuation followed immediately by an alphanumeric, paragraph breaks, and
//! its narrowly defined English `I` exception. Force lowers subsequent letters;
//! blend leaves their case unchanged. Unlike the Windows implementation, Rust
//! Unicode scalar mappings can expand safely (`İ` -> `i\u{307}`).
//! These are scalar-based rules, **not** Unicode titlecase/UAX #29 word boundaries,
//! grapheme segmentation, normalization, context-sensitive or locale-aware case
//! conversion. Combining marks can be word boundaries under the proper-case
//! alphanumeric rule, just as non-alphanumerics are in the original algorithm.
//!
//! # Numeric semantics
//!
//! Numeric sort is explicitly **strict whole-line sorting**, not Notepad++'s
//! natural integer-chunk sorter or permissive `double`-based decimal sorter.
//! After trimming ASCII margins, integers accept `[+-]?[0-9]+`. Decimals accept
//! an optional sign and ASCII digits with at most the selected dot/comma;
//! `.5`, `1.`, `-0`, and their comma forms are accepted. At least one digit is
//! required. Exponents, thousands separators, trailing text, NaN and infinity
//! are rejected. Blank handling is explicit and never substitutes zero.
//!
//! Integers fit `i128`. Decimals are exact fixed-point values with an `i128`
//! coefficient after removing fractional trailing zeros, and a nonnegative
//! decimal scale bounded by the input size. Coefficient overflow is an error,
//! not rounding. Comparison never rescales by multiplication and never uses
//! `f64`, so differing scales and values above 2^53 compare exactly.
//!
//! # Upstream references and differences
//!
//! Semantics were checked against the checked-in `reference/notepad-plus-plus/PowerEditor/src`:
//! `ScintillaComponent/ScintillaEditView.cpp` (`changeCase`, `columnReplace`,
//! `sortLines`), `Notepad_plus.cpp` (`doTrim`, `wsTabConvert`, `removeEmptyLine`,
//! `removeDuplicateLines`), and `MISC/Common/Sorters.h`.
//! Mixed EOL support, strict numeric grammar, checked signed column arithmetic,
//! and explicit final-newline policy are deliberate cross-platform contracts.
//! Choose `FinalNewline::Remove` to remove the terminal empty row as Notepad++'s
//! remove-empty command does. Reordering never treats a terminal marker as a
//! phantom sortable row or normalizes it to the editor's configured EOL.
//! Locale/natural sorting, column-substring sorting, random case/reordering,
//! grapheme/display-cell tab widths, and partial-selection context inference are
//! intentionally out of scope.

use std::cmp::Ordering;
use std::fmt;

pub const MAX_INPUT_BYTES: usize = 4 * 1024 * 1024;
pub const MAX_OUTPUT_BYTES: usize = 16 * 1024 * 1024;
pub const MAX_LINES: usize = 100_000;
pub const MAX_GENERATED_ROWS: usize = 100_000;
pub const MAX_ROW_BYTES: usize = 4096;
pub const MAX_TAB_WIDTH: usize = 256;

pub type Result<T> = std::result::Result<T, Error>;

/// Defaults are hard ceilings. Callers can lower any limit, including to zero.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Limits {
    pub max_input_bytes: usize,
    pub max_output_bytes: usize,
    pub max_lines: usize,
    pub max_generated_rows: usize,
    pub max_row_bytes: usize,
}

impl Default for Limits {
    fn default() -> Self {
        Self {
            max_input_bytes: MAX_INPUT_BYTES,
            max_output_bytes: MAX_OUTPUT_BYTES,
            max_lines: MAX_LINES,
            max_generated_rows: MAX_GENERATED_ROWS,
            max_row_bytes: MAX_ROW_BYTES,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Error {
    InvalidLimit { name: &'static str },
    InvalidOption { name: &'static str },
    InputTooLarge { limit: usize },
    OutputTooLarge { limit: usize },
    TooManyLines { limit: usize },
    TooManyRows { limit: usize },
    RowTooLarge { row: usize, limit: usize },
    InvalidNumber { line: usize },
    NumericOverflow { line: usize },
    SequenceOverflow { row: usize },
    ColumnOverflow { line: usize },
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidLimit { name } => write!(f, "Limit {name} exceeds its hard ceiling."),
            Self::InvalidOption { name } => write!(f, "Invalid option: {name}."),
            Self::InputTooLarge { limit } => write!(f, "Input exceeds {limit} UTF-8 bytes."),
            Self::OutputTooLarge { limit } => write!(f, "Output exceeds {limit} UTF-8 bytes."),
            Self::TooManyLines { limit } => write!(f, "Input exceeds {limit} physical lines."),
            Self::TooManyRows { limit } => write!(f, "Column output exceeds {limit} rows."),
            Self::RowTooLarge { row, limit } => {
                write!(f, "Column row {row} exceeds {limit} bytes.")
            }
            Self::InvalidNumber { line } => {
                write!(f, "Line {line} is not a valid whole-line number.")
            }
            Self::NumericOverflow { line } => {
                write!(
                    f,
                    "Line {line} exceeds the signed 128-bit numeric coefficient range."
                )
            }
            Self::SequenceOverflow { row } => {
                write!(f, "Column sequence overflows at row {row}.")
            }
            Self::ColumnOverflow { line } => {
                write!(f, "Tab column arithmetic overflows on line {line}.")
            }
        }
    }
}

impl std::error::Error for Error {}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CaseMode {
    ProperForce,
    ProperBlend,
    SentenceForce,
    SentenceBlend,
    Invert,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FinalNewline {
    Preserve,
    Remove,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum SortOrder {
    #[default]
    Ascending,
    Descending,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum NumericKind {
    #[default]
    Integer,
    DecimalDot,
    DecimalComma,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum BlankNumbers {
    #[default]
    Reject,
    /// Independent of ascending/descending numeric order.
    First,
    /// Independent of ascending/descending numeric order.
    Last,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct NumericSortOptions {
    pub kind: NumericKind,
    pub order: SortOrder,
    pub blanks: BlankNumbers,
}

/// Width is 1..=256. Columns count Unicode scalars, not UTF-8 bytes or glyph cells.
/// `first_column` applies only to the first supplied line; later lines start at 0.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TabOptions {
    pub width: usize,
    pub first_column: usize,
}

impl Default for TabOptions {
    fn default() -> Self {
        Self {
            width: 4,
            first_column: 0,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Operation {
    Case(CaseMode),
    ReverseLines,
    RemoveConsecutiveDuplicates,
    /// `whitespace=true` also removes lines consisting of ASCII spaces/tabs.
    RemoveEmptyLines {
        whitespace: bool,
        final_newline: FinalNewline,
    },
    TrimLeading,
    TrimBoth,
    TabsToSpaces(TabOptions),
    /// Canonicalize leading ASCII space/tab indentation; never touch interiors.
    LeadingSpacesToTabs(TabOptions),
    SortNumeric(NumericSortOptions),
}

/// Return the complete replacement, or an error. No partial output is returned.
pub fn transform(operation: Operation, text: &str, limits: Limits) -> Result<String> {
    validate_limits(limits)?;
    if text.len() > limits.max_input_bytes {
        return Err(Error::InputTooLarge {
            limit: limits.max_input_bytes,
        });
    }
    let records = split_lines(text, limits.max_lines)?;
    let mut output = Output::new(limits.max_output_bytes);
    match operation {
        Operation::Case(mode) => change_case(text, mode, &mut output)?,
        Operation::ReverseLines => {
            for (line, slot) in records.iter().rev().zip(&records) {
                output.text(line.body)?;
                output.text(slot.ending)?;
            }
        }
        Operation::RemoveConsecutiveDuplicates => {
            let mut selected = Vec::new();
            let mut previous = None;
            for line in &records {
                if previous != Some(line.body) {
                    selected.push(*line);
                    previous = Some(line.body);
                }
            }
            render_filtered(&records, &selected, FinalNewline::Preserve, &mut output)?;
        }
        Operation::RemoveEmptyLines {
            whitespace,
            final_newline,
        } => {
            let selected: Vec<_> = records
                .iter()
                .filter(|line| {
                    if whitespace {
                        !line.body.trim_matches([' ', '\t']).is_empty()
                    } else {
                        !line.body.is_empty()
                    }
                })
                .copied()
                .collect();
            render_filtered(&records, &selected, final_newline, &mut output)?;
        }
        Operation::TrimLeading | Operation::TrimBoth => {
            for line in &records {
                let value = if operation == Operation::TrimBoth {
                    line.body.trim_matches([' ', '\t'])
                } else {
                    line.body.trim_start_matches([' ', '\t'])
                };
                output.text(value)?;
                output.text(line.ending)?;
            }
        }
        Operation::TabsToSpaces(options) => tabs(&records, options, false, &mut output)?,
        Operation::LeadingSpacesToTabs(options) => tabs(&records, options, true, &mut output)?,
        Operation::SortNumeric(options) => sort_numbers(&records, options, &mut output)?,
    }
    Ok(output.value)
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum NumberBase {
    #[default]
    Decimal,
    HexLower,
    HexUpper,
    Octal,
    Binary,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PaddingFill {
    Zero,
    Space,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum Padding {
    #[default]
    None,
    /// A minimum total width INCLUDING the minus sign; never truncates digits.
    Minimum { width: usize, fill: PaddingFill },
    /// Align every row to the widest generated value.
    Auto { fill: PaddingFill },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ColumnNumberOptions {
    pub start: i128,
    pub step: i128,
    pub rows: usize,
    /// Number of rows sharing a value before the next checked increment.
    pub repeat: usize,
    pub base: NumberBase,
    pub padding: Padding,
}

impl Default for ColumnNumberOptions {
    fn default() -> Self {
        Self {
            start: 0,
            step: 1,
            rows: 0,
            repeat: 1,
            base: NumberBase::Decimal,
            padding: Padding::None,
        }
    }
}

/// Generate per-row insertion proposals without line endings or base prefixes.
///
/// Negative numbers use sign/magnitude in every base, never two's complement.
/// Zero padding follows the sign (`-003`); spaces precede it (`  -3`). Unlike
/// Notepad++'s "none" mode, [`Padding::None`] adds no trailing alignment spaces.
/// Every required value and output width is checked before strings are returned.
/// There is no arithmetic increment after the final required value.
pub fn column_numbers(options: ColumnNumberOptions, limits: Limits) -> Result<Vec<String>> {
    validate_limits(limits)?;
    if options.repeat == 0 {
        return Err(Error::InvalidOption { name: "repeat" });
    }
    if let Padding::Minimum { width, .. } = options.padding {
        if width > MAX_ROW_BYTES {
            return Err(Error::InvalidOption {
                name: "padding width",
            });
        }
    }
    if options.rows > limits.max_generated_rows {
        return Err(Error::TooManyRows {
            limit: limits.max_generated_rows,
        });
    }
    if options.rows > limits.max_output_bytes {
        return Err(Error::OutputTooLarge {
            limit: limits.max_output_bytes,
        });
    }
    let mut values = Vec::with_capacity(options.rows);
    let mut value = options.start;
    let mut widest = 0;
    for row in 0..options.rows {
        if row > 0 && row % options.repeat == 0 {
            value = value
                .checked_add(options.step)
                .ok_or(Error::SequenceOverflow { row: row + 1 })?;
        }
        widest = widest.max(number_width(value, options.base));
        values.push(value);
    }
    let (minimum, fill) = match options.padding {
        Padding::None => (0, PaddingFill::Space),
        Padding::Minimum { width, fill } => (width, fill),
        Padding::Auto { fill } => (widest, fill),
    };
    let mut total = 0usize;
    for (row, &value) in values.iter().enumerate() {
        let width = number_width(value, options.base).max(minimum);
        if width > limits.max_row_bytes {
            return Err(Error::RowTooLarge {
                row: row + 1,
                limit: limits.max_row_bytes,
            });
        }
        total = total.checked_add(width).ok_or(Error::OutputTooLarge {
            limit: limits.max_output_bytes,
        })?;
        if total > limits.max_output_bytes {
            return Err(Error::OutputTooLarge {
                limit: limits.max_output_bytes,
            });
        }
    }
    let mut result = Vec::with_capacity(options.rows);
    for value in values {
        let magnitude = value.unsigned_abs();
        let digits = match options.base {
            NumberBase::Decimal => magnitude.to_string(),
            NumberBase::HexLower => format!("{magnitude:x}"),
            NumberBase::HexUpper => format!("{magnitude:X}"),
            NumberBase::Octal => format!("{magnitude:o}"),
            NumberBase::Binary => format!("{magnitude:b}"),
        };
        let raw_width = digits.len() + usize::from(value < 0);
        let pad = minimum.saturating_sub(raw_width);
        let mut row = Output::new(limits.max_row_bytes);
        if fill == PaddingFill::Space {
            row.repeat(' ', pad)?;
        }
        if value < 0 {
            row.character('-')?;
        }
        if fill == PaddingFill::Zero {
            row.repeat('0', pad)?;
        }
        row.text(&digits)?;
        result.push(row.value);
    }
    Ok(result)
}

fn validate_limits(limits: Limits) -> Result<()> {
    for (name, actual, ceiling) in [
        ("input bytes", limits.max_input_bytes, MAX_INPUT_BYTES),
        ("output bytes", limits.max_output_bytes, MAX_OUTPUT_BYTES),
        ("lines", limits.max_lines, MAX_LINES),
        (
            "generated rows",
            limits.max_generated_rows,
            MAX_GENERATED_ROWS,
        ),
        ("row bytes", limits.max_row_bytes, MAX_ROW_BYTES),
    ] {
        if actual > ceiling {
            return Err(Error::InvalidLimit { name });
        }
    }
    Ok(())
}

struct Output {
    value: String,
    limit: usize,
}

impl Output {
    fn new(limit: usize) -> Self {
        Self {
            value: String::new(),
            limit,
        }
    }

    fn check(&self, additional: usize) -> Result<()> {
        if additional > self.limit - self.value.len() {
            return Err(Error::OutputTooLarge { limit: self.limit });
        }
        Ok(())
    }

    fn text(&mut self, text: &str) -> Result<()> {
        self.check(text.len())?;
        self.value.push_str(text);
        Ok(())
    }

    fn character(&mut self, ch: char) -> Result<()> {
        self.check(ch.len_utf8())?;
        self.value.push(ch);
        Ok(())
    }

    fn repeat(&mut self, ch: char, count: usize) -> Result<()> {
        let size = count
            .checked_mul(ch.len_utf8())
            .ok_or(Error::OutputTooLarge { limit: self.limit })?;
        self.check(size)?;
        self.value.extend(std::iter::repeat_n(ch, count));
        Ok(())
    }
}

#[derive(Clone, Copy)]
pub(crate) struct Line<'a> {
    pub(crate) body: &'a str,
    pub(crate) ending: &'a str,
}

pub(crate) fn split_lines(text: &str, limit: usize) -> Result<Vec<Line<'_>>> {
    let mut result = Vec::new();
    let bytes = text.as_bytes();
    let mut start = 0;
    while start < text.len() {
        if result.len() >= limit {
            return Err(Error::TooManyLines { limit });
        }
        let mut end = start;
        while end < bytes.len() && !matches!(bytes[end], b'\r' | b'\n') {
            end += 1;
        }
        let next = if end == bytes.len() {
            end
        } else if bytes[end] == b'\r' && bytes.get(end + 1) == Some(&b'\n') {
            end + 2
        } else {
            end + 1
        };
        result.push(Line {
            body: &text[start..end],
            ending: &text[end..next],
        });
        start = next;
    }
    Ok(result)
}

fn render_filtered(
    original: &[Line<'_>],
    selected: &[Line<'_>],
    final_newline: FinalNewline,
    output: &mut Output,
) -> Result<()> {
    for (index, line) in selected.iter().enumerate() {
        output.text(line.body)?;
        let ending = if index + 1 < selected.len() {
            line.ending
        } else if final_newline == FinalNewline::Preserve {
            original.last().map_or("", |last| last.ending)
        } else {
            ""
        };
        output.text(ending)?;
    }
    Ok(())
}

fn apostrophe(ch: char) -> bool {
    matches!(ch, '\'' | '\u{2018}' | '\u{2019}')
}

fn change_case(text: &str, mode: CaseMode, output: &mut Output) -> Result<()> {
    let mut previous: Option<char> = None;
    let mut previous_previous: Option<char> = None;
    let mut new_sentence = true;
    let mut saw_cr = false;
    let mut saw_lf = false;
    let mut input = text.chars().peekable();
    while let Some(ch) = input.next() {
        let next = input.peek().copied();
        let mut upper = false;
        let mut lower = false;
        match mode {
            CaseMode::Invert => {
                upper = ch.is_lowercase();
                lower = !upper;
            }
            CaseMode::ProperForce | CaseMode::ProperBlend if ch.is_alphabetic() => {
                let internal_apostrophe = previous.is_some_and(apostrophe)
                    && previous_previous.is_some_and(char::is_alphanumeric);
                upper = !internal_apostrophe && !previous.is_some_and(char::is_alphanumeric);
                lower = !upper && mode == CaseMode::ProperForce;
            }
            CaseMode::SentenceForce | CaseMode::SentenceBlend => {
                if ch.is_alphabetic() {
                    upper = new_sentence;
                    lower = !upper && mode == CaseMode::SentenceForce;
                    new_sentence = false;
                    saw_cr = false;
                    saw_lf = false;
                    let english_i = matches!(ch, 'i' | 'I')
                        && previous.is_some_and(|c| c.is_whitespace() || matches!(c, '(' | '"'))
                        && next.is_some_and(|c| c.is_whitespace() || c == '\'');
                    if english_i {
                        upper = true;
                        lower = false;
                    }
                } else if matches!(ch, '.' | '!' | '?') {
                    new_sentence = next.is_some_and(|c| !c.is_alphanumeric());
                } else if ch == '\r' {
                    new_sentence |= saw_cr;
                    saw_cr = true;
                } else if ch == '\n' {
                    new_sentence |= saw_lf;
                    saw_lf = true;
                }
            }
            _ => {}
        }
        if upper {
            for mapped in ch.to_uppercase() {
                output.character(mapped)?;
            }
        } else if lower {
            for mapped in ch.to_lowercase() {
                output.character(mapped)?;
            }
        } else {
            output.character(ch)?;
        }
        previous_previous = previous;
        previous = Some(ch);
    }
    Ok(())
}

fn tabs(
    records: &[Line<'_>],
    options: TabOptions,
    leading: bool,
    output: &mut Output,
) -> Result<()> {
    if options.width == 0 || options.width > MAX_TAB_WIDTH {
        return Err(Error::InvalidOption { name: "tab width" });
    }
    if options.first_column > MAX_INPUT_BYTES {
        return Err(Error::InvalidOption {
            name: "first column",
        });
    }
    for (index, line) in records.iter().enumerate() {
        let initial = if index == 0 { options.first_column } else { 0 };
        let mut column = initial;
        let advance = |column: usize, count: usize| {
            column
                .checked_add(count)
                .ok_or(Error::ColumnOverflow { line: index + 1 })
        };
        if leading {
            let prefix = line
                .body
                .bytes()
                .take_while(|ch| matches!(ch, b' ' | b'\t'))
                .count();
            for ch in line.body[..prefix].bytes() {
                column = advance(
                    column,
                    if ch == b'\t' {
                        options.width - column % options.width
                    } else {
                        1
                    },
                )?;
            }
            let mut emitted = initial;
            while options.width - emitted % options.width <= column - emitted {
                let next_stop = advance(emitted, options.width - emitted % options.width)?;
                output.character('\t')?;
                emitted = next_stop;
            }
            output.repeat(' ', column - emitted)?;
            output.text(&line.body[prefix..])?;
        } else {
            for ch in line.body.chars() {
                if ch == '\t' {
                    let width = options.width - column % options.width;
                    output.repeat(' ', width)?;
                    column = advance(column, width)?;
                } else {
                    output.character(ch)?;
                    column = advance(column, 1)?;
                }
            }
        }
        output.text(line.ending)?;
    }
    Ok(())
}

struct Number {
    sign: i8,
    digits: String,
    scale: usize,
}

impl Number {
    fn parse(text: &str, kind: NumericKind, line: usize) -> Result<Self> {
        let text = text.trim_matches([' ', '\t']);
        let (negative, unsigned) = if let Some(body) = text.strip_prefix('-') {
            (true, body)
        } else {
            (false, text.strip_prefix('+').unwrap_or(text))
        };
        let invalid = || Error::InvalidNumber { line };
        let separator = match kind {
            NumericKind::Integer => None,
            NumericKind::DecimalDot => Some('.'),
            NumericKind::DecimalComma => Some(','),
        };
        let (integer, fraction) = separator
            .and_then(|sep| unsigned.split_once(sep))
            .unwrap_or((unsigned, ""));
        if (integer.is_empty() && fraction.is_empty())
            || !integer.bytes().all(|c| c.is_ascii_digit())
            || !fraction.bytes().all(|c| c.is_ascii_digit())
        {
            return Err(invalid());
        }
        let fraction = fraction.trim_end_matches('0');
        let mut coefficient = 0i128;
        for digit in integer.bytes().chain(fraction.bytes()) {
            coefficient = coefficient
                .checked_mul(10)
                .and_then(|value| {
                    if negative {
                        value.checked_sub(i128::from(digit - b'0'))
                    } else {
                        value.checked_add(i128::from(digit - b'0'))
                    }
                })
                .ok_or(Error::NumericOverflow { line })?;
        }
        Ok(Self {
            sign: coefficient.signum() as i8,
            digits: coefficient.unsigned_abs().to_string(),
            scale: fraction.len(),
        })
    }

    fn compare(&self, other: &Self) -> Ordering {
        let sign_order = self.sign.cmp(&other.sign);
        if sign_order != Ordering::Equal || self.sign == 0 {
            return sign_order;
        }
        // Input limits bound scale well within i64, including on 32-bit targets.
        let exponent = self.digits.len() as i64 - self.scale as i64;
        let other_exponent = other.digits.len() as i64 - other.scale as i64;
        let mut order = exponent.cmp(&other_exponent);
        if order == Ordering::Equal {
            let width = self.digits.len().max(other.digits.len());
            order = self
                .digits
                .bytes()
                .chain(std::iter::repeat(b'0'))
                .take(width)
                .cmp(
                    other
                        .digits
                        .bytes()
                        .chain(std::iter::repeat(b'0'))
                        .take(width),
                );
        }
        if self.sign < 0 {
            order.reverse()
        } else {
            order
        }
    }
}

fn sort_numbers(
    records: &[Line<'_>],
    options: NumericSortOptions,
    output: &mut Output,
) -> Result<()> {
    let mut keyed = Vec::with_capacity(records.len());
    for (index, record) in records.iter().enumerate() {
        let blank = record.body.trim_matches([' ', '\t']).is_empty();
        let number = if blank && options.blanks != BlankNumbers::Reject {
            None
        } else {
            Some(Number::parse(record.body, options.kind, index + 1)?)
        };
        keyed.push((record, number));
    }
    keyed.sort_by(|(_, left), (_, right)| match (left, right) {
        (None, None) => Ordering::Equal,
        (None, Some(_)) => {
            if options.blanks == BlankNumbers::First {
                Ordering::Less
            } else {
                Ordering::Greater
            }
        }
        (Some(_), None) => {
            if options.blanks == BlankNumbers::First {
                Ordering::Greater
            } else {
                Ordering::Less
            }
        }
        (Some(left), Some(right)) => {
            let order = left.compare(right);
            if options.order == SortOrder::Descending {
                order.reverse()
            } else {
                order
            }
        }
    });
    for ((line, _), slot) in keyed.iter().zip(records) {
        output.text(line.body)?;
        output.text(slot.ending)?;
    }
    Ok(())
}

fn number_width(value: i128, base: NumberBase) -> usize {
    let radix = match base {
        NumberBase::Decimal => 10,
        NumberBase::HexLower | NumberBase::HexUpper => 16,
        NumberBase::Octal => 8,
        NumberBase::Binary => 2,
    };
    let mut magnitude = value.unsigned_abs();
    let mut digits = 1 + usize::from(value < 0);
    while magnitude >= radix {
        magnitude /= radix;
        digits += 1;
    }
    digits
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(operation: Operation, text: &str) -> String {
        transform(operation, text, Limits::default()).unwrap()
    }

    fn numeric(kind: NumericKind, order: SortOrder, text: &str) -> String {
        run(
            Operation::SortNumeric(NumericSortOptions {
                kind,
                order,
                blanks: BlankNumbers::Reject,
            }),
            text,
        )
    }

    fn numbers(options: ColumnNumberOptions) -> Vec<String> {
        column_numbers(options, Limits::default()).unwrap()
    }

    #[test]
    fn proper_force_and_blend_preserve_apostrophes_and_unicode() {
        let input = "hELLO dON'T o’NEILL ‘qUOTE’ 3D café🙂WORLD";
        assert_eq!(
            run(Operation::Case(CaseMode::ProperForce), input),
            "Hello Don't O’neill ‘Quote’ 3d Café🙂World"
        );
        assert_eq!(
            run(Operation::Case(CaseMode::ProperBlend), input),
            "HELLO DON'T O’NEILL ‘QUOTE’ 3D Café🙂WORLD"
        );
        assert_eq!(
            run(
                Operation::Case(CaseMode::ProperForce),
                "éCOLE\tΩΜΈΓΑ\r\nstraße"
            ),
            "École\tΩμέγα\r\nStraße"
        );
    }

    #[test]
    fn invert_case_expands_unicode_without_corrupting_scalars() {
        assert_eq!(
            run(Operation::Case(CaseMode::Invert), "Straße İΣﬃ𐐀𐐨\r\n"),
            "sTRASSE i\u{307}σFFI𐐨𐐀\r\n"
        );
        assert_eq!(
            run(Operation::Case(CaseMode::Invert), "e\u{301}🙂\0"),
            "E\u{301}🙂\0"
        );
    }

    #[test]
    fn sentence_case_uses_upstream_punctuation_paragraph_and_english_i_rules() {
        let input = "hELLO WORLD. nEXT!yes? 123 fOO\r\nsTILL\r\n\r\nnEW i AM (i'm) and i.";
        assert_eq!(
            run(Operation::Case(CaseMode::SentenceForce), input),
            "Hello world. Next!yes? 123 Foo\r\nstill\r\n\r\nNew I am (I'm) and i."
        );
        assert_eq!(
            run(
                Operation::Case(CaseMode::SentenceBlend),
                "hELLO WORLD. nEXT\rnext"
            ),
            "HELLO WORLD. NEXT\rnext"
        );
        assert_eq!(
            run(Operation::Case(CaseMode::SentenceForce), "éCOLE? ωΜΈΓΑ"),
            "École? Ωμέγα"
        );
    }

    #[test]
    fn case_modes_preserve_mixed_eols_and_empty_final_newline_edges() {
        for mode in [
            CaseMode::ProperForce,
            CaseMode::ProperBlend,
            CaseMode::SentenceForce,
            CaseMode::SentenceBlend,
            CaseMode::Invert,
        ] {
            for text in ["", "\r\n", "\n\r\r\n", "a\r\nB\rc\nD", "a\n"] {
                let result = run(Operation::Case(mode), text);
                let before: Vec<_> = split_lines(text, MAX_LINES)
                    .unwrap()
                    .iter()
                    .map(|l| l.ending)
                    .collect();
                let after: Vec<_> = split_lines(&result, MAX_LINES)
                    .unwrap()
                    .iter()
                    .map(|l| l.ending)
                    .collect();
                assert_eq!(before, after);
            }
        }
        assert_eq!(
            run(Operation::Case(CaseMode::Invert), "a\r\nB\rc\nD"),
            "A\r\nb\rC\nd"
        );
    }

    #[test]
    fn reverse_keeps_eol_slots_without_inventing_a_phantom_row() {
        for (before, after) in [
            ("", ""),
            ("a", "a"),
            ("\n", "\n"),
            ("a\n", "a\n"),
            ("\na", "a\n"),
            ("a\n\n", "\na\n"),
            ("a\r\nb\rc\nlast", "last\r\nc\rb\na"),
            ("a\r\nb\rc\n", "c\r\nb\ra\n"),
        ] {
            assert_eq!(run(Operation::ReverseLines, before), after);
        }
    }

    #[test]
    fn consecutive_duplicates_are_content_only_case_sensitive_and_local() {
        assert_eq!(
            run(Operation::RemoveConsecutiveDuplicates, "a\r\na\nb\rb\nb"),
            "a\r\nb"
        );
        assert_eq!(
            run(Operation::RemoveConsecutiveDuplicates, "a\nb\na\n"),
            "a\nb\na\n"
        );
        assert_eq!(
            run(Operation::RemoveConsecutiveDuplicates, "a\nA\na"),
            "a\nA\na"
        );
        assert_eq!(
            run(Operation::RemoveConsecutiveDuplicates, "\r\n\n\r"),
            "\r"
        );
        assert_eq!(run(Operation::RemoveConsecutiveDuplicates, ""), "");
        assert_eq!(run(Operation::RemoveConsecutiveDuplicates, "é\né"), "é");
        assert_eq!(
            run(Operation::RemoveConsecutiveDuplicates, "é\ne\u{301}"),
            "é\ne\u{301}"
        );
    }

    #[test]
    fn empty_and_ascii_blank_line_removal_have_explicit_terminal_policy() {
        let empty = Operation::RemoveEmptyLines {
            whitespace: false,
            final_newline: FinalNewline::Preserve,
        };
        let blank = Operation::RemoveEmptyLines {
            whitespace: true,
            final_newline: FinalNewline::Preserve,
        };
        let no_final = Operation::RemoveEmptyLines {
            whitespace: true,
            final_newline: FinalNewline::Remove,
        };
        let input = " a \r\n\r\n \t\rb\n\t";
        assert_eq!(run(empty, input), " a \r\n \t\rb\n\t");
        assert_eq!(run(blank, input), " a \r\nb");
        assert_eq!(run(blank, "a\r\n\r\n"), "a\r\n");
        assert_eq!(run(no_final, "a\r\n\r\n"), "a");
        assert_eq!(run(empty, "\t  "), "\t  ");
        assert_eq!(run(blank, "\t  "), "");
        assert_eq!(run(blank, "\u{a0}\n"), "\u{a0}\n");
        for input in ["", "\r\n", "\r\n\n\r", " \r\n\t\n"] {
            assert_eq!(run(blank, input), "");
        }
    }

    #[test]
    fn trim_leading_and_both_only_trim_ascii_margins() {
        let input = " \talpha \t\r\n\t beta\t\rgamma \n \t";
        assert_eq!(
            run(Operation::TrimLeading, input),
            "alpha \t\r\nbeta\t\rgamma \n"
        );
        assert_eq!(run(Operation::TrimBoth, input), "alpha\r\nbeta\rgamma\n");
        assert_eq!(run(Operation::TrimBoth, " \u{a0} \t"), "\u{a0}");
        assert_eq!(run(Operation::TrimLeading, ""), "");
    }

    #[test]
    fn tab_expansion_uses_scalar_columns_and_resets_at_every_eol() {
        assert_eq!(
            run(
                Operation::TabsToSpaces(TabOptions::default()),
                "\té\t中\t🙂\tx\r\nab\tz\rc\t"
            ),
            "    é   中   🙂   x\r\nab  z\rc   "
        );
        assert_eq!(
            run(
                Operation::TabsToSpaces(TabOptions {
                    width: 4,
                    first_column: 2
                }),
                "é\tX\n\tY"
            ),
            "é X\n    Y"
        );
        assert_eq!(
            run(
                Operation::TabsToSpaces(TabOptions {
                    width: 1,
                    first_column: 0
                }),
                "a\t"
            ),
            "a "
        );
        assert_eq!(
            run(Operation::TabsToSpaces(TabOptions::default()), "e\u{301}\t"),
            "e\u{301}  "
        );
    }

    #[test]
    fn leading_spaces_to_tabs_canonicalizes_prefixes_but_not_interiors() {
        assert_eq!(
            run(
                Operation::LeadingSpacesToTabs(TabOptions::default()),
                "      alpha  beta\t \r\n \t  gamma\n\t  \tdelta\r   "
            ),
            "\t  alpha  beta\t \r\n\t  gamma\n\t\tdelta\r   "
        );
        assert_eq!(
            run(
                Operation::LeadingSpacesToTabs(TabOptions {
                    width: 4,
                    first_column: 2
                }),
                "  x"
            ),
            "\tx"
        );
    }

    #[test]
    fn indentation_compression_preserves_expanded_columns_exhaustively() {
        for width in 1..=8 {
            for first_column in [0, 1, 3, 4, 99] {
                let options = TabOptions {
                    width,
                    first_column,
                };
                for mask in 0..64usize {
                    let prefix: String = (0..6)
                        .map(|bit| if mask & (1 << bit) == 0 { ' ' } else { '\t' })
                        .collect();
                    let input = format!("{prefix}x  y\t\r\n{prefix}é\n");
                    let compressed = run(Operation::LeadingSpacesToTabs(options), &input);
                    assert_eq!(
                        run(Operation::TabsToSpaces(options), &compressed),
                        run(Operation::TabsToSpaces(options), &input)
                    );
                }
            }
        }
    }

    #[test]
    fn integer_sort_is_exact_above_f64_precision_and_preserves_eol_slots() {
        assert_eq!(
            numeric(
                NumericKind::Integer,
                SortOrder::Ascending,
                "10\r\n-2\r9007199254740993\n9007199254740992"
            ),
            "-2\r\n10\r9007199254740992\n9007199254740993"
        );
        assert_eq!(
            numeric(NumericKind::Integer, SortOrder::Ascending, " 2 \n\t-1\t"),
            "\t-1\t\n 2 "
        );
    }

    #[test]
    fn numeric_equal_values_are_stable_in_both_directions() {
        let source = "01\n+1\n1\n-0\n0\n-1";
        assert_eq!(
            numeric(NumericKind::Integer, SortOrder::Descending, source),
            source
        );
        assert_eq!(
            numeric(NumericKind::Integer, SortOrder::Ascending, source),
            "-1\n-0\n0\n01\n+1\n1"
        );
        assert_eq!(
            numeric(
                NumericKind::DecimalDot,
                SortOrder::Ascending,
                ".5\n1.\n+.50\n-.0\n0.00"
            ),
            "-.0\n0.00\n.5\n+.50\n1."
        );
    }

    #[test]
    fn dot_and_comma_decimal_sorts_do_not_round_or_use_thousands_separators() {
        assert_eq!(
            numeric(
                NumericKind::DecimalDot,
                SortOrder::Ascending,
                "9007199254740993.1\n9007199254740993.01\n9007199254740992.99"
            ),
            "9007199254740992.99\n9007199254740993.01\n9007199254740993.1"
        );
        assert_eq!(
            numeric(
                NumericKind::DecimalComma,
                SortOrder::Ascending,
                "10,2\r\n-1,5\n,25\r10,02"
            ),
            "-1,5\r\n,25\n10,02\r10,2"
        );
        assert_eq!(
            numeric(NumericKind::DecimalComma, SortOrder::Ascending, "2\n1,234"),
            "1,234\n2"
        );
    }

    #[test]
    fn decimal_comparison_never_multiplies_coefficients_to_align_scales() {
        let tiny = "0.00000000000000000000000000000000000001";
        let large = i128::MAX.to_string();
        let source = format!("{large}\n{tiny}\n-{tiny}\n0");
        assert_eq!(
            numeric(NumericKind::DecimalDot, SortOrder::Ascending, &source),
            format!("-{tiny}\n0\n{tiny}\n{large}")
        );
        assert_eq!(
            numeric(
                NumericKind::DecimalDot,
                SortOrder::Ascending,
                &format!("{large}.0\n-{large}.000")
            ),
            format!("-{large}.000\n{large}.0")
        );
    }

    #[test]
    fn exact_comparison_agrees_with_small_checked_rational_values() {
        for a in -20i128..=20 {
            for b in -20i128..=20 {
                for scale_a in 0..4usize {
                    for scale_b in 0..4usize {
                        let left = Number {
                            sign: a.signum() as i8,
                            digits: a.unsigned_abs().to_string(),
                            scale: scale_a,
                        };
                        let right = Number {
                            sign: b.signum() as i8,
                            digits: b.unsigned_abs().to_string(),
                            scale: scale_b,
                        };
                        let reference =
                            (a * 10i128.pow(scale_b as u32)).cmp(&(b * 10i128.pow(scale_a as u32)));
                        assert_eq!(left.compare(&right), reference);
                    }
                }
            }
        }
    }

    #[test]
    fn blank_numeric_lines_are_rejected_or_explicitly_positioned() {
        assert_eq!(
            transform(
                Operation::SortNumeric(NumericSortOptions::default()),
                "1\n \t\n2",
                Limits::default()
            ),
            Err(Error::InvalidNumber { line: 2 })
        );
        assert_eq!(
            run(
                Operation::SortNumeric(NumericSortOptions {
                    blanks: BlankNumbers::First,
                    ..Default::default()
                }),
                "2\n \t\n1"
            ),
            " \t\n1\n2"
        );
        assert_eq!(
            run(
                Operation::SortNumeric(NumericSortOptions {
                    order: SortOrder::Descending,
                    blanks: BlankNumbers::Last,
                    ..Default::default()
                }),
                "1\n \t\n2"
            ),
            "2\n1\n \t"
        );
        assert_eq!(numeric(NumericKind::Integer, SortOrder::Ascending, ""), "");
    }

    #[test]
    fn numeric_grammar_rejects_partial_parses_and_non_ascii_numbers() {
        for text in [
            "", "+", "-", "1e3", "NaN", "inf", "--1", "+-1", "1 2", "1x", "١", "−1", "\u{a0}1",
        ] {
            assert_eq!(
                Number::parse(text, NumericKind::Integer, 7).err(),
                Some(Error::InvalidNumber { line: 7 })
            );
        }
        for (kind, text) in [
            (NumericKind::Integer, "1.0"),
            (NumericKind::DecimalDot, "1,2"),
            (NumericKind::DecimalComma, "1.2"),
            (NumericKind::DecimalDot, "."),
            (NumericKind::DecimalDot, "-."),
            (NumericKind::DecimalDot, "1.2.3"),
            (NumericKind::DecimalComma, "1,234,567"),
        ] {
            assert_eq!(
                Number::parse(text, kind, 3).err(),
                Some(Error::InvalidNumber { line: 3 })
            );
        }
    }

    #[test]
    fn signed_numeric_boundaries_and_overflow_are_explicit() {
        let maximum = i128::MAX.to_string();
        let minimum = i128::MIN.to_string();
        assert_eq!(
            numeric(
                NumericKind::Integer,
                SortOrder::Ascending,
                &format!("{maximum}\n{minimum}")
            ),
            format!("{minimum}\n{maximum}")
        );
        assert!(Number::parse(&format!("{minimum}.0"), NumericKind::DecimalDot, 1).is_ok());
        for text in [
            "170141183460469231731687303715884105728",
            "-170141183460469231731687303715884105729",
        ] {
            assert_eq!(
                Number::parse(text, NumericKind::Integer, 2).err(),
                Some(Error::NumericOverflow { line: 2 })
            );
        }
        assert_eq!(
            Number::parse(&format!("{maximum}.1"), NumericKind::DecimalDot, 4).err(),
            Some(Error::NumericOverflow { line: 4 })
        );
    }

    #[test]
    fn text_limits_reject_input_output_counts_and_invalid_options() {
        assert_eq!(
            transform(
                Operation::TrimBoth,
                "é",
                Limits {
                    max_input_bytes: 1,
                    ..Default::default()
                }
            ),
            Err(Error::InputTooLarge { limit: 1 })
        );
        assert_eq!(
            transform(
                Operation::Case(CaseMode::Invert),
                "İ",
                Limits {
                    max_output_bytes: 2,
                    ..Default::default()
                }
            ),
            Err(Error::OutputTooLarge { limit: 2 })
        );
        assert_eq!(
            transform(
                Operation::TabsToSpaces(TabOptions::default()),
                "\t",
                Limits {
                    max_output_bytes: 3,
                    ..Default::default()
                }
            ),
            Err(Error::OutputTooLarge { limit: 3 })
        );
        assert_eq!(
            transform(
                Operation::ReverseLines,
                "a\r\nb",
                Limits {
                    max_lines: 1,
                    ..Default::default()
                }
            ),
            Err(Error::TooManyLines { limit: 1 })
        );
        assert_eq!(
            transform(
                Operation::TrimBoth,
                " \t a  ",
                Limits {
                    max_output_bytes: 1,
                    ..Default::default()
                }
            )
            .unwrap(),
            "a"
        );
        for width in [0, MAX_TAB_WIDTH + 1] {
            assert_eq!(
                transform(
                    Operation::TabsToSpaces(TabOptions {
                        width,
                        first_column: 0
                    }),
                    "",
                    Limits::default()
                ),
                Err(Error::InvalidOption { name: "tab width" })
            );
        }
        assert_eq!(
            transform(
                Operation::LeadingSpacesToTabs(TabOptions {
                    width: 4,
                    first_column: usize::MAX
                }),
                "",
                Limits::default()
            ),
            Err(Error::InvalidOption {
                name: "first column"
            })
        );
        assert!(matches!(
            transform(
                Operation::TrimBoth,
                "",
                Limits {
                    max_output_bytes: MAX_OUTPUT_BYTES + 1,
                    ..Default::default()
                }
            ),
            Err(Error::InvalidLimit { .. })
        ));
    }

    #[test]
    fn zero_limits_allow_genuinely_empty_results_and_terminal_markers_count_once() {
        let limits = Limits {
            max_input_bytes: 0,
            max_output_bytes: 0,
            max_lines: 0,
            ..Default::default()
        };
        assert_eq!(transform(Operation::ReverseLines, "", limits).unwrap(), "");
        assert_eq!(
            transform(
                Operation::ReverseLines,
                "a\r\n",
                Limits {
                    max_lines: 1,
                    ..Default::default()
                }
            )
            .unwrap(),
            "a\r\n"
        );
        assert_eq!(
            transform(
                Operation::RemoveEmptyLines {
                    whitespace: false,
                    final_newline: FinalNewline::Preserve
                },
                "\r\n",
                Limits {
                    max_output_bytes: 0,
                    ..Default::default()
                }
            )
            .unwrap(),
            ""
        );
    }

    #[test]
    fn column_sequences_are_signed_and_support_all_requested_bases() {
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: -2,
                rows: 5,
                ..Default::default()
            }),
            ["-2", "-1", "0", "1", "2"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: -16,
                rows: 3,
                base: NumberBase::HexUpper,
                ..Default::default()
            }),
            ["-10", "-F", "-E"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: 15,
                rows: 2,
                base: NumberBase::HexLower,
                ..Default::default()
            }),
            ["f", "10"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: 8,
                rows: 3,
                base: NumberBase::Octal,
                ..Default::default()
            }),
            ["10", "11", "12"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: 2,
                rows: 3,
                base: NumberBase::Binary,
                ..Default::default()
            }),
            ["10", "11", "100"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions::default()),
            Vec::<String>::new()
        );
    }

    #[test]
    fn column_padding_honors_signs_minimum_width_and_auto_alignment() {
        let base = ColumnNumberOptions {
            start: -16,
            rows: 3,
            base: NumberBase::HexUpper,
            ..Default::default()
        };
        assert_eq!(
            numbers(ColumnNumberOptions {
                padding: Padding::Minimum {
                    width: 4,
                    fill: PaddingFill::Zero
                },
                ..base
            }),
            ["-010", "-00F", "-00E"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                padding: Padding::Minimum {
                    width: 4,
                    fill: PaddingFill::Space
                },
                ..base
            }),
            [" -10", "  -F", "  -E"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                padding: Padding::Auto {
                    fill: PaddingFill::Zero
                },
                ..base
            }),
            ["-10", "-0F", "-0E"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: -12,
                step: 10,
                rows: 3,
                padding: Padding::Auto {
                    fill: PaddingFill::Space
                },
                ..Default::default()
            }),
            ["-12", " -2", "  8"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: 100,
                rows: 1,
                padding: Padding::Minimum {
                    width: 1,
                    fill: PaddingFill::Zero
                },
                ..Default::default()
            }),
            ["100"]
        );
    }

    #[test]
    fn column_repeat_is_bounded_and_does_not_step_past_the_last_row() {
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: 9,
                rows: 5,
                repeat: 2,
                padding: Padding::Auto {
                    fill: PaddingFill::Zero
                },
                ..Default::default()
            }),
            ["09", "09", "10", "10", "11"]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: i128::MAX,
                rows: 1,
                ..Default::default()
            }),
            [i128::MAX.to_string()]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: i128::MAX,
                rows: 2,
                repeat: 2,
                ..Default::default()
            }),
            [i128::MAX.to_string(), i128::MAX.to_string()]
        );
        assert_eq!(
            column_numbers(
                ColumnNumberOptions {
                    start: i128::MAX,
                    rows: 3,
                    repeat: 2,
                    ..Default::default()
                },
                Limits::default()
            ),
            Err(Error::SequenceOverflow { row: 3 })
        );
    }

    #[test]
    fn column_checked_steps_support_extremes_without_false_multiplication_overflow() {
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: i128::MIN,
                step: i128::MAX,
                rows: 3,
                ..Default::default()
            }),
            [
                i128::MIN.to_string(),
                "-1".to_owned(),
                (i128::MAX - 1).to_string()
            ]
        );
        assert_eq!(
            numbers(ColumnNumberOptions {
                start: i128::MIN,
                rows: 1,
                base: NumberBase::Binary,
                ..Default::default()
            }),
            [format!("-1{}", "0".repeat(127))]
        );
        assert_eq!(
            column_numbers(
                ColumnNumberOptions {
                    start: i128::MIN,
                    step: -1,
                    rows: 2,
                    ..Default::default()
                },
                Limits::default()
            ),
            Err(Error::SequenceOverflow { row: 2 })
        );
    }

    #[test]
    fn column_output_row_and_count_limits_are_checked_before_returning_any_rows() {
        let options = ColumnNumberOptions {
            rows: 2,
            padding: Padding::Minimum {
                width: 4,
                fill: PaddingFill::Zero,
            },
            ..Default::default()
        };
        assert_eq!(
            column_numbers(
                options,
                Limits {
                    max_output_bytes: 7,
                    ..Default::default()
                }
            ),
            Err(Error::OutputTooLarge { limit: 7 })
        );
        assert_eq!(
            column_numbers(
                options,
                Limits {
                    max_row_bytes: 3,
                    ..Default::default()
                }
            ),
            Err(Error::RowTooLarge { row: 1, limit: 3 })
        );
        assert_eq!(
            column_numbers(
                options,
                Limits {
                    max_generated_rows: 1,
                    ..Default::default()
                }
            ),
            Err(Error::TooManyRows { limit: 1 })
        );
        assert_eq!(
            column_numbers(
                ColumnNumberOptions {
                    repeat: 0,
                    ..Default::default()
                },
                Limits::default()
            ),
            Err(Error::InvalidOption { name: "repeat" })
        );
        assert_eq!(
            column_numbers(
                ColumnNumberOptions {
                    padding: Padding::Minimum {
                        width: MAX_ROW_BYTES + 1,
                        fill: PaddingFill::Space
                    },
                    ..Default::default()
                },
                Limits::default()
            ),
            Err(Error::InvalidOption {
                name: "padding width"
            })
        );
        assert!(column_numbers(
            ColumnNumberOptions::default(),
            Limits {
                max_generated_rows: 0,
                max_output_bytes: 0,
                ..Default::default()
            }
        )
        .unwrap()
        .is_empty());
    }
}
