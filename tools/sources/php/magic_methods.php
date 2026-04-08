<?php

namespace App\Models;

/**
 * Class demonstrating magic methods
 */
class MagicContainer {
    private array $data = [];
    private array $methods = [];

    public function __construct(array $data = []) {
        $this->data = $data;
    }

    public function __get(string $name) {
        return $this->data[$name] ?? null;
    }

    public function __set(string $name, $value): void {
        $this->data[$name] = $value;
    }

    public function __isset(string $name): bool {
        return isset($this->data[$name]);
    }

    public function __unset(string $name): void {
        unset($this->data[$name]);
    }

    public function __call(string $name, array $arguments) {
        return "Called method: $name";
    }

    public static function __callStatic(string $name, array $arguments) {
        return "Called static method: $name";
    }

    public function __toString(): string {
        return json_encode($this->data);
    }

    public function __invoke(string $arg): string {
        return "Invoked with: $arg";
    }

    public function __clone() {
        $this->data = array_merge([], $this->data);
    }

    public function __destruct() {
        // Cleanup
    }

    public function __sleep(): array {
        return ['data'];
    }

    public function __wakeup(): void {
        // Restore state
    }

    public function __debugInfo(): array {
        return ['count' => count($this->data)];
    }
}
