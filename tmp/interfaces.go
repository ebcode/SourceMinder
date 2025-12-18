package main

import "io"

// Interface with multiple methods
type Reader interface {
	Read(p []byte) (n int, err error)
	Close() error
}

// Interface with embedded interface
type ReadCloser interface {
	io.Reader
	Close() error
}

// Empty interface
type Any interface{}

// Interface with single method
type Stringer interface {
	String() string
}
