// Test ES2019 private class members using #
class BankAccount {
  #balance: number;
  #accountNumber: string;
  owner: string;

  constructor(owner: string, initialBalance: number) {
    this.owner = owner;
    this.#balance = initialBalance;
    this.#accountNumber = this.#generateAccountNumber();
  }

  #generateAccountNumber(): string {
    return `ACC-${Math.random().toString(36).substr(2, 9)}`;
  }

  deposit(amount: number): void {
    this.#balance += amount;
  }

  getBalance(): number {
    return this.#balance;
  }

  #validateTransaction(amount: number): boolean {
    return amount > 0 && this.#balance >= amount;
  }
}
