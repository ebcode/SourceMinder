// TypeScript explicit property declarations test
class TestClass {
  database: Database;
  apiKey: string;
  cache: Map<string, any>;
  config: Config;
  nested: NestedType;
  dynamicProp: any;
  initialized: boolean;
  timestamp: number;

  constructor(database: Database, apiKey: string) {
    this.database = database;
    this.apiKey = apiKey;
    this.cache = new Map();
    this.config.timeout = 5000;
    this.nested.deep.value = 'test';
  }

  setProperty(value: any) {
    this.dynamicProp = value;
  }

  initializeState() {
    this.initialized = true;
    this.timestamp = Date.now();
  }
}

// Should NOT be indexed (not this)
function external(obj: any) {
  obj.property = 'value';
}
