<?php

namespace App\Models;

class User {
    public $name;
    private $password;

    public function __construct($name) {
        $this->name = $name;
    }

    public function save() {
        // Save user to database
        return true;
    }
}
