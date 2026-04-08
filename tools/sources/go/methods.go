package main

type Rectangle struct {
	Width  float64
	Height float64
}

// Value receiver method
func (r Rectangle) Area() float64 {
	return r.Width * r.Height
}

// Pointer receiver method
func (r *Rectangle) Scale(factor float64) {
	r.Width *= factor
	r.Height *= factor
}

type Counter struct {
	count int
}

// Pointer receiver for mutation
func (c *Counter) Increment() {
	c.count++
}

// Value receiver for read-only
func (c Counter) Value() int {
	return c.count
}

// Method with multiple parameters
func (r Rectangle) Resize(width, height float64) {
	r.Width = width
	r.Height = height
}
