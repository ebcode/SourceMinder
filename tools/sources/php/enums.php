<?php

namespace App\Models;

/**
 * Backed enum with string values
 */
enum UserStatus: string {
    case Active = 'active';
    case Inactive = 'inactive';
    case Suspended = 'suspended';
    case Pending = 'pending';
}

/**
 * Pure enum without backing values
 */
enum Priority {
    case Low;
    case Medium;
    case High;
    case Critical;
}

/**
 * Enum with methods
 */
enum Color: string {
    case Red = 'red';
    case Green = 'green';
    case Blue = 'blue';

    public function getHex(): string {
        return match($this) {
            Color::Red => '#FF0000',
            Color::Green => '#00FF00',
            Color::Blue => '#0000FF',
        };
    }
}
