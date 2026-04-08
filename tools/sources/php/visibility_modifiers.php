<?php

namespace App\Models;

/**
 * Class demonstrating visibility modifiers
 */
class Account {
    public string $username;
    protected string $email;
    private string $passwordHash;

    public function getUsername(): string {
        return $this->username;
    }

    protected function validateEmail(string $email): bool {
        return filter_var($email, FILTER_VALIDATE_EMAIL) !== false;
    }

    private function hashPassword(string $password): string {
        return password_hash($password, PASSWORD_DEFAULT);
    }
}
