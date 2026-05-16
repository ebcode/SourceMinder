// Generic types, lifetimes, where clauses, trait bounds

use std::fmt::{Debug, Display};

pub struct Wrapper<'a, T: 'a + Clone> {
    value: &'a T,
    name: String,
}

impl<'a, T: 'a + Clone + Display> Wrapper<'a, T> {
    pub fn new(value: &'a T, name: String) -> Self {
        Wrapper { value, name }
    }

    pub fn render(&self) -> String {
        format!("{}: {}", self.name, self.value)
    }
}

pub trait Repository<K, V>
where
    K: Eq + std::hash::Hash,
    V: Clone,
{
    fn insert(&mut self, key: K, value: V);
    fn get(&self, key: &K) -> Option<&V>;
}

pub fn print_pair<T, U>(left: T, right: U)
where
    T: Display,
    U: Debug,
{
    println!("{} / {:?}", left, right);
}

pub fn longest<'a>(x: &'a str, y: &'a str) -> &'a str {
    if x.len() >= y.len() { x } else { y }
}

pub fn first_word<'s>(text: &'s str) -> &'s str {
    text.split_whitespace().next().unwrap_or("")
}

// Higher-ranked trait bounds
pub fn apply_fn<F>(callback: F)
where
    F: for<'r> Fn(&'r str) -> usize,
{
    let _ = callback("hello");
}

// Generic associated type via trait
pub trait Stream {
    type Item<'a> where Self: 'a;
    fn next<'a>(&'a mut self) -> Option<Self::Item<'a>>;
}
