/**
 * @typedef {Object} User
 * @property {string} userId
 * @property {string} email
 */

/**
 * Fetches user by ID
 * @param {string} userId - The user identifier
 * @param {boolean} includeDeleted - Include deleted users
 * @returns {Promise<User>}
 */
function fetchUser(userId, includeDeleted) {
  return Promise.resolve({ userId, email: 'test@example.com' });
}

/**
 * @param {User[]} users
 * @returns {number}
 */
function countActiveUsers(users) {
  return users.length;
}

module.exports = { fetchUser, countActiveUsers };
