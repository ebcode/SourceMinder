// TypeScript class with explicit property declarations
class UserManager {
  database: Database;
  cache: Map<string, User>;

  constructor(database: Database) {
    this.database = database;
    this.cache = new Map();
  }

  async getUser(userId: string) {
    return this.database.query('SELECT * FROM users WHERE id = ?', [userId]);
  }
}
