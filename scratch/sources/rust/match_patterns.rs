// match expressions, if-let, while-let, for loops, destructuring patterns

pub enum Shape {
    Circle(f64),
    Square(f64),
    Rect { width: f64, height: f64 },
}

pub fn area(shape: &Shape) -> f64 {
    match shape {
        Shape::Circle(radius) => 3.14159 * radius * radius,
        Shape::Square(side) => side * side,
        Shape::Rect { width, height } => width * height,
    }
}

pub fn classify(x: i32) -> &'static str {
    match x {
        0 => "zero",
        n if n < 0 => "negative",
        1..=9 => "single digit",
        10 | 100 | 1000 => "round",
        _ => "other",
    }
}

pub fn destructure_tuple(pair: (i32, i32, i32)) -> i32 {
    let (first, _, third) = pair;
    first + third
}

pub fn if_let_demo(maybe: Option<i32>) {
    if let Some(value) = maybe {
        println!("got {}", value);
    } else if let None = maybe {
        println!("nothing");
    }
}

pub fn while_let_demo() {
    let mut queue = vec![1, 2, 3];
    while let Some(item) = queue.pop() {
        println!("{}", item);
    }
}

pub fn for_loop_demo(items: &[(String, u32)]) {
    for (name, count) in items {
        println!("{}: {}", name, count);
    }

    for idx in 0..10 {
        println!("{}", idx);
    }
}

pub fn nested_pattern(opt: Option<Result<i32, String>>) -> i32 {
    match opt {
        Some(Ok(v)) => v,
        Some(Err(_msg)) => -1,
        None => 0,
    }
}
