package main

import "fmt"

// Variadic function with int parameters
func sum(numbers ...int) int {
	total := 0
	for _, num := range numbers {
		total += num
	}
	return total
}

// Variadic with mixed parameters
func greet(prefix string, names ...string) {
	for _, name := range names {
		fmt.Println(prefix, name)
	}
}

// Variadic with interface{}
func printAll(items ...interface{}) {
	for _, item := range items {
		fmt.Println(item)
	}
}

// Using variadic arguments
func useVariadic() {
	// Call with multiple arguments
	result := sum(1, 2, 3, 4, 5)

	// Call with slice
	numbers := []int{10, 20, 30}
	result = sum(numbers...)

	// Mixed parameter variadic
	greet("Hello", "Alice", "Bob", "Charlie")

	// Interface variadic
	printAll(42, "hello", true, 3.14)

	_ = result
}

// Empty variadic call
func allowEmpty(vals ...string) int {
	return len(vals)
}
