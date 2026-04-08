package main

import "fmt"

// Base struct
type Engine struct {
	Horsepower int
	Type       string
}

func (e Engine) Start() {
	fmt.Println("Engine started")
}

// Embedding Engine in Car
type Car struct {
	Engine // Embedded field
	Brand  string
	Model  string
}

// Multiple embedding
type Logger struct {
	Prefix string
}

func (l Logger) Log(msg string) {
	fmt.Println(l.Prefix, msg)
}

type Database struct {
	Logger // Embedded Logger
	Name   string
}

// Interface embedding
type Reader interface {
	Read() string
}

type Writer interface {
	Write(data string)
}

type ReadWriter interface {
	Reader // Embedded interface
	Writer // Embedded interface
}

// Anonymous field embedding
type Point struct {
	X, Y int
}

type Circle struct {
	Point  // Anonymous embedded field
	Radius int
}
