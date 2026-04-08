// Context types test - classes, functions, variables, properties
class UserManager {
  constructor(database) {
    this.database = database;
    this.cache = new Map();
  }

  async getUser(userId) {
    return this.database.query('SELECT * FROM users WHERE id = ?', [userId]);
  }

  clearCache() {
    this.cache.clear();
  }
}

const globalConfig = {
  apiKey: 'test',
  timeout: 5000
};

function processRequest(request, options) {
  const { method, url } = request;
  return { method, url, options };
}

module.exports = { UserManager, globalConfig, processRequest };
