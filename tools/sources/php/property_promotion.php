<?php

namespace App\Models;

/**
 * Constructor property promotion (PHP 8.0+)
 */
class Customer {
    public function __construct(
        public int $id,
        public string $name,
        public string $email,
        private string $passwordHash,
        protected ?string $phone = null
    ) {
        // Properties automatically created and assigned
    }

    public function updateEmail(string $newEmail): void {
        $this->email = $newEmail;
    }
}

/**
 * Mixed promoted and traditional properties
 */
class Order {
    private array $items = [];

    public function __construct(
        public int $orderId,
        public Customer $customer,
        public float $total
    ) {
        // Some properties promoted, others declared traditionally
    }
}
