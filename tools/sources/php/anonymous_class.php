<?php

namespace App\Models;

/**
 * Anonymous class examples
 */
class AnonymousExample {
    public function getLogger() {
        // Anonymous class implementing interface
        return new class {
            public function log(string $message): void {
                echo $message . "\n";
            }

            public function error(string $message): void {
                echo "ERROR: $message\n";
            }
        };
    }

    public function getCounter(int $start = 0) {
        // Anonymous class with constructor
        return new class($start) {
            private int $count;

            public function __construct(int $initial) {
                $this->count = $initial;
            }

            public function increment(): int {
                return ++$this->count;
            }

            public function get(): int {
                return $this->count;
            }
        };
    }
}

// Using anonymous class directly
$handler = new class {
    public function handle(string $event): void {
        echo "Handling: $event\n";
    }
};
