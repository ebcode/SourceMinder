package main

import "fmt"

func anonymousFunctions() {
	// Anonymous function assigned to variable
	add := func(a, b int) int {
		return a + b
	}
	result := add(5, 3)

	// Immediately invoked function
	message := func(name string) string {
		return "Hello, " + name
	}("Alice")

	// Anonymous function with closure
	counter := 0
	increment := func() int {
		counter++
		return counter
	}
	increment()
	increment()

	// Function returning function
	multiplier := func(factor int) func(int) int {
		return func(x int) int {
			return x * factor
		}
	}
	double := multiplier(2)
	triple := multiplier(3)

	_, _, _, _ = result, message, double, triple
}

// Higher-order function
func applyOperation(x, y int, op func(int, int) int) int {
	return op(x, y)
}

func useHigherOrder() {
	// Pass anonymous function as argument
	sum := applyOperation(10, 5, func(a, b int) int {
		return a + b
	})

	product := applyOperation(10, 5, func(a, b int) int {
		return a * b
	})

	_, _ = sum, product
}

// Closure capturing variables
func makeCounter(start int) func() int {
	count := start
	return func() int {
		count++
		return count
	}
}

func closureExample() {
	c1 := makeCounter(0)
	c2 := makeCounter(100)

	fmt.Println(c1()) // 1
	fmt.Println(c1()) // 2
	fmt.Println(c2()) // 101
}
