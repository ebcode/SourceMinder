// JavaScript arrow functions and anonymous functions

// Basic arrow function
const square = x => x ** 2;

// Multi-parameter arrow function
const add = (a, b) => a + b;
const multiply = (x, y, z) => x * y * z;

// Arrow function with block body
const greet = name => {
    const greeting = `Hello, ${name}!`;
    return greeting;
};

// Arrow function in array methods
const numbers = [1, 2, 3, 4, 5];
const squared = numbers.map(x => x ** 2);
const evens = numbers.filter(x => x % 2 === 0);
const total = numbers.reduce((acc, x) => acc + x, 0);

// Async arrow function
const fetchData = async (url) => {
    const response = await fetch(url);
    return response.text();
};

// Arrow function returning object (requires parentheses)
const makePoint = (x, y) => ({ x, y });

// Anonymous function expression
const oldStyleFunc = function(x) {
    return x * 2;
};

// Named function expression
const namedFunc = function multiply(x, y) {
    return x * y;
};

// Anonymous async function
const asyncFunc = async function(id) {
    console.log(id);
};

// Arrow function in object
const operations = {
    add: (x, y) => x + y,
    sub: (x, y) => x - y,
    mul: (x, y) => x * y,
    div: (x, y) => y !== 0 ? x / y : null
};

// Arrow function as callback
setTimeout(() => {
    console.log('Delayed execution');
}, 1000);

// Arrow function with destructuring
const getFullName = ({ firstName, lastName }) => `${firstName} ${lastName}`;

// Closure with arrow function
function makeMultiplier(factor) {
    return x => x * factor;
}

const triple = makeMultiplier(3);
const quadruple = makeMultiplier(4);

// Arrow function returning arrow function
const curry = a => b => a + b;
const addFive = curry(5);
const result = addFive(10);

// Immediately invoked arrow function (IIAFE)
const immediateResult = (x => x * 2)(5);

// IIFE with anonymous function
const config = (function() {
    const privateVar = 'secret';
    return {
        get: () => privateVar
    };
})();

// Arrow function with rest parameters
const sum = (...args) => args.reduce((a, b) => a + b, 0);

// Arrow function with default parameters
const greetWithDefault = (name = 'World') => `Hello, ${name}!`;

// Nested arrow functions
const outer = x => {
    const inner = y => x + y;
    return inner;
};

// Arrow function in promise chain
Promise.resolve(42)
    .then(x => x * 2)
    .then(x => x + 10)
    .catch(err => console.error(err));

// Arrow function with conditional return
const abs = x => x >= 0 ? x : -x;

// Arrow function in class
class Calculator {
    constructor() {
        this.multiplier = 2;
    }

    // Arrow function as class property (binds 'this')
    multiply = x => x * this.multiplier;

    // Regular method for comparison
    divide(x) {
        return x / this.multiplier;
    }
}

// Higher-order function with arrow function parameter
function applyTwice(fn, value) {
    return fn(fn(value));
}

const doubledTwice = applyTwice(x => x * 2, 5);

// Arrow function in array sorting
const students = [
    { name: 'Alice', grade: 85 },
    { name: 'Bob', grade: 92 },
    { name: 'Charlie', grade: 85 }
];
const sorted = students.sort((a, b) => a.grade - b.grade);

// Arrow function with array methods chained
const processedNumbers = [1, 2, 3, 4, 5]
    .filter(x => x > 2)
    .map(x => x ** 2)
    .reduce((acc, x) => acc + x, 0);

// Arrow function in forEach
numbers.forEach(x => console.log(x));

// Arrow function in find/findIndex
const firstEven = numbers.find(x => x % 2 === 0);
const firstEvenIndex = numbers.findIndex(x => x % 2 === 0);

// Arrow function in some/every
const hasEven = numbers.some(x => x % 2 === 0);
const allPositive = numbers.every(x => x > 0);

// Callback with anonymous function
document.addEventListener('click', function(event) {
    console.log('Clicked at:', event.clientX, event.clientY);
});

// Callback with arrow function (if in browser)
document.addEventListener('keydown', (e) => {
    console.log('Key pressed:', e.key);
});

// Generator function (anonymous)
const generator = function*() {
    yield 1;
    yield 2;
    yield 3;
};

// Async generator
const asyncGenerator = async function*() {
    yield await Promise.resolve(1);
    yield await Promise.resolve(2);
};

// Arrow function in object method shorthand context
const api = {
    fetch: async (id) => {
        return await fetch(`/api/items/${id}`);
    },
    process: data => data.map(x => x * 2)
};

// Nested anonymous functions
const complex = function(x) {
    return function(y) {
        return function(z) {
            return x + y + z;
        };
    };
};

const partial1 = complex(1);
const partial2 = partial1(2);
const finalResult = partial2(3);
