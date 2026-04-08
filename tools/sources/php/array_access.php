<?php

// Simple array access
$users = [];
$users[$index] = 'Alice';
$users[0] = 'Bob';

// Array access with expressions
$data[$offset + 1] = 'value';
$items[$count * 2] = 'item';
$records[$key . '_suffix'] = 'record';

// Nested array access
$matrix[$row][$col] = 'cell';
$config[$section][$setting] = 'option';

// Array access in conditions
if ($array[$position] > 0) {
    $result = $array[$index];
}

// Array access with function calls
$cache[$getId()] = 'cached';
$lookup[$getKey($param)] = 'value';

// Multidimensional with expressions
$grid[$x + 1][$y - 1] = 'point';
