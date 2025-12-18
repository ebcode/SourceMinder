// ES6 exports
export function getUserById(id: string) {
  return { id };
}

export const userCache = new Map();

function internalHelper() {
  return 'not exported';
}
