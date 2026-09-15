use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", deny_unknown_fields)]
pub enum Step {
    Insert { text: String },
    Command { command: String },
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Macro {
    pub schema: u32,
    pub name: String,
    pub steps: Vec<Step>,
}
pub const COMMANDS: &[&str] = &[
    "left",
    "right",
    "up",
    "down",
    "home",
    "end",
    "document_start",
    "document_end",
    "backspace",
    "delete",
    "tab",
    "back_tab",
    "select_all",
    "duplicate_line",
    "duplicate_selection",
    "delete_line",
    "word_left",
    "word_right",
    "extend_left",
    "extend_right",
    "extend_up",
    "extend_down",
    "auto_indent",
];
impl Macro {
    pub fn playback_steps(&self, repeats: u32) -> Result<u32, String> {
        self.validate()?;
        if !(1..=1000).contains(&repeats) || self.steps.is_empty() {
            return Err("Playback needs a nonempty macro and 1-1,000 repetitions.".into());
        }
        let total = self
            .steps
            .len()
            .checked_mul(repeats as usize)
            .ok_or("Macro step count overflow.")?;
        if total > 100000 {
            return Err("Repeated playback exceeds 100,000 steps.".into());
        }
        Ok(total as u32)
    }
    pub fn validate(&self) -> Result<(), String> {
        if self.schema != 1 || self.name.len() > 128 || self.steps.len() > 4096 {
            return Err("Unsupported macro schema or size.".into());
        }
        let mut size = 0;
        for step in &self.steps {
            match step {
                Step::Insert { text } => {
                    size += text.len();
                    if text.contains('\0') {
                        return Err("Macro insertion contains NUL text.".into());
                    }
                }
                Step::Command { command } if COMMANDS.contains(&command.as_str()) => {}
                Step::Command { command } => {
                    return Err(format!("Unsupported macro command: {command}"))
                }
            }
        }
        if size > 1024 * 1024 {
            return Err("Macro text exceeds 1 MiB.".into());
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn macro_data_cannot_execute_external_commands() {
        let mut tape = Macro {
            schema: 1,
            name: "example".into(),
            steps: vec![Step::Insert {
                text: "hello".into(),
            }],
        };
        tape.validate().unwrap();
        let serialized = serde_json::to_string(&tape).unwrap();
        serde_json::from_str::<Macro>(&serialized)
            .unwrap()
            .validate()
            .unwrap();
        tape.steps.push(Step::Command {
            command: "run_process".into(),
        });
        assert!(tape.validate().is_err());
    }
    #[test]
    fn repeat_counts_are_bounded_before_playback() {
        let mut tape = Macro {
            schema: 1,
            name: "repeat".into(),
            steps: vec![
                Step::Command {
                    command: "right".into()
                };
                100
            ],
        };
        assert_eq!(tape.playback_steps(1000).unwrap(), 100000);
        assert!(tape.playback_steps(1001).is_err());
        assert!(tape.playback_steps(0).is_err());
        tape.steps.push(Step::Command {
            command: "left".into(),
        });
        assert!(tape.playback_steps(1000).is_err());
        tape.steps.clear();
        assert!(tape.playback_steps(1).is_err());
    }
}
