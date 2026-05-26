import sqlite3InitModule from "./node_modules/@sqlite.org/sqlite-wasm/dist/index.mjs";

const statusEl = document.getElementById("status");
const summaryEl = document.getElementById("summary");
const contextsEl = document.getElementById("contexts");
const contextsBodyEl = document.getElementById("contexts-body");
const sampleEl = document.getElementById("sample");
const sampleBodyEl = document.getElementById("sample-body");
const queryShellEl = document.getElementById("query-shell");
const queryOutputEl = document.getElementById("query-output");
const queryInputEl = document.getElementById("query-input");
const errorEl = document.getElementById("error");

let activeDb = null;

const CONTEXT_ALIASES = {
  arg: "ARG",
  argument: "ARG",
  call: "CALL",
  case: "CASE",
  class: "CLASS",
  com: "COM",
  comment: "COM",
  enum: "ENUM",
  exc: "EXC",
  exception: "EXC",
  exp: "EXP",
  export: "EXP",
  file: "FILE",
  filename: "FILE",
  func: "FUNC",
  function: "FUNC",
  goto: "GOTO",
  iface: "IFACE",
  imp: "IMP",
  import: "IMP",
  interface: "IFACE",
  label: "LABEL",
  lam: "LAM",
  lambda: "LAM",
  namespace: "NS",
  ns: "NS",
  prop: "PROP",
  property: "PROP",
  str: "STR",
  string: "STR",
  trait: "TRAIT",
  type: "TYPE",
  var: "VAR",
  variable: "VAR",
};

const MULTI_VALUE_FLAGS = new Set(["-i", "--include-context", "-x", "--exclude-context", "-f", "--file"]);

function setStatus(message) {
  statusEl.textContent = message;
}

function showError(message) {
  errorEl.hidden = false;
  errorEl.textContent = message;
  setStatus("Failed to load browser SQLite test harness.");
}

function setQueryOutput(message) {
  queryOutputEl.value = message;
}

function expectSingleValue(db, sql, bind) {
  const value = db.selectValue(sql, bind);
  if (value === undefined) {
    throw new Error(`Query returned no rows: ${sql}`);
  }
  return value;
}

function renderSummary(cards) {
  summaryEl.innerHTML = "";
  for (const card of cards) {
    const node = document.createElement("div");
    node.className = "card";
    node.innerHTML = `<span class="label">${card.label}</span><div class="value">${card.value}</div>`;
    summaryEl.appendChild(node);
  }
  summaryEl.hidden = false;
}

function renderContexts(rows) {
  contextsBodyEl.innerHTML = "";
  for (const [context, count] of rows) {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td>${context}</td><td>${count}</td>`;
    contextsBodyEl.appendChild(tr);
  }
  contextsEl.hidden = false;
}

function renderSampleRows(rows) {
  sampleBodyEl.innerHTML = "";
  for (const [symbol, context, directory, filename, line] of rows) {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td><code>${symbol}</code></td><td>${context}</td><td><code>${directory}${filename}</code></td><td>${line}</td>`;
    sampleBodyEl.appendChild(tr);
  }
  sampleEl.hidden = false;
}

function normalizePattern(rawPattern) {
  return rawPattern.replace(/\\/g, "\\\\").replace(/%/g, "\\%").replace(/_/g, "\\_").replace(/\*/g, "%").replace(/\./g, "_");
}

function tokenizeCommand(input) {
  const tokens = [];
  let current = "";
  let quote = null;

  for (let i = 0; i < input.length; i += 1) {
    const char = input[i];
    const next = input[i + 1];

    if (char === "\\" && next) {
      current += next;
      i += 1;
      continue;
    }

    if (quote) {
      if (char === quote) {
        quote = null;
      } else {
        current += char;
      }
      continue;
    }

    if (char === '"' || char === "'") {
      quote = char;
      continue;
    }

    if (/\s/.test(char)) {
      if (current) {
        tokens.push(current);
        current = "";
      }
      continue;
    }

    current += char;
  }

  if (current) {
    tokens.push(current);
  }

  return tokens;
}

function isFlagToken(token) {
  return token.startsWith("-");
}

function normalizeContextToken(token) {
  if (token.toLowerCase() === "noise") {
    return ["COM", "STR"];
  }

  const mapped = CONTEXT_ALIASES[token.toLowerCase()];
  return mapped ? [mapped] : [];
}

function buildFileLikePattern(rawPattern) {
  const normalized = normalizePattern(rawPattern);
  if (rawPattern.startsWith(".")) {
    return `%${normalized}`;
  }
  if (rawPattern.endsWith("/")) {
    return `%${normalized}%`;
  }
  if (rawPattern.includes("/")) {
    return `%${normalized}%`;
  }
  return normalized.includes("%") || normalized.includes("_") ? normalized : rawPattern;
}

function parseQiCommand(input) {
  const tokens = tokenizeCommand(input.trim());
  if (tokens.length === 0) {
    throw new Error("Enter a qi command.");
  }

  const command = {
    raw: input.trim(),
    patterns: [],
    include: [],
    exclude: [],
    files: [],
    definition: null,
    limit: 25,
    unsupported: [],
  };

  let index = tokens[0] === "qi" ? 1 : 0;
  while (index < tokens.length) {
    const token = tokens[index];

    if (!isFlagToken(token)) {
      command.patterns.push(token);
      index += 1;
      continue;
    }

    if (token === "--def") {
      command.definition = 1;
      index += 1;
      continue;
    }

    if (token === "--usage") {
      command.definition = 0;
      index += 1;
      continue;
    }

    if (token === "--limit") {
      const value = tokens[index + 1];
      if (!value || isFlagToken(value)) {
        throw new Error("--limit requires a number.");
      }
      const parsed = Number.parseInt(value, 10);
      if (!Number.isFinite(parsed) || parsed <= 0) {
        throw new Error("--limit must be a positive integer.");
      }
      command.limit = parsed;
      index += 2;
      continue;
    }

    if (MULTI_VALUE_FLAGS.has(token)) {
      const values = [];
      index += 1;
      while (index < tokens.length && !isFlagToken(tokens[index])) {
        values.push(tokens[index]);
        index += 1;
      }
      if (values.length === 0) {
        throw new Error(`${token} requires at least one value.`);
      }

      if (token === "-i" || token === "--include-context") {
        for (const value of values) {
          const contexts = normalizeContextToken(value);
          if (contexts.length === 0) {
            throw new Error(`Unknown context type: ${value}`);
          }
          command.include.push(...contexts);
        }
      } else if (token === "-x" || token === "--exclude-context") {
        for (const value of values) {
          const contexts = normalizeContextToken(value);
          if (contexts.length === 0) {
            throw new Error(`Unknown context type: ${value}`);
          }
          command.exclude.push(...contexts);
        }
      } else if (token === "-f" || token === "--file") {
        command.files.push(...values);
      }
      continue;
    }

    command.unsupported.push(token);
    index += 1;
  }

  if (command.patterns.length === 0) {
    throw new Error("A qi command needs at least one search pattern.");
  }

  command.include = [...new Set(command.include)];
  command.exclude = [...new Set(command.exclude.filter((value) => !command.include.includes(value)))];
  return command;
}

function buildInClause(column, values, binds, negate = false) {
  const placeholders = values.map(() => "?").join(", ");
  binds.push(...values);
  return `${column} ${negate ? "NOT IN" : "IN"} (${placeholders})`;
}

function buildQiSql(command) {
  const binds = [];
  const clauses = [];

  const patternClauses = command.patterns.map((pattern) => {
    const normalized = normalizePattern(pattern.toLowerCase());
    const hasWildcard = /[%_]/.test(normalized);
    binds.push(normalized);
    return hasWildcard ? "symbol LIKE ? ESCAPE '\\'" : "symbol = ?";
  });
  clauses.push(`(${patternClauses.join(" OR ")})`);

  if (command.include.length > 0) {
    clauses.push(buildInClause("context", command.include, binds));
  }

  if (command.exclude.length > 0) {
    clauses.push(buildInClause("context", command.exclude, binds, true));
  }

  if (command.definition !== null) {
    clauses.push("is_definition = ?");
    binds.push(command.definition);
  }

  if (command.files.length > 0) {
    const fileClauses = command.files.map((pattern) => {
      const likePattern = buildFileLikePattern(pattern);
      const targetsPath = pattern.includes("/") || pattern.endsWith("/") || pattern.startsWith(".");
      binds.push(likePattern);
      return targetsPath ? "(directory || filename) LIKE ? ESCAPE '\\'" : "filename LIKE ? ESCAPE '\\'";
    });
    clauses.push(`(${fileClauses.join(" OR ")})`);
  }

  const where = clauses.join(" AND ");
  return {
    binds,
    countSql: `SELECT COUNT(*) FROM code_index WHERE ${where}`,
    rowsSql: `SELECT line, symbol, context, directory, filename
              FROM code_index
              WHERE ${where}
              ORDER BY directory, filename, line
              LIMIT ${command.limit}`,
  };
}

function runTinyQuery(input) {
  if (!activeDb) {
    setQueryOutput("Database is still loading.");
    return;
  }

  try {
    const command = parseQiCommand(input);
    const { binds, countSql, rowsSql } = buildQiSql(command);
    const total = expectSingleValue(activeDb, countSql, binds);
    const rows = activeDb.selectArrays(rowsSql, binds);
    const lines = [`$ ${command.raw}`, ""];

    if (command.unsupported.length > 0) {
      lines.push(`Ignoring unsupported flags: ${command.unsupported.join(" ")}`);
      lines.push("");
    }

    if (rows.length === 0) {
      lines.push("No results");
      setQueryOutput(lines.join("\n"));
      return;
    }

    for (const [line, symbol, context, directory, filename] of rows) {
      lines.push(`${directory}${filename}:${line}  ${symbol}  ${context}`);
    }

    lines.push("");
    lines.push(`Found ${total} match${total === 1 ? "" : "es"}${total > rows.length ? ` (showing first ${rows.length})` : ""}`);
    setQueryOutput(lines.join("\n"));
  } catch (error) {
    setQueryOutput(error instanceof Error ? error.message : String(error));
  }
}

function installQueryForm() {
  queryInputEl.addEventListener("keydown", (event) => {
    if (event.key !== "Enter") {
      return;
    }
    event.preventDefault();
    runTinyQuery(queryInputEl.value);
  });

  queryShellEl.hidden = false;
  setQueryOutput("SQLite is ready. Enter a qi command and press Enter.");
}

async function loadDatabaseBytes() {
  const response = await fetch("./code-index.browser.db");
  if (!response.ok) {
    throw new Error(`Failed to fetch ./code-index.browser.db: ${response.status} ${response.statusText}`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

async function main() {
  try {
    setStatus("Initializing SQLite WASM runtime...");
    const sqlite3 = await sqlite3InitModule({
      print: () => {},
      printErr: (...args) => console.error(...args),
    });

    setStatus("Fetching browser snapshot database...");
    const bytes = await loadDatabaseBytes();

    setStatus("Deserializing SQLite database into browser memory...");
    const db = new sqlite3.oo1.DB();
    const ptr = sqlite3.wasm.allocFromTypedArray(bytes);
    const rc = sqlite3.capi.sqlite3_deserialize(
      db.pointer,
      "main",
      ptr,
      bytes.byteLength,
      bytes.byteLength,
      sqlite3.capi.SQLITE_DESERIALIZE_FREEONCLOSE,
    );
    db.checkRc(rc);
    activeDb = db;

    const totalRows = expectSingleValue(db, "SELECT COUNT(*) FROM code_index");
    const totalFiles = expectSingleValue(db, "SELECT COUNT(DISTINCT directory || filename) FROM code_index");
    const distinctSymbols = expectSingleValue(db, "SELECT COUNT(DISTINCT symbol) FROM code_index");
    const definitions = expectSingleValue(db, "SELECT COUNT(*) FROM code_index WHERE is_definition = 1");

    const topContexts = db.selectArrays(`
      SELECT context, COUNT(*) AS count
      FROM code_index
      GROUP BY context
      ORDER BY count DESC
      LIMIT 8
    `);

    const sampleRows = db.selectArrays(`
      SELECT symbol, context, directory, filename, line
      FROM code_index
      ORDER BY directory, filename, line
      LIMIT 10
    `);

    renderSummary([
      { label: "Indexed rows", value: totalRows.toLocaleString() },
      { label: "Distinct files", value: totalFiles.toLocaleString() },
      { label: "Distinct symbols", value: distinctSymbols.toLocaleString() },
      { label: "Definitions", value: definitions.toLocaleString() },
    ]);

    renderContexts(topContexts);
    renderSampleRows(sampleRows);
    installQueryForm();
    runTinyQuery("qi % -i call -x noise --limit 20");

    setStatus(`SQLite WASM is running in-browser with SQLite ${sqlite3.version.libVersion}.`);
  } catch (error) {
    console.error(error);
    showError(error instanceof Error ? error.stack || error.message : String(error));
  }
}

main();
