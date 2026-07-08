// Free function with parameters and return type
fn add(a: i32, b: i32) -> i32 {
    a + b
}

// Public function
pub fn greet(name: &str) -> String {
    format!("Hello, {}!", name)
}

// Generic function with where clause
pub fn largest<T>(list: &[T]) -> &T
where
    T: PartialOrd,
{
    let mut largest = &list[0];
    for item in list {
        if item > largest {
            largest = item;
        }
    }
    largest
}

// Async function
pub async fn fetch_user(id: u64) -> Result<String, std::io::Error> {
    Ok(format!("user_{}", id))
}

// Unsafe function
pub unsafe fn dangerous(ptr: *mut i32) {
    *ptr = 42;
}

// Const fn
pub const fn square(x: u32) -> u32 {
    x * x
}

// Extern fn
pub extern "C" fn callback(value: i32) -> i32 {
    value * 2
}
