// JavaScript property assignment test
class TestClass {
  constructor(database, apiKey) {
    this.database = database;
    this.apiKey = apiKey;
    this.cache = new Map();
    this.config.timeout = 5000;
    this.nested.deep.value = 'test';
  }

  setProperty(value) {
    this.dynamicProp = value;
  }

  initializeState() {
    this.initialized = true;
    this.timestamp = Date.now();
  }
}

// Should NOT be indexed (not this)
function external(obj) {
  obj.property = 'value';
}
