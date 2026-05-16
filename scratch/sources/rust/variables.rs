pub fn variables_demo() {
    // Simple let bindings
    let x = 5;
    let y: i32 = 10;
    let mut counter = 0;

    // Tuple destructuring
    let (a, b, c) = (1, 2, 3);

    // Struct destructuring
    let Point { x: px, y: py } = Point { x: 1.0, y: 2.0 };

    // Type-annotated with explicit value
    let name: String = String::from("alice");

    // Const inside function
    const PI: f64 = 3.14159;

    // Reference binding
    let r = &x;
    let mut_ref = &mut counter;
    *mut_ref += 1;

    // If-let pattern
    let some_value = Some(42);
    if let Some(v) = some_value {
        println!("{}", v);
    }

    // While-let
    let mut stack = vec![1, 2, 3];
    while let Some(top) = stack.pop() {
        println!("{}", top);
    }
}

struct Point { x: f64, y: f64 }
