package main

// Generic function with type parameter
func Max[T int | float64](a, b T) T {
	if a > b {
		return a
	}
	return b
}

// Generic type constraint
type Number interface {
	int | int64 | float64
}

// Generic function with constraint
func Sum[T Number](values []T) T {
	var total T
	for _, v := range values {
		total += v
	}
	return total
}

// Generic struct
type Stack[T any] struct {
	items []T
}

func (s *Stack[T]) Push(item T) {
	s.items = append(s.items, item)
}

func (s *Stack[T]) Pop() T {
	if len(s.items) == 0 {
		var zero T
		return zero
	}
	item := s.items[len(s.items)-1]
	s.items = s.items[:len(s.items)-1]
	return item
}

// Generic interface
type Container[T any] interface {
	Add(T)
	Get() T
}

// Multiple type parameters
func Pair[K comparable, V any](key K, value V) map[K]V {
	return map[K]V{key: value}
}

// Comparable constraint
func Contains[T comparable](slice []T, value T) bool {
	for _, v := range slice {
		if v == value {
			return true
		}
	}
	return false
}
