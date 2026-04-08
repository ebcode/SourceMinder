package main

func channelOperations() {
	// Unbuffered channel
	messages := make(chan string)

	// Buffered channel
	buffered := make(chan int, 100)

	// Send operation
	messages <- "hello"

	// Receive operation
	msg := <-messages

	// Receive with ok check
	value, ok := <-messages

	// Close channel
	close(messages)

	// Range over channel
	for item := range buffered {
		_ = item
	}
}

// Channel as function parameter
func sendData(ch chan<- int) {
	ch <- 42
}

// Receive-only channel parameter
func receiveData(ch <-chan string) string {
	return <-ch
}

// Bidirectional channel
func processChannel(ch chan int) {
	ch <- 10
	val := <-ch
	_ = val
}
