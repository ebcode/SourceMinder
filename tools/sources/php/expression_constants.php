<?php

namespace App\Test;

class Factory {
    public const CONFIG = 'production';

    public static function getClass(): string {
        return self::class;
    }

    public static function getInstance(): self {
        return new self();
    }
}

class ConfigManager {
    public $factory;

    public function __construct() {
        $this->factory = new Factory();
    }
}

// Test 1: Function call result as scope
function getClass(): string {
    return Factory::class;
}

$config1 = getClass()::CONFIG;

// Test 2: Method call on object as scope
$manager = new ConfigManager();
$config2 = $manager->factory->getInstance()::CONFIG;

// Test 3: Static method call result as scope
$config3 = Factory::getInstance()::CONFIG;

// Test 4: Property access as scope
$obj = new ConfigManager();
$config4 = $obj->factory::CONFIG;

// Test 5: Parenthesized expression as scope
$className = Factory::class;
$config5 = ($className)::CONFIG;

// Test 6: Chained method calls
$config6 = Factory::getInstance()::getInstance()::CONFIG;

// Test 7: Variable as scope (already supported, but included for completeness)
$factoryClass = Factory::class;
$config7 = $factoryClass::CONFIG;
