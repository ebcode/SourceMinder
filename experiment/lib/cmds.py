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
# Individual file-read tools (subset of READ_RE), broken out so the qi-vs-grep
# displacement story can be extended to qi-vs-(grep+cat+sed): qi is also used to
# replace a whole-file `cat` or a line-ranged `sed -n`.
CAT_RE = re.compile(r"(^|[\s;|&(])cat(\s|$)")
SED_RE = re.compile(r"(^|[\s;|&(])sed(\s|$)")

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


def _split_segments(command: str) -> list[str]:
    """Quote-aware split of a shell action on unquoted ; | & and newlines, so
    operators inside quotes don't corrupt detection. Paired && / || collapse to
    one boundary. Returns every segment (empty ones included; callers strip)."""
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
    return segs


def _segment_program(seg: str) -> str | None:
    """First word of a segment after stripping leading VAR=val assignments;
    None for an empty/whitespace segment."""
    s = re.sub(r"^(?:\w+=\S+\s+)+", "", seg.strip())
    if not s:
        return None
    return s.split()[0]


def command_programs(command: str) -> list[str]:
    """The program invoked by each non-empty segment of a shell action, in order
    (e.g. ``qi a; echo x; grep b`` -> ["qi", "echo", "grep"])."""
    return [p for p in (_segment_program(s) for s in _split_segments(command)) if p]


_GREP_ALIASES = {"grep", "rg", "ag", "ack"}


def _recognized_kinds(command: str) -> set[str]:
    """The set of recognized exploration tools an action uses, one label per kind:
    'qi' | 'grep' | 'cat' | 'sed_read'. grep aliases (rg/ag/ack) fold into 'grep';
    only stdout sed (``sed -n``, not ``sed -i``) counts as 'sed_read'. Anything
    else -- head/tail/less, sed -i edits, pytest/npm/git, ... -- is not recognized
    and contributes nothing."""
    kinds: set[str] = set()
    for seg in _split_segments(command):
        p = _segment_program(seg)
        if p is None:
            continue
        if p == "qi":
            kinds.add("qi")
        elif p in _GREP_ALIASES:
            kinds.add("grep")
        elif p == "cat":
            kinds.add("cat")
        elif p == "sed" and not _sed_is_edit(seg):
            kinds.add("sed_read")
    return kinds


def action_tool(command: str) -> str | None:
    """Classify a shell action by recognized-tool *usage*, not a forced partition.

    Returns 'qi' | 'grep' | 'cat' | 'sed_read' when exactly one recognized tool is
    used, 'mixed' when two or more are, or None when the action uses none of them
    (a pytest run, an edit, a git command -- excluded from the exploration charts).
    ``pytest ... | head`` -> None; ``cat a; sed -n b`` -> 'mixed';
    ``cat a; cat b`` -> 'cat'; ``qi a; echo ===; qi b`` -> 'qi'."""
    kinds = _recognized_kinds(command)
    if not kinds:
        return None
    if len(kinds) >= 2:
        return "mixed"
    return next(iter(kinds))


# Pure output-reducing pipe filters: they truncate/reshape an upstream tool's
# stream without adding independent content, so they don't break homogeneous
# token attribution (``grep x | head`` is still grep's output). echo is a
# separator the agent prints between outputs. Recognized tools are deliberately
# absent -- a second content tool (``cat a; grep b``) is NOT a filter.
_PASSTHRU_PROGS = {"echo", "head", "tail", "less", "more", "wc", "sort", "uniq",
                   "cut", "tr", "column", "nl", "fold"}


def only_tool_and_echo(command: str, tool: str) -> bool:
    """True when an action's only content source is `tool` -- the homogeneous gate
    for clean per-tool token attribution.

    `tool` is one of 'qi' | 'grep' | 'cat' | 'sed_read'. Besides >=1 `tool`, the
    action may contain only echo and pure output-reducing filters (head/tail/wc/
    ...); any other program (including a *different* recognized tool) disqualifies
    it. ``cat a; cat b`` and ``grep x | head`` ARE homogeneous; ``cat a; grep b``
    and ``cat a; python x`` are NOT."""
    seen_target = False
    for seg in _split_segments(command):
        p = _segment_program(seg)
        if p is None:
            continue
        if tool == "grep":
            kind = "grep" if p in _GREP_ALIASES else p
        elif tool == "sed_read":
            kind = "sed_read" if (p == "sed" and not _sed_is_edit(seg)) else p
        else:
            kind = p
        if kind == tool:
            seen_target = True
        elif p not in _PASSTHRU_PROGS:
            return False
    return seen_target


def only_qi_and_echo(command: str) -> bool:
    """True when an action's segments are exclusively qi and echo, with >=1 qi.

    echo is treated as a no-op separator the agent prints between qi outputs, so
    the action's output is still attributable to qi alone. ``qi foo; ls`` is NOT
    pure (ls adds output); ``qi foo; echo ===; qi bar`` IS pure."""
    return only_tool_and_echo(command, "qi")


def qi_subcommands(command: str) -> list[str]:
    """Split a shell action into its qi-invoking sub-commands, quote-aware.

    Keeps segments whose first word -- after any leading VAR=val assignments --
    is qi. ``qi a; grep b; qi c`` -> ["qi a", "qi c"]."""
    out = []
    for seg in _split_segments(command):
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


def qi_verbose_filter(subcmd: str) -> bool:
    """True if the qi command uses the -v/--verbose flag."""
    toks = _qi_tokenize(subcmd)
    return "-v" in toks or "--verbose" in toks


def count_tools(cmd: str) -> tuple[int, int, int]:
    """Return (qi, grep, file_read) invocation counts in one command string."""
    return (
        len(QI_RE.findall(cmd)),
        len(GREP_RE.findall(cmd)),
        len(READ_RE.findall(cmd)),
    )


def count_cat(cmd: str) -> int:
    """Number of cat invocations in one command string."""
    return len(CAT_RE.findall(cmd))


def count_sed(cmd: str) -> int:
    """Number of sed invocations in one command string."""
    return len(SED_RE.findall(cmd))


def _sed_is_edit(seg: str) -> bool:
    """True if a sed sub-command edits in place (-i / --in-place); otherwise it
    writes to stdout and is a read/transform. Catches -i, -i.bak, --in-place[=X]."""
    for t in _qi_tokenize(seg)[1:]:
        if t == "--in-place" or t.startswith("--in-place=") or re.match(r"^-i", t):
            return True
    return False


def _sed_segments(cmd: str) -> list[str]:
    return [s for s in _split_segments(cmd) if _segment_program(s) == "sed"]


def count_sed_read(cmd: str) -> int:
    """sed invocations that print to stdout (a file read/view), not -i edits."""
    return sum(1 for s in _sed_segments(cmd) if not _sed_is_edit(s))


def count_sed_edit(cmd: str) -> int:
    """sed invocations that edit a file in place (-i / --in-place)."""
    return sum(1 for s in _sed_segments(cmd) if _sed_is_edit(s))


def uses_qi(cmd: str) -> bool:
    """Whether the command invokes qi at all (for per-turn flagging)."""
    return QI_RE.search(cmd) is not None
