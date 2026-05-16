// Common Rust error patterns: thiserror-style enums, From impls, Result chains

use std::fmt;

#[derive(Debug)]
pub enum AppError {
    NotFound,
    InvalidInput(String),
    Io(std::io::Error),
    Parse { field: String, reason: String },
}

impl fmt::Display for AppError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AppError::NotFound => write!(f, "not found"),
            AppError::InvalidInput(msg) => write!(f, "invalid: {}", msg),
            AppError::Io(err) => write!(f, "io error: {}", err),
            AppError::Parse { field, reason } => {
                write!(f, "parse error in {}: {}", field, reason)
            }
        }
    }
}

impl std::error::Error for AppError {}

impl From<std::io::Error> for AppError {
    fn from(err: std::io::Error) -> Self {
        AppError::Io(err)
    }
}

pub type AppResult<T> = Result<T, AppError>;

pub fn load_config(path: &str) -> AppResult<String> {
    let content = std::fs::read_to_string(path)?;
    if content.is_empty() {
        return Err(AppError::InvalidInput(String::from("empty file")));
    }
    Ok(content)
}
