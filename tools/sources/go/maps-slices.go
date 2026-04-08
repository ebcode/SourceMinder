package main

func sliceOperations() {
	// Slice declaration
	var numbers []int

	// Slice literal
	names := []string{"Alice", "Bob", "Charlie"}

	// Make slice with length and capacity
	data := make([]int, 5, 10)

	// Append to slice
	numbers = append(numbers, 1, 2, 3)

	// Slice of slice
	subset := names[0:2]

	// Range over slice
	for i, name := range names {
		_ = i
		_ = name
	}

	// Multi-dimensional slice
	matrix := [][]int{
		{1, 2, 3},
		{4, 5, 6},
	}

	_, _, _ = data, subset, matrix
}

func mapOperations() {
	// Map declaration
	var users map[string]int

	// Map literal
	ages := map[string]int{
		"Alice": 30,
		"Bob":   25,
	}

	// Make map
	scores := make(map[string]float64)

	// Add/update entry
	scores["test"] = 95.5

	// Get value
	age := ages["Alice"]

	// Get with ok check
	value, exists := ages["Charlie"]

	// Delete entry
	delete(ages, "Bob")

	// Range over map
	for key, val := range scores {
		_ = key
		_ = val
	}

	_, _, _ = users, age, value
	_ = exists
}
