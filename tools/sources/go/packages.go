package main

import (
	"fmt"
	"io"
	"net/http"
	"os"
)

// Import with alias
import (
	stdlog "log"
	mrand "math/rand"
)

// Blank import for side effects
import _ "database/sql"

// Dot import (imports into current namespace)
import . "math"

func useImports() {
	// Standard library usage
	fmt.Println("Hello")

	// Aliased import
	stdlog.Println("Logging")
	mrand.Intn(100)

	// Multiple imports from same package
	file, err := os.Open("file.txt")
	if err != nil {
		return
	}
	defer file.Close()

	// Interface from import
	var r io.Reader = file

	// HTTP client
	resp, err := http.Get("http://example.com")
	_ = resp
	_ = r
}
