<?php

namespace App\Models;

/**
 * Class with static members
 */
class Database {
    private static ?Database $instance = null;
    public static int $queryCount = 0;

    private const HOST = 'localhost';
    public const PORT = 3306;
    protected const TIMEOUT = 30;

    private function __construct() {
        // Private constructor for singleton
    }

    public static function getInstance(): self {
        if (self::$instance === null) {
            self::$instance = new self();
        }
        self::$queryCount++;
        return self::$instance;
    }

    public static function resetQueryCount(): void {
        self::$queryCount = 0;
    }
}

// Usage examples
$db = Database::getInstance();
Database::resetQueryCount();
Database::$queryCount = 42;
