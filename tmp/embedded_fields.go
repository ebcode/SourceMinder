package main

import (
	"io"
	"sync"
)

// Struct with embedded fields
type MyStruct struct {
	io.Reader          // embedded interface
	sync.Mutex         // embedded struct
	*sync.RWMutex      // embedded pointer
	Name        string // regular field
	age         int    // regular private field
}

// Struct with only embedded fields
type Composite struct {
	io.Reader
	io.Writer
	io.Closer
}

// Struct with qualified embedded field
type Wrapper struct {
	sync.Mutex
	Value int
}
