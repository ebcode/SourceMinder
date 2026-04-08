<?php

namespace App\Models;

/**
 * Basic class definition
 */
class Product {
    public int $id;
    public string $name;
    public float $price;

    public function __construct(int $id, string $name, float $price) {
        $this->id = $id;
        $this->name = $name;
        $this->price = $price;
    }

    public function getTotal(int $quantity): float {
        return $this->price * $quantity;
    }

    public function updatePrice(float $newPrice): void {
        $this->price = $newPrice;
    }
}

// Instantiate and use the class
$product = new Product(1, "Widget", 19.99);
$total = $product->getTotal(5);
$product->updatePrice(24.99);
