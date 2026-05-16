// Loop labels, break-with-value, complex loop expressions

pub fn nested_break() -> i32 {
    let result = 'outer: loop {
        let mut count = 0;
        'inner: loop {
            count += 1;
            if count == 5 {
                break 'outer 42;
            }
            if count > 10 {
                break 'inner;
            }
        }
    };
    result
}

pub fn loop_returns_value() -> u32 {
    let mut x = 0;
    let final_value = loop {
        x += 1;
        if x > 100 {
            break x * 2;
        }
    };
    final_value
}

pub fn labeled_for() {
    'rows: for row in 0..10 {
        for col in 0..10 {
            if row == col {
                continue 'rows;
            }
            println!("{},{}", row, col);
        }
    }
}

pub fn block_as_expression() -> i32 {
    let y = {
        let a = 1;
        let b = 2;
        a + b
    };
    y
}
