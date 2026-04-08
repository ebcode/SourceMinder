package main

import (
	"fmt"
	"strings"
)

// User represents a system user
type User struct {
	ID       int
	Username string
	Email    string
}

// UserManager handles user operations
type UserManager struct {
	users map[int]*User
}

// NewUserManager creates a new user manager
func NewUserManager() *UserManager {
	return &UserManager{
		users: make(map[int]*User),
	}
}

// AddUser adds a new user to the system
func (um *UserManager) AddUser(id int, username, email string) error {
	if _, exists := um.users[id]; exists {
		return fmt.Errorf("user with ID %d already exists", id)
	}

	um.users[id] = &User{
		ID:       id,
		Username: username,
		Email:    email,
	}
	return nil
}

// GetUser retrieves a user by ID
func (um *UserManager) GetUser(id int) (*User, error) {
	user, exists := um.users[id]
	if !exists {
		return nil, fmt.Errorf("user not found")
	}
	return user, nil
}

// Helper function to validate email
func validateEmail(email string) bool {
	return strings.Contains(email, "@")
}

const (
	MaxUsers = 1000
	MinAge   = 18
)

var defaultManager *UserManager

func main() {
	defaultManager = NewUserManager()
	err := defaultManager.AddUser(1, "alice", "alice@example.com")
	if err != nil {
		fmt.Println("Error:", err)
		return
	}

	user, _ := defaultManager.GetUser(1)
	fmt.Printf("User: %s (%s)\n", user.Username, user.Email)
}
