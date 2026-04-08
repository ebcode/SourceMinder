// Test all preprocessor directives

// Simple includes
#include <stdio.h>
#include "myheader.h"

// Defines (already tested)
#define MAX_SIZE 100
#define MIN(a,b) ((a)<(b)?(a):(b))

// Conditional compilation
#ifdef DEBUG
    #define LOG(x) printf(x)
#else
    #define LOG(x)
#endif

#ifndef BUFFER_SIZE
    #define BUFFER_SIZE 1024
#endif

#if defined(USE_OPENGL)
    #define GRAPHICS_API "OpenGL"
#elif defined(USE_VULKAN)
    #define GRAPHICS_API "Vulkan"
#else
    #define GRAPHICS_API "Software"
#endif

// Undefine
#undef TEMP_MACRO

// Pragmas
#pragma once
#pragma GCC optimize("O3")

// Line directive
#line 100 "fake_file.c"

// Error and warning
#ifdef INVALID_CONFIG
    #error "Invalid configuration detected"
#endif

#ifdef DEPRECATED_FEATURE
    #warning "This feature is deprecated"
#endif

int main() {
    return 0;
}
