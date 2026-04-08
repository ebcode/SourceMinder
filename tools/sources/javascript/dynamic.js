// Dynamic imports and requires
const fs = require('fs');
const path = require('path');

function loadModule(moduleName) {
  const mod = require('./' + moduleName);
  return mod;
}

async function loadComponent(componentName) {
  const component = await import(`./components/${componentName}`);
  return component.default;
}

function requireConfig(env) {
  return require(`./config/${env}.json`);
}

module.exports = { loadModule, loadComponent, requireConfig };
