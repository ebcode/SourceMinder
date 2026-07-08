// Methods declared in traits without bodies (function_signature_item)

pub trait Storage {
    // Bodyless signatures
    fn read(&self, key: &str) -> Option<Vec<u8>>;
    fn write(&mut self, key: &str, value: &[u8]) -> bool;
    fn delete(&mut self, key: &str);

    // Associated type
    type Iter<'a>: Iterator<Item = &'a str> where Self: 'a;

    // Associated const
    const VERSION: u32 = 1;

    // Provided method (has body)
    fn exists(&self, key: &str) -> bool {
        self.read(key).is_some()
    }
}

pub trait Visitor {
    fn visit_node(&mut self, name: &str);
    fn visit_edge(&mut self, from: &str, to: &str);
    fn finish(self) -> u32;
}

// Trait with supertrait and default methods
pub trait Serializable: Send + Sync + 'static {
    fn serialize(&self) -> Vec<u8>;

    fn serialize_size(&self) -> usize {
        self.serialize().len()
    }
}
