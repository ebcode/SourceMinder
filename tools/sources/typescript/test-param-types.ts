// Test TypeScript parameter types

interface FileFilter {
    count: number;
}

function simpleType(x: number): void {}

function customType(filter: FileFilter): void {}

function arrayType(items: string[]): void {}

function optionalParam(count?: number): void {}
