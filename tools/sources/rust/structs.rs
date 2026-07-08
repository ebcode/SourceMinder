// Plain struct with named fields
pub struct User {
    pub id: u64,
    pub name: String,
    email: String,
    active: bool,
}

// Tuple struct
pub struct Point(pub f64, pub f64);

// Unit struct
pub struct Marker;

// Struct with generic and lifetime params
pub struct Container<'a, T> {
    inner: &'a T,
    count: usize,
}

// Struct with derive attribute
#[derive(Debug, Clone, PartialEq)]
pub struct Config {
    pub host: String,
    pub port: u16,
}

// Impl block with methods and associated function
impl User {
    pub fn new(id: u64, name: String, email: String) -> Self {
        User { id, name, email, active: true }
    }

    pub fn deactivate(&mut self) {
        self.active = false;
    }

    pub fn email(&self) -> &str {
        &self.email
    }
}
