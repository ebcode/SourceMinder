// Macro definition
macro_rules! debug_print {
    ($val:expr) => {
        println!("DEBUG: {} = {:?}", stringify!($val), $val);
    };
    ($fmt:expr, $($args:tt)*) => {
        println!(concat!("DEBUG: ", $fmt), $($args)*);
    };
}

pub fn closures_demo() {
    // Simple closure
    let add_one = |x: i32| x + 1;
    let result = add_one(5);

    // Closure with multiple args, no annotations
    let multiply = |a, b| a * b;
    let product = multiply(3, 4);

    // Move closure
    let captured = String::from("hello");
    let printer = move || println!("{}", captured);
    printer();

    // Closure passed to higher-order function
    let nums = vec![1, 2, 3, 4, 5];
    let doubled: Vec<i32> = nums.iter().map(|n| n * 2).collect();
    let total: i32 = nums.iter().filter(|&&n| n > 2).sum();

    debug_print!(result);
    debug_print!("product = {}", product);
}
