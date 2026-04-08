<?php

namespace App\Models;

/**
 * Regular function
 */
function calculateDiscount(float $price, float $percentage): float {
    return $price * ($percentage / 100);
}

/**
 * Function with optional parameters
 */
function formatName(string $first, string $last, ?string $middle = null): string {
    return $middle ? "$first $middle $last" : "$first $last";
}

/**
 * Arrow function (PHP 7.4+)
 */
$square = fn(int $n): int => $n * $n;

/**
 * Anonymous function / closure
 */
$greet = function(string $name): string {
    return "Hello, $name!";
};

/**
 * Closure with use clause
 */
$multiplier = 10;
$multiply = function(int $value) use ($multiplier): int {
    return $value * $multiplier;
};
