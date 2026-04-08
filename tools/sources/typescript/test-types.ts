// Test file for TypeScript type extraction
// Testing: variable types, function return types, property types

// Variables with explicit type annotations
const userName: string = "test";
let count: number = 42;
const isActive: boolean = true;
const data: Data = fetchData();
const items: string[] = ["a", "b"];
const callback: (x: number) => void = (x) => {};

// Variables without type annotations (implicit/inferred)
const inferredString = "hello";
const inferredNumber = 123;

// Function declarations with return types
function getUserById(id: number): User {
    return { id, name: "test" };
}

function processData(): void {
    console.log("processing");
}

async function fetchUser(): Promise<User> {
    return { id: 1, name: "test" };
}

// Arrow functions with return types
const getCount = (): number => 42;
const transform = (x: string): Data => ({ value: x });

// Class with property types
class UserManager {
    // Properties with types
    name: string;
    age: number;
    private userId: number;
    public readonly maxUsers: number = 100;
    static instanceCount: number = 0;

    // Methods with return types
    getUser(): User {
        return { id: 1, name: this.name };
    }

    async fetchData(): Promise<Data> {
        return { value: "data" };
    }

    processItems(items: string[]): void {
        console.log(items);
    }
}

// Interface with property types
interface Config {
    host: string;
    port: number;
    timeout: number;
    callback: (err: Error) => void;
}

// Type alias
type Point = {
    x: number;
    y: number;
};

// Generic types
const map: Map<string, number> = new Map();
const users: Array<User> = [];
function getItems<T>(): T[] {
    return [];
}
