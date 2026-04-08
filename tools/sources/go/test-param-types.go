package main

// Test Go parameter types

type FileFilter struct {
	count int
}

func simpleType(x int) {}

func customType(filter FileFilter) {}

func pointerType(filter *FileFilter) {}

func sliceType(items []string) {}
