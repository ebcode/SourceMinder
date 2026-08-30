# Repository Guidance

At the start of every session working in this repository, read these documents in full and use them as governing design guidance for all analysis, code changes, reviews, and recommendations:

- [system-design-precepts.md](./system-design-precepts.md)
- [software-design-precepts.md](./software-design-precepts.md)

These documents are not optional background reading. They define the expected engineering standards for this repository.

Apply them concretely:

- Treat system behavior under failure, concurrency, networking, persistence, and observability as first-class design concerns.
- Treat data formats, schemas, and interfaces as long-lived contracts.
- Prefer simple, deep modules with strong boundaries and hidden implementation details.
- Eliminate accidental complexity, refactor when design debt appears, and avoid pushing complexity to callers.
- Measure and verify behavior instead of assuming it.

If a requested change appears to conflict with these precepts, call out the conflict explicitly and propose an approach that satisfies the request while preserving the design principles where possible.

---

## Session-learned guidance

- **Avoid blanket `sed -i` path-prefix rewrites.** A `sed -i 's|experiment/||g'` sweep over code + docs looked mechanical but silently mangled prose ("the `experiment/` directory" → "the `` directory"), comments, and a migration-plan doc that was supposed to be a historical record of the move. Cheap to miss, costly to repair.
  - Prefer targeted `edit`/`rg -n`-reviewed changes; if a sweep is truly warranted, first confirm the token only appears as the intended prefix (e.g. `rg -n` and inspect every hit), exclude files that must stay historically accurate, and diff/verify after (`py_compile`, `bash -n`, re-grep).
  - When de-prefixing paths after a repo move, review *each* match for prose vs. path before rewriting.
