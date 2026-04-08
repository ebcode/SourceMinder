// TypeScript arrow functions and anonymous functions

// Basic arrow function
const square = (xx: number): number => xx ** 2;

// Multi-parameter arrow function
const add = (aa: number, bb: number): number => aa + bb;
const multiply = (xx: number, yy: number, zz: number): number => xx * yy * zz;

// Arrow function with block body
const greet = (name: string): string => {
    const greeting = `Hello, ${name}!`;
    return greeting;
};

// Arrow function in array methods
const numbers = [1, 2, 3, 4, 5];
const squared = numbers.map(xx => xx ** 2);
const evens = numbers.filter(xx => xx % 2 === 0);
const total = numbers.reduce((acc, xx) => acc + xx, 0);

// Arrow function with type inference
const double = (xx: number) => xx * 2;

// Async arrow function
const fetchData = async (url: string): Promise<string> => {
    const response = await fetch(url);
    return response.text();
};

// Arrow function returning object (requires parentheses)
const makePoint = (xx: number, yy: number) => ({ xx, yy });

// Anonymous function expression
const oldStyleFunc = function(xx: number): number {
    return xx * 2;
};

// Anonymous async function
const asyncFunc = async function(id: number): Promise<void> {
    console.log(id);
};

// Arrow function in object
const operations = {
    add: (xx: number, yy: number) => xx + yy,
    sub: (xx: number, yy: number) => xx - yy,
    mul: (xx: number, yy: number) => xx * yy,
    div: (xx: number, yy: number) => y !== 0 ? xx / yy : null
};

// Arrow function as callback
setTimeout(() => {
    console.log('Delayed execution');
}, 1000);

// Arrow function with destructuring
const getFullName = ({ firstName, lastName }: { firstName: string; lastName: string }) =>
    `${firstName} ${lastName}`;

// Closure with arrow function
function makeMultiplier(factor: number) {
    return (xx: number) => xx * factor;
}

const triple = makeMultiplier(3);
const quadruple = makeMultiplier(4);

// Arrow function returning arrow function
const curry = (aa: number) => (bb: number) => aa + bb;
const addFive = curry(5);
const result = addFive(10);

// Generic arrow function
const identity = <T>(xx: T): T => xx;
const first = <T>(arr: T[]): T | undefined => arr[0];

// Arrow function in type definition
type Transformer<T, U> = (input: T) => U;
const toString: Transformer<number, string> = (nn) => nn.toString();

// Immediately invoked arrow function
const immediateResult = ((xx: number) => xx * 2)(5);

// Arrow function with rest parameters
const sum = (...args: number[]): number => args.reduce((aa, bb) => aa + bb, 0);

// Arrow function with default parameters
const greetWithDefault = (name: string = 'World') => `Hello, ${name}!`;

// Nested arrow functions
const outer = (xx: number) => {
    const inner = (yy: number) => xx + yy;
    return inner;
};

// Arrow function in promise chain
Promise.resolve(42)
    .then(x => xx * 2)
    .then(x => xx + 10)
    .catch(err => console.error(err));

// Arrow function with conditional return
const abs = (xx: number) => xx >= 0 ? xx : -xx;

// Arrow function in class
class Calculator {
    private multiplier = 2;

    // Arrow function as class property (binds 'this')
    multiply = (xx: number) => xx * this.multiplier;

    // Regular method for comparison
    divide(xx: number): number {
        return xx / this.multiplier;
    }
}

// Higher-order function with arrow function parameter
function applyTwice<T>(fn: (xx: T) => T, value: T): T {
    return fn(fn(value));
}

const doubledTwice = applyTwice(xx => xx * 2, 5);

// Arrow function in array sorting
const students = [
    { name: 'Alice', grade: 85 },
    { name: 'Bob', grade: 92 },
    { name: 'Charlie', grade: 85 }
];
const sorted = students.sort((aa, bb) => aa.grade - bb.grade);
