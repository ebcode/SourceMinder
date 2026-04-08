// Test generic types with complex arguments
interface Test {
    simpleGeneric: Array<string>;

    genericWithObject: ReadonlyArray<{
        kind: string;
        nested: { foo: number };
    }>;
}
