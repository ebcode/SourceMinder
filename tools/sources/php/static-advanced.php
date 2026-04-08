<?php

namespace App\Models;

class Base {
    protected static $config = [];
    protected const VERSION = '1.0';
    public static function init() { return new static(); }
}

class Child extends Base {
    public static function setup() {
        parent::init();
        parent::$config = ['key' => 'value'];
        echo parent::VERSION;
        static::configure();
    }

    private static function configure() {
        static::$config['active'] = true;
    }

    public function run() { return $this; }
}

// Namespaced static access
\App\Models\Child::setup();
\App\Models\Base::init();

// Variable class names
$class = 'Child';
$class::setup();
$model = new Base();
$model::init();

// Chained static to instance method
Child::init()->run();

// Test chained static property access (if valid PHP)
// $obj1::$obj2::$prop;
