use std::collections::HashMap;
use std::io::{self, Read, Write};
use std::sync::{Arc, Mutex};
use serde::{Serialize, Deserialize};
use crate::config::Settings;
use super::helpers::format_time;

// Inline module
pub mod auth {
    pub fn login(user: &str) -> bool {
        !user.is_empty()
    }

    pub mod tokens {
        pub fn generate() -> String {
            String::from("token")
        }
    }
}

// Module declaration (extern - resolves to file)
pub mod handlers;

// Re-export
pub use auth::login;
pub use auth::tokens::generate as gen_token;

// Type alias
pub type UserId = u64;
pub type Cache<T> = HashMap<UserId, T>;

// Const and static
pub const MAX_USERS: usize = 1000;
pub static GLOBAL_PREFIX: &str = "app_";
static mut COUNTER: u32 = 0;
