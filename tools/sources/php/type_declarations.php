<?php

namespace App\Models;

/**
 * Class demonstrating type declarations
 */
class TypeExample {
    // Nullable types
    private ?string $optionalName = null;
    private ?int $optionalId = null;

    // Union types (PHP 8.0+)
    private int|float $number;
    private string|array $data;

    // Mixed type
    private mixed $anything;

    public function processValue(string|int|null $value): bool {
        return $value !== null;
    }

    public function getValue(): string|int|float {
        return $this->number;
    }

    public function handleData(mixed $input): void {
        $this->anything = $input;
    }

    // Intersection types (PHP 8.1+) - requires interfaces
    public function process(Countable&Traversable $collection): void {}
}

/**
 * Function with complex type declarations
 */
/*
function transformData(
    int|string $input,
    ?callable $transformer = null,
    bool|array $options = false
): string|array|null {
    return null;
}
*/
