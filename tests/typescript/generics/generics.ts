// Test TypeScript generics
class Box<TValue> {
    value: TValue;

    constructor(value: TValue) {
        this.value = value;
    }

    getValue(): TValue {
        return this.value;
    }

    setValue(newValue: TValue): void {
        this.value = newValue;
    }
}

function identity<TItem>(arg: TItem): TItem {
    return arg;
}

function pair<TKey, TVal>(key: TKey, val: TVal): [TKey, TVal] {
    return [key, val];
}

type Result<TData> = { data: TData; success: boolean };
