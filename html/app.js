/* app.js -- xterm.js terminal shell for qi.
 *
 * qi.wasm and sqlite.wasm run in qi-worker.js (web worker) so no query
 * execution blocks the main-thread terminal UI.
 *
 * xterm.js and FitAddon are loaded via <script> tags (UMD bundles).
 */

var statusEl = document.getElementById("status");
var summaryEl = document.getElementById("summary");
var terminalContainerEl = document.getElementById("terminal-container");
var errorEl = document.getElementById("error");
var projectSelectEl = document.getElementById("project-select");

var term = null;
var cmdBuffer = "";
var cursorPos = 0;
var isExecuting = false;
var switching = false;   /* a project switch is in flight; keep input gated */
var PROMPT = "$ ";

/* Project state */
var projectsById = {};
var currentProjectId = "";   /* last successfully loaded project; revert target on a failed switch */
var terminalInstalled = false;
var sqliteVersion = "";

/* Command history — persisted to localStorage */
var HISTORY_KEY = 'qi_history';
var HISTORY_MAX = 500;
var history = (function() {
    try { return JSON.parse(localStorage.getItem(HISTORY_KEY)) || []; }
    catch (e) { return []; }
})();
var historyIndex = -1;
var savedBuffer = "";

var worker = new Worker("./qi-worker.js", { type: "module" });

function setStatus(message) {
    statusEl.textContent = message;
}

function formatBytes(n) {
    if (n >= 1048576) return (n / 1048576).toFixed(1) + " MB";
    if (n >= 1024) return (n / 1024).toFixed(0) + " KB";
    return n + " B";
}

/* Populate the project dropdown: group projects by language into <optgroup>s
 * (the labeled, in-<select> equivalent of an <hr> separator), languages sorted
 * alphabetically, and append each project's download size to its label. */
function renderProjectOptions(projects) {
    var byLanguage = {};
    for (var i = 0; i < projects.length; i++) {
        var proj = projects[i];
        projectsById[proj.id] = proj;
        var lang = proj.language || "other";
        (byLanguage[lang] = byLanguage[lang] || []).push(proj);
    }

    var langs = Object.keys(byLanguage).sort();
    for (var li = 0; li < langs.length; li++) {
        var group = document.createElement("optgroup");
        group.label = langs[li];
        var members = byLanguage[langs[li]];
        for (var mi = 0; mi < members.length; mi++) {
            var p = members[mi];
            var opt = document.createElement("option");
            opt.value = p.id;
            opt.textContent = p.sizeBytes
                ? p.name + " (" + formatBytes(p.sizeBytes) + ")"
                : p.name;
            group.appendChild(opt);
        }
        projectSelectEl.appendChild(group);
    }
}

/* Project dropdown -> ask the worker to switch projects. */
projectSelectEl.addEventListener("change", function() {
    var p = projectsById[projectSelectEl.value];
    if (!p) return;
    isExecuting = true;            /* block queries until the new DB is ready */
    switching = true;              /* an in-flight query's output must not re-enable input */
    projectSelectEl.disabled = true;
    if (term) termWrite("\r\nSwitching to " + p.name + "...\r\n");
    setStatus("Loading " + p.name + "...");
    worker.postMessage({ type: "load-project", project: p });
});

function showError(message) {
    errorEl.hidden = false;
    errorEl.textContent = message;
    setStatus("Failed to load browser SQLite test harness.");
}

function termWrite(text) {
    if (term) term.write(text.replace(/\n/g, "\r\n"));
}

/* Redraw the prompt line using VT sequences: clear line, write prompt+buffer,
 * position cursor.  Avoids character-by-character artifacts. */
function redrawPrompt() {
    if (!term) return;
    term.write("\r\x1b[K" + PROMPT + cmdBuffer);
    if (cmdBuffer.length > cursorPos)
        term.write("\r\x1b[" + (PROMPT.length + cursorPos) + "C");
}

/* Called after completing a command (Enter / clear) to reset prompt state. */
function resetPrompt() {
    cmdBuffer = "";
    cursorPos = 0;
    redrawPrompt();
}

function runTinyQuery(input) {
    if (isExecuting) { console.log("[main] runTinyQuery skipped: already executing"); return; }
    isExecuting = true;
    console.log("[main] runTinyQuery posting:", input);
    worker.postMessage({ type: "query", cmd: input });
}

/* History navigation */
function historyUp() {
    if (history.length === 0) return;
    if (historyIndex === -1) savedBuffer = cmdBuffer;
    if (historyIndex < history.length - 1) {
        historyIndex++;
        cmdBuffer = history[history.length - 1 - historyIndex];
        cursorPos = cmdBuffer.length;
        redrawPrompt();
    }
}

function historyDown() {
    if (historyIndex === -1) return;
    if (historyIndex > 0) {
        historyIndex--;
        cmdBuffer = history[history.length - 1 - historyIndex];
    } else {
        historyIndex = -1;
        cmdBuffer = savedBuffer;
        savedBuffer = "";
    }
    cursorPos = cmdBuffer.length;
    redrawPrompt();
}

worker.onmessage = function(event) {
    var msg = event.data;
    console.log("[main] worker msg type:", msg.type);

    switch (msg.type) {
    case "status":
        console.log("[main] status:", msg.message);
        setStatus(msg.message);
        break;

    case "projects":
        console.log("[main] projects:", msg.projects.length);
        sqliteVersion = msg.version;
        projectsById = {};
        projectSelectEl.innerHTML = "";
        renderProjectOptions(msg.projects);
        projectSelectEl.hidden = false;
        break;

    case "progress":
        if (msg.total > 0) {
            var pct = Math.floor((msg.loaded / msg.total) * 100);
            setStatus("Downloading " +
                (projectsById[msg.projectId] ? projectsById[msg.projectId].name : "project") +
                "… " + pct + "% (" + formatBytes(msg.loaded) + " / " + formatBytes(msg.total) + ")");
        } else {
            setStatus("Downloading… " + formatBytes(msg.loaded));
        }
        break;

    case "ready":
        console.log("[main] ready, project:", msg.projectId);
        renderSummary(msg.summary);
        isExecuting = false;
        switching = false;
        projectSelectEl.disabled = false;
        if (msg.projectId) { projectSelectEl.value = msg.projectId; currentProjectId = msg.projectId; }
        var firstLoad = !terminalInstalled;
        if (firstLoad) {
            terminalInstalled = true;
            installTerminal();
        }
        /* Announce the loaded project (both first load and switch). */
        termWrite("\r\nLoaded project: " + msg.projectName +
            (msg.projectVersion ? " (v" + msg.projectVersion + ")" : "") + "\r\n\r\n");
        resetPrompt();
        term.focus();
        setStatus("Project: " + (msg.projectName || "") + " — SQLite " + sqliteVersion + " in-browser.");
        /* Self-test: run an auto-query on first load, after the announce. */
        if (firstLoad) {
            //var testCmd = "qi % -i call -v -x noise --limit 5";
            var testCmd = "qi % -f logger.go --toc";
            termWrite(testCmd + "\r\n");
            runTinyQuery(testCmd);
        }
        break;

    case "output":
        console.log("[main] output, length:", msg.text.length, "first 200:", JSON.stringify(msg.text.substring(0, 200)));
        termWrite(msg.text);
        /* If a project switch is queued behind this query, leave input gated;
         * the 'ready' handler will clear isExecuting and redraw the prompt. */
        if (!switching) {
            isExecuting = false;
            resetPrompt();
        }
        break;

    case "error":
        console.error("[main] error:", msg.message);
        if (!terminalInstalled) {
            /* Startup failure (init / first project load) arrives before the
             * terminal exists, so termWrite is a no-op -- surface it in the
             * visible error banner instead, otherwise the page just hangs on its
             * last status line with no reason shown. */
            showError(msg.message);
        } else {
            termWrite("Error: " + msg.message + "\r\n");
        }
        if (msg.phase === "load" || msg.phase === "init") {
            /* The load/switch itself failed -- recover so the shell stays usable
             * (otherwise switching/isExecuting/disabled would stick forever, as
             * only 'ready' clears them and a failed load never reaches 'ready'). */
            switching = false;
            isExecuting = false;
            projectSelectEl.disabled = false;
            if (currentProjectId) projectSelectEl.value = currentProjectId;  /* undo the failed selection */
            if (term) resetPrompt();
        } else if (!switching) {
            /* Query error, no switch pending -- re-enable input. */
            isExecuting = false;
            resetPrompt();
        }
        /* else: query error while a switch is queued behind it -- stay gated;
         * the pending load clears state via 'ready' or its own 'load' error. */
        break;
    }
};

worker.onerror = function(error) {
    showError("Worker error: " + (error.message || String(error)));
};

function installTerminal() {
    var FitAddonCtor = FitAddon.FitAddon || FitAddon;
    term = new Terminal({
        cursorBlink: true,
        fontSize: 14,
        fontFamily: '"SFMono-Regular", Consolas, "Liberation Mono", monospace',
        theme: {
            background: "#0d152b",
            foreground: "#e6edf7",
            cursor: "#9dd4ff",
            selectionBackground: "#2a3f6e",
        },
        cols: 100,
        rows: 30,
    });

    var fitAddon = new FitAddonCtor();
    term.loadAddon(fitAddon);
    term.open(terminalContainerEl);

    /* Intercept arrow keys, Home, End, Delete before xterm processes them.
     * Return false to prevent xterm from emitting terminal escape sequences. */
    term.attachCustomKeyEventHandler(function(event) {
        if (event.type !== "keydown") return true;

        /* Make plain PageUp/PageDown scroll the viewport (xterm only scrolls on
         * Shift+PageUp/Down by default).  Handled before the isExecuting gate so
         * the user can scroll back through output while a query is streaming. */
        if (event.key === "PageUp")   { term.scrollPages(-1); return false; }
        if (event.key === "PageDown") { term.scrollPages(1);  return false; }

        if (isExecuting) return true;

        switch (event.key) {
        case "ArrowUp":    historyUp();   return false;
        case "ArrowDown":  historyDown(); return false;
        case "ArrowLeft":
            if (cursorPos > 0) { cursorPos--; redrawPrompt(); }
            return false;
        case "ArrowRight":
            if (cursorPos < cmdBuffer.length) { cursorPos++; redrawPrompt(); }
            return false;
        case "Home":
            cursorPos = 0; redrawPrompt();
            return false;
        case "End":
            cursorPos = cmdBuffer.length; redrawPrompt();
            return false;
        case "Delete":
            if (cursorPos < cmdBuffer.length) {
                cmdBuffer = cmdBuffer.slice(0, cursorPos) + cmdBuffer.slice(cursorPos + 1);
                redrawPrompt();
            }
            return false;
        }
        return true; /* let xterm handle all other keys */
    });

    /* Handle keyboard input — printable chars, Backspace, Enter. */
    term.onData(function(data) {
        if (isExecuting) return;

        for (var i = 0; i < data.length; i++) {
            var ch = data[i];

            if (ch === "\r") {
                /* Enter pressed — submit command */
                termWrite("\r\n");
                var cmd = cmdBuffer.trim();
                if (cmd) {
                    if (cmd === "clear" || cmd === "cls") {
                        term.clear();
                        resetPrompt();
                    } else {
                        if (history.length === 0 || history[history.length - 1] !== cmd) {
                            history.push(cmd);
                            if (history.length > HISTORY_MAX) history.splice(0, history.length - HISTORY_MAX);
                            try { localStorage.setItem(HISTORY_KEY, JSON.stringify(history)); } catch (e) { /* quota exceeded */ }
                        }
                        historyIndex = -1;
                        runTinyQuery(cmd);
                    }
                } else {
                    resetPrompt();
                }
            } else if (ch === "\x7f") {
                /* Backspace — delete character before cursor */
                if (cursorPos > 0) {
                    cmdBuffer = cmdBuffer.slice(0, cursorPos - 1) + cmdBuffer.slice(cursorPos);
                    cursorPos--;
                    redrawPrompt();
                }
            } else if (ch >= " ") {
                /* Printable character — insert at cursor position */
                cmdBuffer = cmdBuffer.slice(0, cursorPos) + ch + cmdBuffer.slice(cursorPos);
                cursorPos++;
                redrawPrompt();
            }
        }
    });

    /* Fit terminal to container on resize */
    try { fitAddon.fit(); } catch (_) { /* ignore */ }
    window.addEventListener("resize", function() {
        try { fitAddon.fit(); } catch (_) { /* ignore */ }
    });

    //termWrite("qi WASM bridge ready. Type a qi command.\r\n");
    /* No prompt drawn here: the 'ready' handler announces the project and draws
     * the prompt right after, so drawing one now leaves a stray empty '$' line. */

    /* Auto-focus so the user can type immediately without clicking the terminal. */
    term.focus();
}

function renderSummary(cards) {
    summaryEl.innerHTML = "";
    for (var i = 0; i < cards.length; i++) {
        var node = document.createElement("div");
        node.className = "card";
        node.innerHTML = "<span class=\"label\">" + cards[i].label + "</span><div class=\"value\">" + cards[i].value + "</div>";
        summaryEl.appendChild(node);
    }
    summaryEl.hidden = false;
}

function main() {
    try {
        setStatus("Starting qi worker...");
        worker.postMessage({ type: "init" });
    } catch (error) {
        console.error(error);
        showError(error instanceof Error ? error.stack || error.message : String(error));
    }
}

main();
