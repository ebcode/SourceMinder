/* Stub for building with emcc (no backtrace support in wasm) */
#ifndef EXECINFO_STUB_H
#define EXECINFO_STUB_H
static inline int backtrace(void **buffer, int size) { return 0; }
static inline char **backtrace_symbols(void *const *buffer, int size) { return NULL; }
#endif
