package main

import "fmt"

func openFile(filename string) {
	// Defer executes at end of function
	defer fmt.Println("File closed")

	fmt.Println("File opened")
}

func multipleDefers() {
	// Defers execute in LIFO order
	defer fmt.Println("First")
	defer fmt.Println("Second")
	defer fmt.Println("Third")
}

func deferWithPanic() {
	defer func() {
		if r := recover(); r != nil {
			fmt.Println("Recovered:", r)
		}
	}()

	panic("something went wrong")
}

func cleanup() {
	resource := acquireResource()
	defer releaseResource(resource)

	// Use resource
	processResource(resource)
}

func acquireResource() int {
	return 42
}

func releaseResource(r int) {
	fmt.Println("Released:", r)
}

func processResource(r int) {
	// Processing
}
