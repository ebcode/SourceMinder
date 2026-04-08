<?php

namespace App\Models;

/**
 * Abstract class with abstract methods
 */
abstract class BaseRepository {
    protected string $table;

    abstract public function find(int $id): ?object;
    abstract protected function validate(array $data): bool;

    public function getTable(): string {
        return $this->table;
    }
}

/**
 * Concrete implementation of abstract class
 */
class ProductRepository extends BaseRepository {
    protected string $table = 'products';

    public function find(int $id): ?object {
        return null;
    }

    protected function validate(array $data): bool {
        return isset($data['name']) && isset($data['price']);
    }
}

/**
 * Final class (cannot be extended)
 */
final class ImmutableConfig {
    public function __construct(
        private readonly array $settings
    ) {}

    final public function get(string $key): mixed {
        return $this->settings[$key] ?? null;
    }
}
