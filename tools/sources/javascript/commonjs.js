// CommonJS exports test
const helpers = require('./helpers');

function getUserById(id) {
  return { id, name: 'test' };
}

function createUser(name, email) {
  return { name, email };
}

const userCache = new Map();

module.exports = {
  getUserById,
  createUser,
  userCache
};
