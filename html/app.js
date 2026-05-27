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
var isExecuting = false;
var PROMPT = "qi> ";

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

function runTinyQuery(input) {
    if (isExecuting) { console.log('[main] runTinyQuery skipped: already executing'); return; }
    isExecuting = true;
    console.log('[main] runTinyQuery posting:', input);
    worker.postMessage({ type: "query", cmd: input });
}

worker.onmessage = function(event) {
    var msg = event.data;
    console.log('[main] worker msg type:', msg.type);

    switch (msg.type) {
    case "status":
        console.log('[main] status:', msg.message);
        setStatus(msg.message);
        break;

    case "ready":
        console.log('[main] ready, version:', msg.version);
        renderSummary(msg.summary);
        installTerminal();
        setStatus("SQLite WASM is running in-browser with SQLite " + msg.version + ".");
        break;

    case "output":
        console.log('[main] output, length:', msg.text.length, 'first 200:', JSON.stringify(msg.text.substring(0, 200)));
        termWrite(msg.text);
        isExecuting = false;
        termWrite(PROMPT);
        break;

    case "error":
        console.error('[main] error:', msg.message);
        termWrite("Error: " + msg.message + "\r\n");
        isExecuting = false;
        termWrite(PROMPT);
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

    /* Handle keyboard input */
    term.onData(function(data) {
        if (isExecuting) return;

        for (var i = 0; i < data.length; i++) {
            var ch = data[i];

            if (ch === "\r") {
                /* Enter pressed */
                termWrite("\r\n");
                var cmd = cmdBuffer.trim();
                cmdBuffer = "";
                if (cmd) {
                    if (cmd === "clear" || cmd === "cls") {
                        term.clear();
                        termWrite(PROMPT);
                    } else {
                        runTinyQuery(cmd);
                    }
                } else {
                    termWrite(PROMPT);
                }
            } else if (ch === "\x7f") {
                /* Backspace */
                if (cmdBuffer.length > 0) {
                    cmdBuffer = cmdBuffer.slice(0, -1);
                    termWrite("\b \b");
                }
            } else if (ch >= " ") {
                /* Printable character */
                cmdBuffer += ch;
                termWrite(ch);
            }
        }
    });

    /* Fit terminal to container on resize */
    try { fitAddon.fit(); } catch (_) { /* ignore */ }
    window.addEventListener("resize", function() {
        try { fitAddon.fit(); } catch (_) { /* ignore */ }
    });

    termWrite("qi WASM bridge ready. Type a qi command.\r\n");
    termWrite(PROMPT);

    /* Self-test */
    var testCmd = "qi % -i call -x noise --limit 20";
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
