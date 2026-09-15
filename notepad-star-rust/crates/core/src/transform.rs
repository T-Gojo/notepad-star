pub fn transform(operation: &str, text: &str) -> Result<String, String> {
    if text.len() > 4 * 1024 * 1024 {
        return Err("Text transformations are limited to 4 MiB per operation.".into());
    }
    match operation {
        "uppercase" => return Ok(text.to_uppercase()),
        "lowercase" => return Ok(text.to_lowercase()),
        _ => {}
    }
    let records: Vec<_> = crate::advanced::split_lines(text, 100000)
        .map_err(|e| e.to_string())?
        .into_iter()
        .map(|line| (line.body, line.ending))
        .collect();
    if operation == "trim_trailing" {
        return Ok(records
            .iter()
            .map(|(line, ending)| format!("{}{ending}", line.trim_end_matches([' ', '\t'])))
            .collect());
    }
    let mut content: Vec<_> = records.iter().map(|(line, _)| *line).collect();
    match operation {
        "sort_ascending" => content.sort(),
        "sort_descending" => content.sort_by(|a, b| b.cmp(a)),
        "remove_duplicates" => {
            let mut seen = std::collections::HashSet::new();
            content.retain(|line| seen.insert(*line));
        }
        _ => return Err(format!("Unsupported text transformation: {operation}")),
    }
    let mut result = String::new();
    for (index, line) in content.iter().enumerate() {
        result.push_str(line);
        let ending = if index + 1 == content.len() {
            records.last().map(|(_, ending)| *ending).unwrap_or("")
        } else {
            records[index].1
        };
        result.push_str(ending);
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn line_operations_preserve_mixed_endings_and_terminal_newline() {
        assert_eq!(
            transform("sort_ascending", "z\r\na\rm\n").unwrap(),
            "a\r\nm\rz\n"
        );
        assert_eq!(transform("sort_ascending", "z\ra").unwrap(), "a\rz");
        assert_eq!(
            transform("trim_trailing", "é \r\nb\t\rc \nlast ").unwrap(),
            "é\r\nb\rc\nlast"
        );
        assert_eq!(transform("remove_duplicates", "a\nb\na").unwrap(), "a\nb");
        assert_eq!(
            transform("remove_duplicates", "a\r\na\r\n").unwrap(),
            "a\r\n"
        );
        assert_eq!(transform("sort_ascending", "").unwrap(), "");
        assert!(transform("unknown", "").is_err());
        assert!(transform("sort_ascending", &"\n".repeat(100001)).is_err());
        assert!(transform("uppercase", &"a".repeat(4 * 1024 * 1024 + 1)).is_err());
    }
}
