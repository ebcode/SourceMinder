// extern crate, extern blocks, FFI

extern crate alloc;
extern crate proc_macro as pm;

// FFI: foreign functions declared in extern block
extern "C" {
    pub fn malloc(size: usize) -> *mut u8;
    pub fn free(ptr: *mut u8);
    pub fn strlen(s: *const i8) -> usize;
    pub static errno: i32;
}

extern "system" {
    pub fn GetLastError() -> u32;
}

// Function exported with C ABI
#[no_mangle]
pub extern "C" fn rust_callback(value: i32) -> i32 {
    value.wrapping_mul(3)
}

// Const fn with extern - rare combo
pub const fn const_helper(n: u32) -> u32 {
    n + 1
}
