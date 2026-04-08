package main

// User represents a basic struct with various field types
type User struct {
	ID       int
	Name     string
	Email    string
	Age      int
	IsActive bool
}

// Point represents a 2D coordinate
type Point struct {
	X float64
	Y float64
}

// Config with nested struct
type Config struct {
	Server   ServerConfig
	Database string
	Port     int
}

type ServerConfig struct {
	Host    string
	Timeout int
}
