// Test file for TypeScript modifier detection
// Tests: static, async, readonly, abstract modifiers

// Abstract class with abstract methods
abstract class AbstractBase {
    abstract abstractMethod(): void;
    abstract readonly abstractProperty: string;

    regularMethod() {
        return "concrete";
    }
}

// Regular class with various modifiers
class TestClass {
    // Static members
    static staticProperty = "static value";
    static readonly staticReadonlyProperty = 100;

    // Instance members
    public regularProperty: string;
    private privateProperty: number;
    readonly readonlyProperty: boolean;

    constructor() {
        this.regularProperty = "test";
        this.privateProperty = 42;
        this.readonlyProperty = true;
    }

    // Static methods
    static staticMethod() {
        return "static";
    }

    static async staticAsyncMethod() {
        return Promise.resolve("static async");
    }

    // Instance methods
    regularMethod() {
        return "regular";
    }

    async asyncMethod() {
        return Promise.resolve("async");
    }

    private privateMethod() {
        return "private";
    }

    protected protectedMethod() {
        return "protected";
    }

    public async publicAsyncMethod() {
        return Promise.resolve("public async");
    }
}

// Async functions
async function asyncFunction() {
    return Promise.resolve("async function");
}

function regularFunction() {
    return "regular function";
}

// Interface with readonly
interface TestInterface {
    readonly readonlyProp: string;
    regularProp: number;
}

// Type alias
type TestType = {
    readonly readonlyField: string;
    mutableField: number;
};

// Export with async
export async function exportedAsyncFunction() {
    return Promise.resolve("exported async");
}

export class ExportedClass {
    static readonly exportedStaticReadonly = "value";

    async exportedAsyncMethod() {
        return Promise.resolve("exported async method");
    }
}
