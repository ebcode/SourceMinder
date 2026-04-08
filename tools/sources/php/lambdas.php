<?php
// PHP anonymous functions (closures) and arrow functions

// Basic anonymous function
$square = function($x) {
    return $x ** 2;
};

// Multi-parameter anonymous function
$add = function($a, $b) {
    return $a + $b;
};

$multiply = function($x, $y, $z) {
    return $x * $y * $z;
};

// Arrow function (PHP 7.4+)
$double = fn($x) => $x * 2;
$triple = fn($x) => $x * 3;

// Arrow function with multiple parameters
$sum = fn($a, $b) => $a + $b;

// Anonymous function in array_map
$numbers = [1, 2, 3, 4, 5];
$squared = array_map(function($x) {
    return $x ** 2;
}, $numbers);

// Arrow function in array_map
$doubled = array_map(fn($x) => $x * 2, $numbers);

// Anonymous function in array_filter
$evens = array_filter($numbers, function($x) {
    return $x % 2 === 0;
});

// Arrow function in array_filter
$odds = array_filter($numbers, fn($x) => $x % 2 !== 0);

// Anonymous function in array_reduce
$total = array_reduce($numbers, function($acc, $x) {
    return $acc + $x;
}, 0);

// Arrow function in array_reduce
$product = array_reduce($numbers, fn($acc, $x) => $acc * $x, 1);

// Closure with 'use' clause (capturing variables)
$multiplier = 10;
$multiplyByTen = function($x) use ($multiplier) {
    return $x * $multiplier;
};

// Closure capturing by reference
$counter = 0;
$increment = function() use (&$counter) {
    $counter++;
    return $counter;
};
$increment();
$increment();

// Anonymous function returning anonymous function
function makeMultiplier($factor) {
    return function($x) use ($factor) {
        return $x * $factor;
    };
}

$timesTwo = makeMultiplier(2);
$timesFive = makeMultiplier(5);

// Arrow function in usort
$students = [
    ['name' => 'Alice', 'grade' => 85],
    ['name' => 'Bob', 'grade' => 92],
    ['name' => 'Charlie', 'grade' => 85]
];
usort($students, fn($a, $b) => $a['grade'] <=> $b['grade']);

// Anonymous function in usort
usort($students, function($a, $b) {
    return strcmp($a['name'], $b['name']);
});

// Closure in object
$operations = [
    'add' => fn($x, $y) => $x + $y,
    'sub' => fn($x, $y) => $x - $y,
    'mul' => fn($x, $y) => $x * $y,
    'div' => fn($x, $y) => $y !== 0 ? $x / $y : null
];

// Immediately invoked anonymous function
$result = (function($x) {
    return $x * 2;
})(5);

// Immediately invoked arrow function
$arrowResult = (fn($x) => $x * 3)(10);

// Anonymous function with type declarations
$typedFunc = function(int $x, int $y): int {
    return $x + $y;
};

// Arrow function with type declaration
$typedArrow = fn(string $name): string => "Hello, $name!";

// Closure capturing multiple variables
$prefix = 'Mr. ';
$suffix = ' Esq.';
$formatName = function($name) use ($prefix, $suffix) {
    return $prefix . $name . $suffix;
};

// Nested closures
$outer = function($x) {
    return function($y) use ($x) {
        return $x + $y;
    };
};
$partial = $outer(10);
$finalResult = $partial(5);

// Anonymous function in array_walk
$items = [1, 2, 3];
array_walk($items, function(&$value, $key) {
    $value = $value ** 2;
});

// Arrow function cannot modify by reference (use anonymous function)
$mutable = [1, 2, 3];
array_walk($mutable, function(&$val) {
    $val *= 2;
});

// Closure as callback
$callback = function($message) {
    echo "Callback: $message\n";
};

// Anonymous function with default parameter
$greet = function($name = 'World') {
    return "Hello, $name!";
};

// Arrow function conditional
$abs = fn($x) => $x >= 0 ? $x : -$x;

// Anonymous function in class
class Calculator {
    private $multiplier = 2;

    public function getMultiplier() {
        // Return closure that uses $this
        return function($x) {
            return $x * $this->multiplier;
        };
    }

    public function getArrowMultiplier() {
        // Arrow functions automatically capture $this
        return fn($x) => $x * $this->multiplier;
    }
}

$calc = new Calculator();
$multiplyFunc = $calc->getMultiplier();
$arrowMultiplyFunc = $calc->getArrowMultiplier();

// Closure with multiple use variables
$a = 10;
$b = 20;
$c = 30;
$complexClosure = function($x) use ($a, $b, $c) {
    return $x + $a + $b + $c;
};

// Anonymous function in array_map with keys
$keyedArray = ['a' => 1, 'b' => 2, 'c' => 3];
$mapped = array_map(function($value, $key) {
    return "$key: $value";
}, $keyedArray, array_keys($keyedArray));

// Closure returning arrow function
$makeAdder = function($x) {
    return fn($y) => $x + $y;
};
$addTen = $makeAdder(10);

// Anonymous function with variadic parameters
$sumAll = function(...$numbers) {
    return array_sum($numbers);
};

// Arrow function cannot use variadic params with complex logic
// (arrow functions are single expression only)
$variadicArrow = fn(...$args) => array_sum($args);

// Anonymous function in uasort (maintains keys)
$assocArray = ['a' => 3, 'b' => 1, 'c' => 2];
uasort($assocArray, fn($a, $b) => $a <=> $b);

// Closure as object property
class EventHandler {
    private $handlers = [];

    public function on($event, $handler) {
        $this->handlers[$event] = $handler;
    }

    public function trigger($event, $data) {
        if (isset($this->handlers[$event])) {
            return $this->handlers[$event]($data);
        }
    }
}

$handler = new EventHandler();
$handler->on('save', fn($data) => "Saving: $data");
$handler->on('delete', function($data) {
    return "Deleting: $data";
});

// Recursive anonymous function (requires use of variable by reference)
$factorial = function($n) use (&$factorial) {
    if ($n <= 1) return 1;
    return $n * $factorial($n - 1);
};

// Array of closures
$filters = [
    fn($x) => $x > 0,
    fn($x) => $x % 2 === 0,
    fn($x) => $x < 100
];

// Apply multiple filters using closures
$applyFilters = function($value, $filters) {
    foreach ($filters as $filter) {
        if (!$filter($value)) {
            return false;
        }
    }
    return true;
};
