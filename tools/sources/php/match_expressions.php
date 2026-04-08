<?php

namespace App\Models;

/**
 * Match expression examples (PHP 8.0+)
 */
class StatusHandler {
    public function getStatusMessage(string $status): string {
        return match($status) {
            'active' => 'User is active',
            'inactive' => 'User is inactive',
            'pending' => 'Awaiting approval',
            default => 'Unknown status',
        };
    }

    public function getStatusCode(string $status): int {
        return match($status) {
            'active', 'verified' => 200,
            'pending', 'reviewing' => 202,
            'inactive' => 404,
            'banned', 'suspended' => 403,
            default => 500,
        };
    }
}
