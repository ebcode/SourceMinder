// Trait with required and provided methods
pub trait Animal {
    fn name(&self) -> &str;
    fn legs(&self) -> u32;

    fn describe(&self) -> String {
        format!("{} has {} legs", self.name(), self.legs())
    }
}

// Trait with associated type and constant
pub trait Container {
    type Item;
    const CAPACITY: usize;

    fn put(&mut self, item: Self::Item);
    fn get(&self) -> Option<&Self::Item>;
}

// Trait with generic param and supertrait
pub trait Drawable<T>: std::fmt::Debug {
    fn draw(&self, target: &mut T);
}

// Implementation of trait
pub struct Dog {
    name: String,
}

impl Animal for Dog {
    fn name(&self) -> &str {
        &self.name
    }
    fn legs(&self) -> u32 {
        4
    }
}

// Blanket implementation
impl<T: Animal> Drawable<String> for T {
    fn draw(&self, target: &mut String) {
        target.push_str(self.name());
    }
}
