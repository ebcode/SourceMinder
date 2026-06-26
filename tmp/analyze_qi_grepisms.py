#!/usr/bin/env python3
"""Analyze qi_commands.csv files for grep-ism patterns that won't work in qi."""

import csv
import re
import sys
import os
from collections import defaultdict

CSV_FILES = [
    "experiment/results/pro_runs/pro_pilot_teleport_v4_flash/qi_commands.csv",
    "experiment/results/pro_runs/pro_pilot_ansible_n40/qi_commands.csv",
    "experiment/results/pro_runs/pro_pilot_ansible_v4_pro/qi_commands.csv",
]

# ============================================================
# Define grep-ism detectors
# ============================================================
# Each detector returns a list of matches found in the command string.
# A match is a dict: {"pattern_type": str, "match_text": str, "context": str}

# NOTE: qi searches for literal symbols. The following grep-isms are
# regex features that qi treats literally, so they won't work as expected.

def detect_alternation(command):
    """Detect \| alternation (grep regex OR) -- qi treats | as literal.
       The agent uses \| to mean 'or', but qi uses --or flag."""
    # Look for escaped pipe: \|
    matches = []
    for m in re.finditer(r'\\\|', command):
        start = max(0, m.start() - 30)
        end = min(len(command), m.end() + 30)
        ctx = command[start:end]
        matches.append({"pattern_type": "1_alternation_\\|", "match_text": m.group(), "context": ctx})
    # Also look for unescaped pipe within quoted patterns (but not in flags like --or)
    # This is tricky: | inside double-quoted args likely means grep-style alternation
    # But grep uses \| for basic regex, and | for extended regex with -E
    # In qi commands, a raw | inside a search pattern is grep-like OR.
    # We detect patterns like: qi "foo|bar" or qi 'foo|bar'
    # But avoid matching the pipe in qi's own output or the --or flag
    # A simple heuristic: look for | not preceded by -- and inside "..." or '...'
    for m in re.finditer(r'["\']([^"\']*\|[^"\']*)["\']', command):
        ctx = m.group(0)
        if '--or' not in ctx and '-or' not in ctx:
            matches.append({"pattern_type": "1_alternation_raw_|_in_pattern", "match_text": m.group(0), "context": ctx})
    return matches

def detect_anchors(command):
    """Detect ^ or $ used as regex anchors in search patterns.
       Examples: qi '^foo' or qi 'foo$'"""
    matches = []
    # Look for patterns like: qi " ^ ..." or qi ' $ ...' or qi " ...$ "
    # We look for ^ at start of a quoted pattern or $ at end
    # Pattern: " ^word or word$ " or ' ^word or word$ '
    for m in re.finditer(r"""qi\s+(?:-[a-zA-Z0-9=]+\s+)*(?:--[a-zA-Z][a-zA-Z-]*(?:\s+[^"'\s-][^\s]*)*(?:\s+))*(?:"|\')(.*?)(?:"|\')""", command):
        pattern = m.group(1)
        if pattern.startswith('^') and len(pattern) > 1:
            matches.append({"pattern_type": "2_anchor_^", "match_text": f'"^{pattern}"', "context": m.group(0)[:120]})
        if pattern.endswith('$') and len(pattern) > 1:
            matches.append({"pattern_type": "2_anchor_$", "match_text": f'"{pattern}"', "context": m.group(0)[:120]})
        if '^' in pattern[1:]:  # ^ in middle (less common)
            for mm in re.finditer(r'\^', pattern[1:]):
                matches.append({"pattern_type": "2_anchor_^_internal", "match_text": pattern, "context": m.group(0)[:120]})
    return matches

def detect_character_classes(command):
    """Detect regex character classes: [abc], [A-Z], [0-9], [[:alpha:]], etc.
       In qi, [...] is literal text."""
    matches = []
    # Look for character class patterns inside quoted args
    # Typical: qi '[A-Z]' or qi "[a-zA-Z]" or qi "[0-9]+"
    for m in re.finditer(r'[\[][a-zA-Z0-9\[\]:.^@!\-\\\s]+[\]]', command):
        text = m.group(0)
        # Skip if it looks like a file path bracket (e.g., [.../...])
        # or if it's inside a qi --within [...] (which is valid qi syntax for file content scope)
        # Key signals of regex char class: contains ranges like a-z, A-Z, 0-9, or POSIX [:...:]
        if (re.search(r'[a-zA-Z]-[a-zA-Z]', text) or
            re.search(r'[0-9]-[0-9]', text) or
            re.search(r'\[:.*?:\]', text)):
            matches.append({"pattern_type": "3_char_class", "match_text": text, "context": m.group(0)})
        # Also catch simple [abc] style - but need to avoid qi's --within [...]
        elif re.search(r'\[[a-zA-Z0-9]+\]', text) and len(text) <= 10:
            # Simple char class like [abc] (but not [foo/bar] paths)
            if '/' not in text and '\\' not in text:
                matches.append({"pattern_type": "3_char_class_simple", "match_text": text, "context": m.group(0)})
    return matches

def detect_quantifiers(command):
    """Detect regex quantifiers: +, ? (after a char), {n}, {n,m}
       In qi, these are literal unless after certain contexts."""
    matches = []
    # + after a character/group: e.g., [a-z]+, foo+, .+
    for m in re.finditer(r'[)\]a-zA-Z0-9_]\s*\+\s*', command):
        ctx_start = max(0, m.start() - 20)
        ctx_end = min(len(command), m.end() + 20)
        ctx = command[ctx_start:ctx_end]
        # Avoid + as file path separator or in URLs
        if '://' not in ctx and 'q+' not in ctx:
            matches.append({"pattern_type": "4_quantifier_+", "match_text": m.group(0).strip(), "context": ctx})
    # ? after a character (not in URLs or q? patterns)
    for m in re.finditer(r'[a-zA-Z0-9_\]\)]\s*\?\s*', command):
        ctx_start = max(0, m.start() - 20)
        ctx_end = min(len(command), m.end() + 20)
        ctx = command[ctx_start:ctx_end]
        if '://' not in ctx and '--' not in ctx:
            matches.append({"pattern_type": "4_quantifier_?", "match_text": m.group(0).strip(), "context": ctx})
    # {n} or {n,m} quantifier
    for m in re.finditer(r'\{[\d]+(?:,[\d]*)?\}', command):
        matches.append({"pattern_type": "4_quantifier_{n,m}", "match_text": m.group(0), "context": m.group(0)})
    return matches

def detect_dot_wildcard(command):
    """Detect .* used as a wildcard pattern (grep .*)
       In qi, . is literal. * alone is supported as qi wildcard.
       .* is grep-style "match anything" -- won't work in qi."""
    matches = []
    for m in re.finditer(r'\.\*', command):
        start = max(0, m.start() - 30)
        end = min(len(command), m.end() + 30)
        ctx = command[start:end]
        matches.append({"pattern_type": "5_dot_wildcard_.*", "match_text": ".*", "context": ctx})
    return matches

def detect_regex_grouping(command):
    """Detect regex grouping: (foo|bar) parentheses with pipe inside.
       In qi, () and | are literal."""
    matches = []
    for m in re.finditer(r'\([^)]*\|[^)]*\)', command):
        matches.append({"pattern_type": "6_regex_grouping_(...|...)", "match_text": m.group(0), "context": m.group(0)})
    return matches

def detect_backslash_classes(command):
    """Detect backslash character classes: \w, \s, \d, \b, \W, \S, \D, \B
       In qi, these are literal text \w, not word-char class."""
    matches = []
    for m in re.finditer(r'\\[wsdbrnWSDBRNt]', command):
        text = m.group(0)
        # Skip \n, \r, \t - these are common escape sequences that grep also uses
        if text in (r'\n', r'\r', r'\t'):
            continue
        if text in (r'\w', r'\s', r'\d', r'\b', r'\W', r'\S', r'\D', r'\B'):
            start = max(0, m.start() - 20)
            end = min(len(command), m.end() + 20)
            ctx = command[start:end]
            matches.append({"pattern_type": f"7_backslash_class_{text}", "match_text": text, "context": ctx})
        # Also catch \1, \2 backreferences
    for m in re.finditer(r'\\[1-9]', command):
        start = max(0, m.start() - 20)
        end = min(len(command), m.end() + 20)
        ctx = command[start:end]
        matches.append({"pattern_type": f"7_backslash_ref_{m.group(0)}", "match_text": m.group(0), "context": ctx})
    return matches

def detect_grep_v_flag(command):
    """Detect grep -v style: qi -v ... where -v means 'verbose' in qi, not invert.
       The agent might use -v expecting grep's invert-match behavior."""
    matches = []
    # Look for qi -v "pattern" -- this means verbose in qi, not invert
    # We look for patterns where -v is used and the agent seems to want inverted match
    for m in re.finditer(r'qi\s+-v\s', command):
        start = max(0, m.start() - 20)
        end = min(len(command), m.end() + 60)
        ctx = command[start:end]
        matches.append({"pattern_type": "8_grep_-v_(qi_verbose)", "match_text": "-v", "context": ctx})
    # Also check for --invert-match or --invert flags
    for m in re.finditer(r'--invert(?:-match)?', command):
        matches.append({"pattern_type": "8_grep_--invert-match", "match_text": m.group(0), "context": m.group(0)})
    return matches

def detect_grep_c_flag(command):
    """Detect grep -c flag (count) -- qi has no -c flag."""
    matches = []
    for m in re.finditer(r'qi\s+-c\s', command):
        start = max(0, m.start() - 20)
        end = min(len(command), m.end() + 60)
        ctx = command[start:end]
        matches.append({"pattern_type": "9_grep_-c_count", "match_text": "-c", "context": ctx})
    return matches

def detect_grep_i_flag(command):
    """Detect grep -i flag (case-insensitive) -- qi IS case-insensitive by default.
       Using -i is redundant but not harmful. Still a grep-ism."""
    matches = []
    for m in re.finditer(r'qi\s+-i\s', command):
        start = max(0, m.start() - 20)
        end = min(len(command), m.end() + 60)
        ctx = command[start:end]
        matches.append({"pattern_type": "10_grep_-i_case-insensitive", "match_text": "-i", "context": ctx})
    return matches

def detect_grep_w_flag(command):
    """Detect grep -w flag (word boundary) -- qi has no -w flag.
       qi does have --w (whole-word), but -w is not the same.
       Let's check both."""
    matches = []
    # qi -w (single dash) might be a flag or part of pattern depending on context
    for m in re.finditer(r'qi\s+.*?\s-w\s', command):
        start = max(0, m.start() - 20)
        end = min(len(command), m.end() + 60)
        ctx = command[start:end]
        matches.append({"pattern_type": "11_grep_-w_word_boundary", "match_text": "-w", "context": ctx})
    return matches

ALL_DETECTORS = [
    detect_alternation,
    detect_anchors,
    detect_character_classes,
    detect_quantifiers,
    detect_dot_wildcard,
    detect_regex_grouping,
    detect_backslash_classes,
    detect_grep_v_flag,
    detect_grep_c_flag,
    detect_grep_i_flag,
    detect_grep_w_flag,
]

# ============================================================
# Main analysis
# ============================================================

def get_short_name(filepath):
    """Extract batch name from filepath."""
    parts = filepath.split('/')
    for p in parts:
        if p.startswith('pro_'):
            return p
    return os.path.basename(os.path.dirname(filepath))

def main():
    all_results = defaultdict(list)  # pattern_type -> list of (batch, command)

    for csv_file in CSV_FILES:
        batch = get_short_name(csv_file)
        if not os.path.exists(csv_file):
            print(f"WARNING: File not found: {csv_file}", file=sys.stderr)
            continue

        with open(csv_file, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row_num, row in enumerate(reader, start=2):
                tool = row.get('tool', '').strip()
                command = row.get('command', '').strip()
                instance = row.get('instance', '').strip()
                run_id = row.get('run_id', '').strip()
                turn_idx = row.get('turn_idx', '')

                # Focus on qi commands
                if tool != 'qi':
                    continue

                if not command:
                    continue

                # Run all detectors
                for detector in ALL_DETECTORS:
                    matches = detector(command)
                    for m in matches:
                        m['batch'] = batch
                        m['instance'] = instance
                        m['run_id'] = run_id
                        m['turn_idx'] = turn_idx
                        m['full_command'] = command[:300]
                        all_results[m['pattern_type']].append(m)

    # ============================================================
    # Report
    # ============================================================
    print("=" * 80)
    print("  GREP-ISM ANALYSIS OF qi COMMANDS IN PRO EXPERIMENTS")
    print("=" * 80)
    print()

    total_qi_commands = 0
    for csv_file in CSV_FILES:
        batch = get_short_name(csv_file)
        if os.path.exists(csv_file):
            with open(csv_file, 'r') as f:
                reader = csv.DictReader(f)
                count = sum(1 for row in reader if row.get('tool', '').strip() == 'qi')
                total_qi_commands += count
                print(f"  {batch}: {count} qi commands")
    print(f"  TOTAL: {total_qi_commands} qi commands across all batches")
    print()

    # Group results by pattern type family
    families = {
        "1. Alternation (\\|)": [],
        "2. Anchors (^, $)": [],
        "3. Character Classes ([...])": [],
        "4. Quantifiers (+, ?, {n,m})": [],
        "5. Dot Wildcard (.*)": [],
        "6. Regex Grouping ((...|...))": [],
        "7. Backslash Classes (\\w, \\s, \\d, \\b)": [],
        "8. grep -v (invert vs verbose)": [],
        "9. grep -c (count)": [],
        "10. grep -i (redundant case-insensitive)": [],
        "11. grep -w (word boundary)": [],
    }

    for pattern_type, entries in sorted(all_results.items()):
        prefix = pattern_type[0]
        for family_name in families:
            if pattern_type.startswith(family_name[0:2]):
                families[family_name].extend(entries)
                break

    # Also compute per-family unique command count
    for family_name, entries in families.items():
        if not entries:
            print(f"\n--- {family_name} ---")
            print("  NO MATCHES FOUND")
            continue

        print(f"\n{'─' * 80}")
        print(f"  {family_name}")
        print(f"  Total matches: {len(entries)}")
        print(f"{'─' * 80}")

        # Group by batch
        by_batch = defaultdict(list)
        for e in entries:
            by_batch[e['batch']].append(e)

        for batch, batch_entries in sorted(by_batch.items()):
            print(f"\n  Batch: {batch} ({len(batch_entries)} matches)")
            # Show up to 5 representative examples
            shown = set()
            count = 0
            for e in batch_entries:
                if count >= 5:
                    break
                key = e.get('full_command', '')[:120]
                if key in shown:
                    continue
                shown.add(key)
                count += 1
                print(f"    [{e['run_id']}, turn {e['turn_idx']}] {e.get('full_command', '')[:200]}")
                if 'context' in e and e['context'] != e.get('full_command', ''):
                    print(f"      match context: ...{e['context']}...")

            if len(batch_entries) > 5:
                remaining = len(batch_entries) - len(shown)
                # Show up to 3 more unique
                for e in batch_entries:
                    if count >= 8:
                        break
                    key = e.get('full_command', '')[:120]
                    if key in shown:
                        continue
                    shown.add(key)
                    count += 1
                    print(f"    [{e['run_id']}, turn {e['turn_idx']}] {e.get('full_command', '')[:200]}")
                if len(batch_entries) > len(shown):
                    print(f"    ... and {len(batch_entries) - len(shown)} more similar matches")

    # ============================================================
    # Summary table
    # ============================================================
    print(f"\n{'=' * 80}")
    print("  SUMMARY TABLE")
    print(f"{'=' * 80}")
    print(f"  {'Grep-ism':<45} {'Matches':>8} {'Unique Cmds':>12} {'Batches':>15}")
    print(f"  {'-'*80}")
    for family_name, entries in families.items():
        short_name = family_name.split(". ", 1)[1] if ". " in family_name else family_name
        unique_cmds = len(set(e.get('full_command', '') for e in entries))
        batches = ", ".join(sorted(set(e['batch'] for e in entries))) if entries else ""
        batches_short = batches[:40] + "..." if len(batches) > 40 else batches
        print(f"  {family_name:<45} {len(entries):>8} {unique_cmds:>12} {batches_short:>15}")

    # ============================================================
    # Per-batch summary
    # ============================================================
    print(f"\n{'=' * 80}")
    print("  PER-BATCH BREAKDOWN")
    print(f"{'=' * 80}")
    for batch in sorted(set(get_short_name(f) for f in CSV_FILES)):
        batch_matches = {}
        for family_name, entries in families.items():
            batch_entries = [e for e in entries if e['batch'] == batch]
            if batch_entries:
                batch_matches[family_name] = len(batch_entries)

        qi_count = 0
        for csv_file in CSV_FILES:
            if get_short_name(csv_file) == batch and os.path.exists(csv_file):
                with open(csv_file, 'r') as f:
                    qi_count = sum(1 for row in csv.DictReader(f) if row.get('tool', '').strip() == 'qi')

        print(f"\n  {batch} (qi commands: {qi_count}):")
        if not batch_matches:
            print("    No grep-isms found!")
        else:
            for family_name, count in sorted(batch_matches.items(), key=lambda x: -x[1]):
                short = family_name.split(". ", 1)[1] if ". " in family_name else family_name
                print(f"    {short}: {count}")

    print(f"\n{'=' * 80}")
    print("  END OF REPORT")
    print(f"{'=' * 80}")

if __name__ == '__main__':
    main()
