// Test file for object type extraction issue
interface Command {
    updateStyle: {
        newStyles: Partial<any>;  // The delta to apply (only changed properties)
        // Stored during execute() for undo
        previousStyles?: Map<string, any>;  // objectId → old style
    };

    simpleType: string;

    nestedObject: {
        foo: number;
        bar: boolean;
    };
}
