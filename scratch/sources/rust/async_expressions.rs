// async/await, ? operator, references, casts, async blocks

use std::future::Future;

pub async fn fetch_data(url: &str) -> Result<String, std::io::Error> {
    let body = download(url).await?;
    let parsed = parse_body(&body).await?;
    Ok(parsed)
}

async fn download(url: &str) -> Result<Vec<u8>, std::io::Error> {
    let _ = url;
    Ok(Vec::new())
}

async fn parse_body(bytes: &[u8]) -> Result<String, std::io::Error> {
    Ok(String::from_utf8_lossy(bytes).to_string())
}

pub fn make_future() -> impl Future<Output = u32> {
    async {
        let x = 1;
        let y = 2;
        x + y
    }
}

pub fn cast_and_ref() {
    let n: i64 = 1_000_000;
    let m = n as u32;
    let r: &i64 = &n;
    let mr: &mut u32 = &mut (m.clone() + 1);
    *mr += 10;
    let _ = (m, r);
}

pub fn try_chain(text: &str) -> Result<i32, std::num::ParseIntError> {
    let parsed = text.trim().parse::<i32>()?;
    let doubled = (parsed.checked_mul(2)).ok_or_else(|| {
        "overflow".parse::<i32>().unwrap_err()
    })?;
    Ok(doubled)
}

pub unsafe fn do_unsafe(ptr: *mut u8) {
    unsafe {
        *ptr = 42;
        let val = *ptr;
        let _ = val;
    }
}
