<?php

namespace App\Models;

/**
 * Interface definitions
 */
interface Storable {
    public function save(): bool;
}

interface Cacheable {
    public function cache(): void;
}

interface Loggable {
    public function log(string $message): void;
}

/**
 * Class implementing single interface
 */
class Document implements Storable {
    public function save(): bool {
        return true;
    }
}

/**
 * Class implementing multiple interfaces
 */
class CachedDocument implements Storable, Cacheable, Loggable {
    public function save(): bool {
        return true;
    }

    public function cache(): void {
    }

    public function log(string $message): void {
    }
}
