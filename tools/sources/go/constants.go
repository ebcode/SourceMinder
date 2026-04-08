package main

// Simple constants
const Pi = 3.14159
const AppName = "MyApp"

// Grouped constants
const (
	StatusOK       = 200
	StatusNotFound = 404
	StatusError    = 500
)

// Typed constants
const (
	MaxUsers    int     = 1000
	Timeout     float64 = 30.5
	DefaultName string  = "Guest"
)

// iota for enumeration
const (
	Sunday = iota
	Monday
	Tuesday
	Wednesday
	Thursday
	Friday
	Saturday
)

// iota with expressions
const (
	_  = iota             // Skip zero
	KB = 1 << (10 * iota) // 1 << 10
	MB                    // 1 << 20
	GB                    // 1 << 30
	TB                    // 1 << 40
)

// iota with custom values
const (
	ReadPermission    = 1 << iota // 1
	WritePermission               // 2
	ExecutePermission             // 4
)

// Multiple constants in one line
const Width, Height = 1920, 1080
