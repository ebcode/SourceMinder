package main

type Person struct {
	Name string
	Age  int
}

func pointerOperations() {
	// Pointer declaration
	var ptr *int

	// Address-of operator
	value := 42
	ptr = &value

	// Dereference operator
	dereferenced := *ptr

	// Pointer to struct
	person := &Person{Name: "Alice", Age: 30}

	// Access fields through pointer (automatic dereferencing)
	name := person.Name
	age := person.Age

	_ = dereferenced
	_ = name
	_ = age
}

// Function with pointer parameter
func updatePerson(p *Person) {
	p.Name = "Bob"
	p.Age = 25
}

// Function returning pointer
func createPerson(name string) *Person {
	return &Person{Name: name, Age: 0}
}

// Nil pointer check
func safeDereference(p *Person) string {
	if p != nil {
		return p.Name
	}
	return ""
}
