use serde::{
    de::{MapAccess, Visitor},
    Deserialize, Deserializer, Serialize,
};
use serde_json::value::RawValue;
use std::{collections::HashSet, fmt};

pub const MAX_INPUT: usize = 2 * 1024 * 1024;
pub const MAX_OUTPUT: usize = 8 * 1024 * 1024;
pub const MAX_NODES: usize = 3000;
const MAX_DEPTH: usize = 64;

#[derive(Debug, Serialize)]
pub struct Node {
    pub parent: Option<usize>,
    pub name: String,
    pub kind: &'static str,
    pub value: String,
    pub path: String,
    pub start: usize,
    pub end: usize,
    pub children: usize,
}

#[derive(Debug, Serialize)]
pub struct Preview {
    pub nodes: Vec<Node>,
    pub pretty: String,
    pub duplicate_keys: usize,
}

struct Entries<'a>(Vec<(String, &'a RawValue)>);
impl<'de> Deserialize<'de> for Entries<'de> {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct Ordered;
        impl<'de> Visitor<'de> for Ordered {
            type Value = Entries<'de>;
            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("a JSON object")
            }
            fn visit_map<M: MapAccess<'de>>(self, mut map: M) -> Result<Self::Value, M::Error> {
                let mut entries = Vec::new();
                while let Some(entry) = map.next_entry()? {
                    entries.push(entry);
                }
                Ok(Entries(entries))
            }
        }
        deserializer.deserialize_map(Ordered)
    }
}

fn validate(input: &str) -> Result<&RawValue, String> {
    if input.len() > MAX_INPUT {
        return Err("JSON tools support at most 2 MiB. The document was not changed.".into());
    }
    serde_json::from_str::<serde_json::Value>(input).map_err(|e| format!("Invalid JSON: {e}"))?;
    // RawValue preserves number spellings, duplicate keys, escapes and key order.
    serde_json::from_str(input).map_err(|e| format!("Invalid JSON: {e}"))
}

pub fn format(input: &str, pretty: bool) -> Result<String, String> {
    validate(input)?;
    let mut result = Vec::with_capacity(input.len());
    let mut depth = 0usize;
    let mut in_string = false;
    let mut escaped = false;
    let bytes = input.as_bytes();
    let newline = |output: &mut Vec<u8>, nesting: usize| {
        output.push(b'\n');
        output.extend(std::iter::repeat_n(b' ', nesting * 2));
    };
    let mut last = b' ';
    for (index, &byte) in bytes.iter().enumerate() {
        if in_string {
            result.push(byte);
            if escaped {
                escaped = false;
            } else if byte == b'\\' {
                escaped = true;
            } else if byte == b'"' {
                in_string = false;
            }
        } else {
            match byte {
                b' ' | b'\t' | b'\r' | b'\n' => continue,
                b'"' => {
                    in_string = true;
                    result.push(byte);
                }
                b'{' | b'[' => {
                    depth += 1;
                    if depth > MAX_DEPTH {
                        return Err("JSON nesting exceeds 64 levels.".into());
                    }
                    result.push(byte);
                    let next = bytes[index + 1..]
                        .iter()
                        .find(|ch| !ch.is_ascii_whitespace());
                    if pretty && !matches!(next, Some(b'}' | b']')) {
                        newline(&mut result, depth);
                    }
                }
                b'}' | b']' => {
                    depth = depth.checked_sub(1).ok_or("Unbalanced JSON container.")?;
                    if pretty && !matches!(last, b'{' | b'[') {
                        newline(&mut result, depth);
                    }
                    result.push(byte);
                }
                b',' => {
                    result.push(byte);
                    if pretty {
                        newline(&mut result, depth);
                    }
                }
                b':' => {
                    result.push(byte);
                    if pretty {
                        result.push(b' ');
                    }
                }
                _ => result.push(byte),
            }
        }
        last = byte;
        if result.len() > MAX_OUTPUT {
            return Err("Formatted JSON exceeds the 8 MiB output limit.".into());
        }
    }
    String::from_utf8(result).map_err(|e| e.to_string())
}

fn summary(value: &str) -> String {
    let mut chars = value.chars();
    let mut output: String = chars.by_ref().take(120).collect();
    if chars.next().is_some() {
        output.push_str("...");
    }
    output
}

pub fn preview(input: &str) -> Result<Preview, String> {
    let raw = validate(input)?;
    let mut result = Preview {
        nodes: Vec::new(),
        pretty: format(input, true)?,
        duplicate_keys: 0,
    };
    append(input, raw, None, "$".into(), String::new(), 0, &mut result)?;
    Ok(result)
}

fn append(
    source: &str,
    raw: &RawValue,
    parent: Option<usize>,
    name: String,
    path: String,
    depth: usize,
    result: &mut Preview,
) -> Result<(), String> {
    if result.nodes.len() >= MAX_NODES || depth > MAX_DEPTH || path.len() > 4096 {
        return Err("JSON preview supports 3,000 nodes, 64 levels and 4 KiB paths. Collapse the input into a smaller document to inspect it.".into());
    }
    let value = raw.get();
    let start = (value.as_ptr() as usize)
        .checked_sub(source.as_ptr() as usize)
        .ok_or("JSON source range is invalid.")?;
    let end = start + value.len();
    if source.get(start..end) != Some(value) {
        return Err("JSON source range is outside the document.".into());
    }
    let kind = match value.as_bytes()[0] {
        b'{' => "object",
        b'[' => "array",
        b'"' => "string",
        b't' | b'f' => "boolean",
        b'n' => "null",
        _ => "number",
    };
    let scalar = if kind == "string" {
        serde_json::from_str::<String>(value).map_err(|e| e.to_string())?
    } else if kind == "object" || kind == "array" {
        String::new()
    } else {
        value.into()
    };
    let index = result.nodes.len();
    result.nodes.push(Node {
        parent,
        name: summary(&name),
        kind,
        value: summary(&scalar),
        path: path.clone(),
        start,
        end,
        children: 0,
    });
    if kind == "object" {
        let entries: Entries<'_> = serde_json::from_str(value).map_err(|e| e.to_string())?;
        result.nodes[index].children = entries.0.len();
        let mut keys = HashSet::new();
        for (key, child) in entries.0 {
            if !keys.insert(key.clone()) {
                result.duplicate_keys += 1;
            }
            let child_path = format!("{path}/{}", key.replace('~', "~0").replace('/', "~1"));
            append(
                source,
                child,
                Some(index),
                key,
                child_path,
                depth + 1,
                result,
            )?;
        }
    } else if kind == "array" {
        let children: Vec<&RawValue> = serde_json::from_str(value).map_err(|e| e.to_string())?;
        result.nodes[index].children = children.len();
        for (position, child) in children.into_iter().enumerate() {
            append(
                source,
                child,
                Some(index),
                format!("[{position}]"),
                format!("{path}/{position}"),
                depth + 1,
                result,
            )?;
        }
    }
    if matches!(kind, "array" | "object") {
        result.nodes[index].value = format!(
            "{} {}",
            result.nodes[index].children,
            if kind == "array" {
                "items"
            } else {
                "properties"
            }
        );
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pretty_and_minify_preserve_exact_tokens_and_duplicate_keys() {
        let source =
            r#" { "z":9007199254740993123456789,"a":1e+099,"z":"\u0061 [,] \" \\","empty": [ ] } "#;
        let formatted = format(source, true).unwrap();
        assert!(formatted.starts_with("{\n  \"z\": 9007199254740993123456789,"));
        assert!(formatted.contains("\"a\": 1e+099"));
        assert!(formatted.contains(r#""z": "\u0061 [,] \" \\""#));
        assert!(formatted.contains("\"empty\": []"));
        assert_eq!(
            format(&formatted, false).unwrap(),
            format(source, false).unwrap()
        );
        assert_eq!(format(&formatted, true).unwrap(), formatted);
    }

    #[test]
    fn preview_preserves_order_duplicates_unicode_paths_and_source_offsets() {
        let input = " \n{\"a/b~\": [\"日本🚀\", false, null], \"x\":1, \"x\":2}";
        let result = preview(input).unwrap();
        assert_eq!(result.duplicate_keys, 1);
        assert_eq!(result.nodes.len(), 7);
        assert_eq!(result.nodes[1].path, "/a~1b~0");
        assert_eq!(result.nodes[2].path, "/a~1b~0/0");
        assert_eq!(result.nodes[2].parent, Some(1));
        assert_eq!(result.nodes[2].value, "日本🚀");
        for node in &result.nodes {
            assert!(serde_json::from_str::<&RawValue>(&input[node.start..node.end]).is_ok());
        }
        assert_eq!(result.nodes[5].value, "1");
        assert_eq!(result.nodes[6].value, "2");
    }

    #[test]
    fn malformed_json_and_budgets_fail_explicitly() {
        for input in [
            "",
            "{",
            "[1,]",
            r#"{"a": NaN}"#,
            r#""\uD800""#,
            "{} garbage",
        ] {
            assert!(format(input, true).unwrap_err().contains("Invalid JSON"));
            assert!(preview(input).is_err());
        }
        assert!(format(&" ".repeat(MAX_INPUT + 1), true)
            .unwrap_err()
            .contains("2 MiB"));
        assert!(preview(&format!("[{}0]", "0,".repeat(MAX_NODES)))
            .unwrap_err()
            .contains("3,000"));
        assert!(format(&format!("{}0{}", "[".repeat(65), "]".repeat(65)), true).is_err());
        for input in ["true", "null", "-12.3e-10", "\"text\"", "[]", "{}"] {
            assert_eq!(preview(input).unwrap().nodes.len(), 1);
        }
    }
}
