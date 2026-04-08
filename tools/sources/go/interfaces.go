package main

// Reader interface with single method
type Reader interface {
	Read(p []byte) (n int, err error)
}

// Writer interface
type Writer interface {
	Write(p []byte) (n int, err error)
}

// ReadWriter combines multiple interfaces
type ReadWriter interface {
	Reader
	Writer
}

// Stringer is a common interface pattern
type Stringer interface {
	String() string
}

// Storage interface with multiple methods
type Storage interface {
	Save(key string, value interface{}) error
	Load(key string) (interface{}, error)
	Delete(key string) error
}
