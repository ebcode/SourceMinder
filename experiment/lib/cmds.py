"""Detect exploration-tool invocations in an agent's shell command.

One canonical definition of "did this command call qi / a grep-like tool / a
file-dump tool", so the mechanism metric is identical across analyzers. Before
this, ``analyze_trajectories.py`` and ``traj_diff.py`` used *different* regexes
(traj_diff only matched ``qi`` right after ``&&``, undercounting), so the same
trajectory could report different qi-usage depending on which tool you asked.

The patterns match a token at a command boundary (start of string, or after
whitespace / ``;`` / ``|`` / ``&`` / ``(``) so ``cd x && qi foo`` and
``a | grep b`` both count, while ``equip`` or ``acatalog`` do not.
"""
from __future__ import annotations

import re
import shlex

QI_RE = re.compile(r"(^|[\s;|&(])qi(\s|$)")
GREP_RE = re.compile(r"(^|[\s;|&(])(grep|rg|ag|ack)(\s|$)")
READ_RE = re.compile(r"(^|[\s;|&(])(cat|sed|head|tail|less|more)(\s|$)")

# --- qi command-quality analysis (shared by the per-command extractors and the
# per-run analyzers, so the antipattern definitions never drift between them) ---

# qi flags that consume the NEXT token as a value, so that token is not a
# positional search pattern. Multi-value flags are listed too; only -f's extra
# values could resemble a dotted name, and those are caught by the extension
# guard in qi_dotted_pattern().
_QI_VALUE_FLAGS = {
    "-i", "--include-context", "-x", "--exclude-context", "-f", "--file",
    "-p", "--parent", "-s", "--scope", "-ns", "--namespace", "-m", "--modifier",
    "-c", "--clue", "-t", "--type", "--parent-type", "--lines", "-w", "--within",
    "-l", "--limit", "-lpf", "--limit-per-file", "-C", "--context",
    "-A", "--after-context", "-B", "--before-context", "--db-file", "--columns",
    "-d", "--definition", "--and",
}

# A dotted token ending in one of these is a file path, not a parent.symbol.
_FILE_EXTS = {
    "py", "js", "jsx", "ts", "tsx", "mjs", "cjs", "go", "c", "h", "cc", "cpp",
    "hpp", "rs", "java", "rb", "php", "json", "txt", "md", "cfg", "ini", "yaml",
    "yml", "toml", "lock", "sh", "xml", "html", "css", "scss", "sql", "csv",
    "tsv", "rst", "po", "pot", "svg", "png", "gitignore", "env",
}

_QUALIFIED_RE = re.compile(r"^[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)+$")
_QUOTED_SPAN_RE = re.compile(r"'([^']*)'|\"([^\"]*)\"")


def _qi_tokenize(subcmd: str) -> list[str]:
    """Tokenize a single qi command, tolerating quoting the agent may mangle."""
    try:
        return shlex.split(subcmd)
    except ValueError:
        return subcmd.split()


def qi_subcommands(command: str) -> list[str]:
    """Split a shell action into its qi-invoking sub-commands, quote-aware.

    Splits on unquoted ; | & and newlines (so operators inside quotes don't
    corrupt detection), then keeps segments whose first word -- after any leading
    VAR=val assignments -- is qi. ``qi a; grep b; qi c`` -> ["qi a", "qi c"]."""
    segs: list[str] = []
    buf: list[str] = []
    q: str | None = None
    i = 0
    while i < len(command):
        ch = command[i]
        if q:
            buf.append(ch)
            if ch == q:
                q = None
        elif ch in "'\"":
            q = ch
            buf.append(ch)
        elif ch in ";\n|&":
            segs.append("".join(buf))
            buf = []
            if i + 1 < len(command) and command[i + 1] in "&|":
                i += 1  # swallow the paired && or ||
        else:
            buf.append(ch)
        i += 1
    segs.append("".join(buf))
    out = []
    for seg in segs:
        s = re.sub(r"^(?:\w+=\S+\s+)+", "", seg.strip())
        if re.match(r"qi(\s|$)", s):
            out.append(s)
    return out


def qi_dotted_pattern(subcmd: str) -> bool:
    """True if a positional search pattern is a qualified dotted name
    (``parent.symbol``) rather than a bare identifier -- the antipattern that
    matches literal text and finds nothing. Values consumed by -f/-i/... and
    tokens ending in a known file extension are excluded."""
    toks = _qi_tokenize(subcmd)
    skip = False
    for i, t in enumerate(toks):
        if i == 0:        # 'qi'
            continue
        if skip:
            skip = False
            continue
        if t in _QI_VALUE_FLAGS:
            skip = True
            continue
        if t.startswith("-"):
            continue
        core = t.strip("'\"").replace("*", "")
        if _QUALIFIED_RE.match(core) and core.rsplit(".", 1)[-1].lower() not in _FILE_EXTS:
            return True
    return False


def qi_quoted_phrase(subcmd: str) -> bool:
    """True if any quoted argument contains an internal space -- a multi-word
    phrase passed as one literal pattern (e.g. ``'def run'``, ``"Google Scholar"``)
    instead of separate OR terms."""
    for m in _QUOTED_SPAN_RE.finditer(subcmd):
        inner = m.group(1) if m.group(1) is not None else m.group(2)
        if len(inner.split()) >= 2:
            return True
    return False


def qi_abs_path_filter(subcmd: str) -> bool:
    """True if any -f/--file value is an absolute path (e.g. /app/lib/x.py) --
    tracks whether the agent uses the container-absolute form (which matches the
    index post-reindex)."""
    toks = _qi_tokenize(subcmd)
    i = 0
    while i < len(toks):
        if toks[i] in ("-f", "--file"):
            j = i + 1
            while j < len(toks) and not toks[j].startswith("-"):
                if toks[j].strip("'\"").startswith("/"):
                    return True
                j += 1
            i = j
        else:
            i += 1
    return False


def qi_parent_filter(subcmd: str) -> bool:
    """True if the qi command uses the -p/--parent filter (the disambiguator the
    qualified-names prompt guidance teaches)."""
    toks = _qi_tokenize(subcmd)
    return "-p" in toks or "--parent" in toks


def count_tools(cmd: str) -> tuple[int, int, int]:
    """Return (qi, grep, file_read) invocation counts in one command string."""
    return (
        len(QI_RE.findall(cmd)),
        len(GREP_RE.findall(cmd)),
        len(READ_RE.findall(cmd)),
    )


def uses_qi(cmd: str) -> bool:
    """Whether the command invokes qi at all (for per-turn flagging)."""
    return QI_RE.search(cmd) is not None
