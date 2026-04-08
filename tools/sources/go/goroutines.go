package main

import "fmt"

func worker(id int, jobs <-chan int, results chan<- int) {
	for job := range jobs {
		results <- job * 2
	}
}

func processData(data []int) {
	// Launch goroutine with anonymous function
	go func() {
		for _, v := range data {
			fmt.Println(v)
		}
	}()
}

func fetchUser(id int) {
	// Simple goroutine launch
	go fetchFromDatabase(id)
}

func fetchFromDatabase(id int) {
	// Simulated async operation
}

func main() {
	// Goroutine with named function
	go worker(1, nil, nil)

	// Goroutine with closure
	done := make(chan bool)
	go func() {
		done <- true
	}()
}
