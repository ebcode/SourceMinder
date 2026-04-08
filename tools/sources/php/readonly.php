<?php

namespace App\Models;

/**
 * Readonly properties (PHP 8.1+)
 */
class Configuration {
    public function __construct(
        public readonly string $appName,
        public readonly string $environment,
        public readonly int $maxConnections
    ) {}
}

/**
 * Readonly class (PHP 8.2+)
 */
readonly class Point {
    public function __construct(
        public int $x,
        public int $y
    ) {}

    public function distanceFrom(Point $other): float {
        $dx = $this->x - $other->x;
        $dy = $this->y - $other->y;
        return sqrt($dx * $dx + $dy * $dy);
    }
}
