<?php

namespace App\Models;

/**
 * Function with many parameters
 */
function createUser(
    string $name,
    string $email,
    int $age = 0,
    bool $active = true,
    ?string $role = null
): array {
    return compact('name', 'email', 'age', 'active', 'role');
}

/**
 * Named arguments usage (PHP 8.0+)
 */
class UserFactory {
    public function create(): array {
        // Call with named arguments in any order
        return createUser(
            email: 'test@example.com',
            name: 'John Doe',
            role: 'admin',
            age: 30
        );
    }

    public function createMinimal(): array {
        // Skip optional parameters
        return createUser(
            name: 'Jane Doe',
            email: 'jane@example.com'
        );
    }
}
