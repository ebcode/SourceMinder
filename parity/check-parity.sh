#!/usr/bin/env bash
# check-parity.sh — run the qi-web parity harness with sane defaults.
#
# The harness lives in test/web-harness/ and is driven by two Node scripts:
#   parity.mjs  — compares native `qi` output against the WASM/browser bridge
#   run.mjs     — pure-helper unit tests + wasm/db integration tests
# This wrapper picks the right project, trims the noise, and gives one verdict.
#
# Usage:
#   parity/check-parity.sh                      # integration tests + parity batch
#   parity/check-parity.sh --batch-only         # parity batch only
#   parity/check-parity.sh "qi % -f x.c -i com" # run ONE qi command; show diff if it diverges
#   parity/check-parity.sh -v                   # verbose: full harness output
#   parity/check-parity.sh -p negroni           # different project (default: sourceminder)
#
# No build step on purpose: this tests whatever qi-web.js is currently built.
# After changing C/JS, rebuild with `make web` yourself, then re-run this.
#
# Exit status: 0 if everything matched/passed, 1 on any divergence/failure,
# 2 on a harness error (mirrors the underlying .mjs scripts).

set -uo pipefail

# Resolve repo root from this script's location so it works from any cwd.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

PARITY="test/web-harness/parity.mjs"
RUN="test/web-harness/run.mjs"

PROJECT="sourceminder"
VERBOSE=0
BATCH_ONLY=0
QUERY=""

usage() {
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--project)   PROJECT="${2:?-p needs a project name}"; shift 2 ;;
        -v|--verbose)   VERBOSE=1; shift ;;
        --batch-only)   BATCH_ONLY=1; shift ;;
        -h|--help)      usage; exit 0 ;;
        -*)             echo "check-parity: unknown flag '$1' (try --help)" >&2; exit 2 ;;
        *)              QUERY="$1"; shift ;;
    esac
done

# ---------------------------------------------------------------------------
# Single-command mode: run one qi query and, if it diverges, show the diff.
# ---------------------------------------------------------------------------
if [[ -n "$QUERY" ]]; then
    if [[ $VERBOSE -eq 1 ]]; then
        node "$PARITY" "$QUERY" --project "$PROJECT"
        exit $?
    fi
    out="$(node "$PARITY" "$QUERY" --project "$PROJECT" 2>/dev/null)"; rc=$?
    # Show from the MATCH/DIFF line through the diff body, dropping the
    # "Project:/Loading" preamble and the trailing "=== parity ===" summary
    # (redundant for a single command).
    echo "$out" | awk '/^=== parity ===/{exit} /^(MATCH|DIFF)/{p=1} p'
    exit $rc
fi

# ---------------------------------------------------------------------------
# Batch mode: parity batch, plus integration tests unless --batch-only.
# ---------------------------------------------------------------------------
parity_rc=0
run_rc=0

if [[ $VERBOSE -eq 1 ]]; then
    [[ $BATCH_ONLY -eq 0 ]] && { node "$RUN" --project "$PROJECT"; run_rc=$?; echo; }
    node "$PARITY" --batch --project "$PROJECT"; parity_rc=$?
    [[ $parity_rc -eq 0 && $run_rc -eq 0 ]] && exit 0 || exit 1
fi

if [[ $BATCH_ONLY -eq 0 ]]; then
    run_out="$(node "$RUN" --project "$PROJECT" 2>/dev/null)"; run_rc=$?
    echo "== tests =="
    echo "$run_out" | grep -E '^(FAIL|[0-9]+ passed)' || true
fi

parity_out="$(node "$PARITY" --batch --project "$PROJECT" 2>/dev/null)"; parity_rc=$?
echo "== parity (${PROJECT}) =="
echo "$parity_out" | grep -E '^DIFF' || true               # only the divergences
echo "$parity_out" | grep -E '[0-9]+ matched, [0-9]+ diverged' || true

echo
if [[ $parity_rc -eq 0 && $run_rc -eq 0 ]]; then
    echo "✓ all green"
    exit 0
else
    echo "✗ failures above (run with -v for full output)"
    exit 1
fi
