# Instance Qi-Fit Assessment

Analysis of the 11 SWE-bench Pro instances indexed in `experiment/dbs/`, ranked by
qi-friendliness. Each entry notes the structural pattern, file count, test counts,
and whether the task plays to qi's strengths (symbol-based navigation, cross-package
call tracing, interface/implementation discovery) or grep's (string literals,
same-directory edits).

## Strong Qi Fit

### vuls (`future-architect/vuls`)
- **files**: 7 (config, models, report, scan ×3)
- **f2p**: 6 | **p2p**: 0 | **lines**: 122 | **lang**: Go
- **pattern**: Struct-heavy change (image digest support) rippling across 4 packages.
  `config/config.go` → `models/scanresults.go` → `scan/base.go` → `report/report.go`.
  qi traces the type definition through its callers; grep drowns in "digest" noise
  across comments, error messages, and unrelated parsing code.

### navidrome #1 (`navidrome/navidrome`)
- **files**: 9 | **f2p**: 3 | **p2p**: 0 | **lines**: 477 | **lang**: Go
- **pattern**: Interface change in `core/agents/interfaces.go` rippling to 3 agent
  implementations (lastfm, listenbrainz, spotify). qi finds all interface implementors
  in one query; grep must find each by name.

### nodebb (`NodeBB/NodeBB`)
- **files**: 11 | **f2p**: 3 | **p2p**: 288 | **lines**: 334 | **lang**: JS
- **pattern**: Parallel database backends (mongo, postgres, redis) needing identical
  changes. Cross-cutting from API → controllers → 3 DB implementations. qi discovers
  all three by type/symbol; grep risks missing one. Heavy p2p suite (288) means slow eval.

## Good Qi Fit

### ansible (`ansible/ansible`)
- **files**: 3 | **f2p**: 15 | **p2p**: 0 | **lines**: 128 | **lang**: Python
- **pattern**: Wrapper-function addition + call-site update. `_parse_clixml` → new
  `_replace_stderr_clixml`. Agent traces symbol from import site to definition, adds
  wrapper, updates callers. qi finds the symbol chain cleanly; grep hits CLIXML noise
  in comments, strings, and regex byte patterns.

### teleport (`gravitational/teleport`)
- **files**: 4 | **f2p**: 4 | **p2p**: 0 | **lines**: 109 | **lang**: Go
- **pattern**: Auth-layer call chain: `lib/web/sessions.go` → `lib/web/apiserver.go`
  → `lib/auth/apiserver.go` → `lib/auth/auth.go`. qi traces the method through the
  call graph; grep must guess at the chain.

### tutanota (`tutao/tutanota`)
- **files**: 3 | **f2p**: 1 | **p2p**: 0 | **lines**: 106 | **lang**: TS
- **pattern**: Cache/storage layer with interface + 2 implementations
  (`OfflineStorage`, `DefaultEntityRestCache`, `EphemeralCacheStorage`). TypeScript
  tree-sitter grammar is mature. Single f2p — cheap smoke test. Pulled and ready.

## Moderate Qi Fit

### navidrome #2 (`navidrome/navidrome`)
- **files**: 4 | **f2p**: 1 | **p2p**: 0 | **lines**: 100 | **lang**: Go
- **pattern**: Config change rippling: `conf/configuration.go` → `core/agents/lastfm.go`
  + `spotify.go` → `server/initial_setup.go`. Cross-package but only 1 f2p — limited
  scope for qi to differentiate.

### openlibrary (`internetarchive/openlibrary`)
- **files**: 8 (4 code + 4 static assets) | **f2p**: 4 | **p2p**: 7 | **lines**: 251
  | **lang**: Python
- **pattern**: New methods (`_get_wikipedia_link`, `_get_statement_values`,
  `get_external_profiles`) added to `WikidataEntity` class. The class structure helps
  qi locate the insertion point, but half the patch is SVGs, CSS, and i18n — assets
  qi can't help with. Already run with haiku, glm-5.2, and mimo.

### qutebrowser (`qutebrowser/qutebrowser`)
- **files**: 3 | **f2p**: 4 | **p2p**: 52 | **lines**: 109 | **lang**: Python
- **pattern**: Logging/download infrastructure across `browser/` and `utils/` packages.
  Symbol-heavy (log classes/methods), cross-package. Already run with haiku and glm-5.2.

## Weak Qi Fit

### webclients (`protonmail/webclients`)
- **files**: 2 | **f2p**: 2 | **p2p**: 0 | **lines**: 100 | **lang**: TS
- **pattern**: Both files in same directory, same component prefix. Email recipient
  parsing (regex/string manipulation). Trivial for grep — qi's multi-file advantage
  wasted. Already run with deepseek-v4-pro.

### flipt (`flipt-io/flipt`)
- **files**: 5 | **f2p**: 18 | **p2p**: 0 | **lines**: 1700 | **lang**: Go
- **pattern**: Feature-flag toggling where key terms appear as string literals, not
  symbols. Proto + generated .pb.go + swagger — grep matches the flag name across all
  files instantly. Diagnosed as structurally the poorest qi fit in the pool.
