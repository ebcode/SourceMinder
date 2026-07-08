#![allow(dead_code)]

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Vector2 {
    pub x: i32,
    pub y: i32,
}

#[derive(Default)]
pub struct Settings {
    pub verbose: bool,
    pub workers: usize,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_vector_equality() {
        let a = Vector2 { x: 1, y: 2 };
        let b = Vector2 { x: 1, y: 2 };
        assert_eq!(a, b);
    }

    #[test]
    #[should_panic]
    fn test_panics() {
        panic!("boom");
    }
}

#[inline]
pub fn fast_path(n: u32) -> u32 {
    n.wrapping_mul(2)
}

#[deprecated(note = "use new_api instead")]
pub fn old_api() {}
