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

var term = null;
var cmdBuffer = "";
var cursorPos = 0;
var isExecuting = false;
var PROMPT = "qi> ";

/* Command history */
var history = [];
var historyIndex = -1;
var savedBuffer = "";

var worker = new Worker("./qi-worker.js", { type: "module" });

function setStatus(message) {
    statusEl.textContent = message;
}

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

    case "ready":
        console.log("[main] ready, version:", msg.version);
        renderSummary(msg.summary);
        installTerminal();
        setStatus("SQLite WASM is running in-browser with SQLite " + msg.version + ".");
        break;

    case "output":
        console.log("[main] output, length:", msg.text.length, "first 200:", JSON.stringify(msg.text.substring(0, 200)));
        termWrite(msg.text);
        isExecuting = false;
        resetPrompt();
        break;

    case "error":
        console.error("[main] error:", msg.message);
        termWrite("Error: " + msg.message + "\r\n");
        isExecuting = false;
        resetPrompt();
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
        if (isExecuting || event.type !== "keydown") return true;

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
                        if (history.length === 0 || history[history.length - 1] !== cmd)
                            history.push(cmd);
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

    termWrite("qi WASM bridge ready. Type a qi command.\r\n");
    resetPrompt();

    /* Self-test */
    var testCmd = "qi % -i call -v -x noise --limit 5";
    termWrite(testCmd + "\r\n");
    runTinyQuery(testCmd);
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
