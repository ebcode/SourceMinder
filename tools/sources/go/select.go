package main

import "time"

func selectOperations() {
	ch1 := make(chan string)
	ch2 := make(chan string)

	// Basic select
	select {
	case msg1 := <-ch1:
		println(msg1)
	case msg2 := <-ch2:
		println(msg2)
	}

	// Select with send
	select {
	case ch1 <- "hello":
		println("sent to ch1")
	case ch2 <- "world":
		println("sent to ch2")
	}

	// Select with default
	select {
	case msg := <-ch1:
		println(msg)
	default:
		println("no message")
	}

	// Select with timeout
	select {
	case msg := <-ch1:
		println(msg)
	case <-time.After(5 * time.Second):
		println("timeout")
	}
}

func fanIn(ch1, ch2 <-chan string) <-chan string {
	out := make(chan string)
	go func() {
		for {
			select {
			case msg := <-ch1:
				out <- msg
			case msg := <-ch2:
				out <- msg
			}
		}
	}()
	return out
}

func nonBlockingReceive(ch <-chan int) {
	select {
	case val := <-ch:
		println(val)
	default:
		println("channel empty")
	}
}
