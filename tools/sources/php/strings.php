<?php

// Single-quoted strings (no interpolation)
$single = 'hello world from single';
$singleMulti = 'foo bar baz';

// Double-quoted strings (no variables)
$double = "hello world from double";
$doubleMulti = "apple orange banana";

// Double-quoted with simple variable interpolation
$name = "John";
$greeting = "Hello $name welcome";

// Double-quoted with complex interpolation
class User {
    public $username = "testuser";
}
$user = new User();
$message = "User is {$user->username} active";

// Heredoc syntax (interpolation enabled)
$heredoc = <<<EOT
This is heredoc content
Multiple lines available
Has words like database connection
EOT;

// Heredoc with variable interpolation
$city = "Boston";
$heredocInterpolated = <<<EOT
Welcome to $city
Enjoy your stay
EOT;

// Nowdoc syntax (no interpolation, like single-quoted)
$nowdoc = <<<'END'
This is nowdoc content
No interpolation here
Keywords like password authentication
END;

// String concatenation
$concat = "first part" . " second part";

// Empty strings
$empty1 = "";
$empty2 = '';

// Strings in function calls
echo "output message here";
print 'another message text';

// Strings in array
$array = [
    "first element value",
    'second element text',
    "error message failed"
];

// Multi-line double-quoted
$multiline = "This is line one
and this is line two
with more content";

// Escape sequences
$escaped = "quote \" and newline \n tab \t";

// String with numbers
$withNumbers = "Order number 12345";
