# Release Builds

Documentation for the tooling that produces SourceMinder's downloadable
release binaries. For a normal source build (pick your languages, clone only
the grammars you need), follow the README — none of this is required.

## fetch-grammars.sh

Fetches all seven tree-sitter grammar repos into the repo root at **pinned
commits** — the exact versions the indexers and golden tests were validated
against. Used by the release workflow, and handy for anyone who wants a
reproducible all-language source build:

```
./fetch-grammars.sh          # fetch anything missing; verify existing checkouts
./fetch-grammars.sh --force  # re-fetch checkouts that are at the wrong commit
```

Behavior:

- Each grammar is fetched shallow (single commit) into `./tree-sitter-<lang>`.
- A checkout already at its pinned commit is left untouched.
- A checkout at a *different* commit is reported and skipped, and the script
  exits nonzero; `--force` re-fetches it to the pin (discarding local state).
  A grammar developer's working clones are never silently clobbered.

### Pinned versions

| Grammar | Pin | Version |
|---|---|---|
| tree-sitter-c | `ae19b676` | v0.24.1 + 2 |
| tree-sitter-go | `2346a3ab` | v0.25.0 + 2 |
| tree-sitter-perl | `67887d64` (release branch) | v1.0.0 + 5 |
| tree-sitter-php | `3f2465c2` | v0.24.2 + 14 |
| tree-sitter-python | `26855eab` | v0.25.0 + 1 |
| tree-sitter-rust | `77a37472` | v0.24.2 |
| tree-sitter-typescript | `75b3874e` | v0.23.2 + 5 |

("+ N" = commits past that tag on the default branch at pin time.)

**Why perl pins the `release` branch:** tree-sitter-perl does not commit the
generated `src/parser.c` on its main branch — generating it requires the
tree-sitter CLI (and Node for `grammar.js` changes). Upstream publishes
pre-generated snapshots on a `release` branch; each commit there is a
`deploy: <main-sha>` of a main commit. Our pin `67887d64` is the deploy of
main `8917c6e9`, the validated grammar version, so no generation step or
extra toolchain is needed anywhere in the pipeline.

**Bumping a pin** means re-validating that language's indexer (build, then
golden tests) before the new pin ships in a release.

## configure --portable

Release builds must use `./configure --enable-all --portable`. The default
release CFLAGS include `-march=native -mtune=native`, which tune the binary
to the build machine's CPU — fine for local builds, but a distributed binary
built that way can crash with an illegal instruction on older hardware.
`--portable` drops those two flags so the binary runs on any CPU of the
target architecture. The configure summary reports which mode is active
("CPU tuning: native/portable").

## release.sh

Builds the distributable static Linux tarballs. Host-neutral: everything
runs in a pinned Alpine container (`tools/release-alpine.Dockerfile` —
alpine 3.22, static tree-sitter runtime v0.26.8) driven by podman or
docker, so a dev box and any CI produce identical artifacts. Like the
grammar pins, the tree-sitter runtime version is validated: bumping it in
the Dockerfile means re-validating the indexers.

The build input is a clean `git archive HEAD` export — the working tree
cannot leak into an artifact.

    ./release.sh                     # release build, both arches
    ./release.sh --arch x86_64       # one arch
    ./release.sh --snapshot          # test build, no tag required
    ./release.sh --keep-work         # keep the temp build dir

A release build requires HEAD to be at an annotated tag `v<VERSION>`
matching VERSION in `shared/constants.h`, with a clean working tree, and
produces `dist/sourceminder-<version>-linux-<arch>.tar.gz` (the eight
binaries, LICENSE, README.md) plus `dist/checksums.txt`. Snapshot builds
are for iterating on the pipeline itself: they skip the tag requirement and
name artifacts with the commit (`...-g<sha>-...`) so they can never be
mistaken for releases.

Each build is smoke-tested before packaging: every binary's `--version`
must match VERSION; every indexer's `--show-config` must succeed in an
empty directory (exercising the embedded config); and a sample C file is
indexed and queried back through `qi`. Native-arch binaries are tested on
the host — an Alpine-built static binary running on a glibc distro is the
portability proof. Foreign arches (aarch64 on an x86_64 host) build and
test in-container via qemu binfmt; the script prints one-time setup
instructions if qemu is missing.

## Release pipeline (see STATIC_RELEASE_PLAN.md)

1. `fetch-grammars.sh` — pinned grammar sources (this document, above).
2. `release.sh` — static musl binaries for Linux x86_64/aarch64, smoke
   tests, tarballs + checksums in `dist/` (this document, above). macOS
   builds are not yet covered (require a Mac in the loop).
3. `install.sh` — user-facing installer:
   `curl -fsSL https://sourceminder.org/install.sh | sh`. Resolves the
   latest tag via the `/releases/latest` redirect (pin with
   `SOURCEMINDER_VERSION=vX.Y.Z`), verifies sha256 against checksums.txt,
   extracts the eight binaries to `~/.local/bin` (`PREFIX=/usr/local` for
   system-wide). Everything is served directly from sourceminder.org — no
   code forge in the install path. The URL contract (documented in the
   script header) is small enough that any static host plus one redirect
   rule can serve it.

## Publishing a release

sourceminder.org serves the install path directly (no forge involved):

    /install.sh                          the installer script
    /releases/download/<tag>/<file>      tarballs + checksums.txt
    /releases/latest                     302 → /releases/tag/<tag>
    /releases/tag/<tag>                  must return 200 (any content --
                                         install.sh parses the tag from the
                                         redirect target's final URL, but
                                         curl -f fails if that URL 404s)

Per release:

1. Bump VERSION in `shared/constants.h`, commit, `git tag -a v<VERSION>`.
2. `./release.sh` — builds, smoke-tests, packages into `dist/`.
3. Upload `dist/*.tar.gz` + `dist/checksums.txt` to
   `sourceminder.org/releases/download/v<VERSION>/`.
4. Create (or leave a stub at) `/releases/tag/v<VERSION>` and point the
   `/releases/latest` redirect at it.
5. If install.sh changed since the last release, upload the new copy to
   `sourceminder.org/install.sh` (the repo copy is the source of truth).
