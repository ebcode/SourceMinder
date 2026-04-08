package main

import "fmt"

func typeAssertions() {
	var i interface{} = "hello"

	// Type assertion
	s := i.(string)
	fmt.Println(s)

	// Type assertion with ok check
	str, ok := i.(string)
	if ok {
		fmt.Println(str)
	}

	// Type assertion that would panic (if not checked)
	// n := i.(int) // This would panic

	// Safe type assertion
	num, ok := i.(int)
	if !ok {
		fmt.Println("Not an int")
	}
	_ = num
}

func typeSwitch(i interface{}) {
	// Type switch
	switch v := i.(type) {
	case int:
		fmt.Println("Integer:", v)
	case string:
		fmt.Println("String:", v)
	case bool:
		fmt.Println("Boolean:", v)
	case []int:
		fmt.Println("Int slice:", v)
	default:
		fmt.Println("Unknown type")
	}
}

func processValue(val interface{}) string {
	switch v := val.(type) {
	case string:
		return v
	case int:
		return fmt.Sprintf("%d", v)
	case float64:
		return fmt.Sprintf("%.2f", v)
	default:
		return "unknown"
	}
}
