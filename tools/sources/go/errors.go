package main

import (
	"errors"
	"fmt"
)

// Custom error type
type ValidationError struct {
	Field   string
	Message string
}

func (e *ValidationError) Error() string {
	return fmt.Sprintf("%s: %s", e.Field, e.Message)
}

// Function returning error
func divide(a, b float64) (float64, error) {
	if b == 0 {
		return 0, errors.New("division by zero")
	}
	return a / b, nil
}

// Custom error with wrapping
func processFile(filename string) error {
	err := openFile(filename)
	if err != nil {
		return fmt.Errorf("failed to process file: %w", err)
	}
	return nil
}

func openFile(name string) error {
	return errors.New("file not found")
}

// Error checking patterns
func handleErrors() {
	// Basic error check
	result, err := divide(10, 0)
	if err != nil {
		fmt.Println("Error:", err)
		return
	}

	// Error with custom type
	err = &ValidationError{
		Field:   "email",
		Message: "invalid format",
	}

	// Type assertion for custom error
	if ve, ok := err.(*ValidationError); ok {
		fmt.Println(ve.Field, ve.Message)
	}

	_ = result
}
