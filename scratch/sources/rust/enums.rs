// Simple C-like enum
pub enum Direction {
    North,
    South,
    East,
    West,
}

// Enum with data variants
pub enum Message {
    Quit,
    Move { x: i32, y: i32 },
    Write(String),
    ChangeColor(i32, i32, i32),
}

// Enum with explicit discriminants
#[repr(u8)]
pub enum Status {
    Pending = 0,
    Active = 1,
    Closed = 2,
}

// Generic enum
pub enum MyResult<T, E> {
    Ok(T),
    Err(E),
}

impl Message {
    pub fn describe(&self) -> &'static str {
        match self {
            Message::Quit => "quit",
            Message::Move { .. } => "move",
            Message::Write(_) => "write",
            Message::ChangeColor(_, _, _) => "color",
        }
    }
}
