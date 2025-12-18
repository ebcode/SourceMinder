// Test basic TypeScript class
class User {
    name: string;
    age: number;
    email: string;

    constructor(name: string, age: number, email: string) {
        this.name = name;
        this.age = age;
        this.email = email;
    }

    getInfo(): string {
        return `${this.name} (${this.age})`;
    }

    isAdult(): boolean {
        return this.age >= 18;
    }

    updateEmail(newEmail: string): void {
        this.email = newEmail;
    }

    static fromJSON(json: any): User {
        return new User(json.name, json.age, json.email);
    }
}
