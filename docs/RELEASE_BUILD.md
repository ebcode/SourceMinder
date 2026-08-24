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

> **You do not run this yourself for a release.** `release.sh` runs
> `fetch-grammars.sh`, `./configure --enable-all --portable`, and `make static`
> for you, inside the pinned container, against a clean `git archive HEAD`
> export. A local `./configure` / `make` has no effect on the release
> artifacts — it's only for normal dev builds. This section documents the flag
> the pipeline uses internally.

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

**Prerequisites** (before running `./release.sh`):

- **podman or docker** installed and runnable — the whole build happens in the
  container; nothing is compiled on the host.
- **For a cross-arch Linux build** (aarch64 Linux on an x86_64 Linux host), two
  one-time host prerequisites — release.sh checks both and fails early with
  instructions if either is missing:
  - qemu binfmt, to run the foreign binaries (smoke tests execute them):
    `docker run --privileged --rm tonistiigi/binfmt --install arm64`
    (or `sudo apt install qemu-user-static binfmt-support`).
  - docker buildx, to build the foreign image: `sudo apt install docker-buildx`.
    The legacy docker builder silently ignores `--platform` and builds the host
    arch, producing a mislabeled tarball; buildx (BuildKit) honors it. podman
    handles `--platform` natively and needs no buildx.

  This covers aarch64 Linux only. macOS (arm64) and Windows-on-ARM are separate
  OS targets — different binary format and libc — that qemu/buildx cannot
  produce from the Alpine container; they need native toolchains and are out of
  scope for `release.sh` (see the macOS/Windows notes in STATIC_RELEASE_PLAN.md).
- **Your changes committed.** The build input is `git archive HEAD`, so
  anything uncommitted (or unstaged) is *not* in the artifacts. Commit first.
- **A clean working tree**, and for a real (non-`--snapshot`) build, **HEAD at
  an annotated tag `v<VERSION>`** matching VERSION in `shared/constants.h`.
- That's it — you do **not** run `./configure` or `make` yourself; `release.sh`
  runs `fetch-grammars.sh` + `configure --enable-all --portable` + `make static`
  in the container.

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

## release-macos.sh

Builds the distributable macOS tarball. Must run on a Mac — macOS cannot be
cross-compiled from Linux (Mach-O format, different ABI, no static libSystem),
so this script builds the host architecture only. Run it on an Apple-silicon
Mac for the arm64 tarball, and on an Intel Mac for the x86_64 tarball.

Unlike the Linux binaries, macOS binaries are not fully static: they
dynamically link Apple's system libraries (`/usr/lib/libSystem` and friends),
which ship with every macOS. tree-sitter and sqlite are statically linked from
pinned source, so recipients need no Homebrew or other third-party packages.
The tree-sitter pin matches the Linux build (v0.26.8); sqlite is built from the
3.53.4 amalgamation, whereas the Linux/Alpine build uses Alpine's sqlite-static
(currently 3.49.x). The SQLite on-disk format is compatible across both, so an
index DB written on one platform reads on the other.

Prerequisites: Xcode Command Line Tools (`xcode-select --install`) for clang,
make, ar, otool, and unzip. Same clean `git archive HEAD` input and
tag/clean-tree rules as release.sh.

    ./release-macos.sh                            # release build (host arch)
    ./release-macos.sh --snapshot                 # test build, no tag required
    ./release-macos.sh --keep-work                # keep the temp build dir
    ./release-macos.sh --deployment-target 12.0   # oldest macOS to support
                                                  # (default 11.0 arm64,
                                                  #  10.15 x86_64)

Before packaging, the script verifies each binary is the expected architecture
and that `otool -L` reports only Apple system libraries (`/usr/lib`,
`/System/Library`) — a non-system dependency (e.g. a Homebrew dylib) fails the
build. It then runs the same smoke test as release.sh (versions, empty-dir
`--show-config`, index a sample and query it back). Output:
`dist/sourceminder-<version>-macos-<arch>.tar.gz` plus `dist/checksums.txt`.

## Release pipeline (see STATIC_RELEASE_PLAN.md)

1. `fetch-grammars.sh` — pinned grammar sources (this document, above).
2. `release.sh` — static musl binaries for Linux x86_64/aarch64, smoke
   tests, tarballs + checksums in `dist/` (this document, above).
   `release-macos.sh` — the macOS equivalent, run natively on a Mac
   (arm64, and an Intel Mac for x86_64).
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
2. Build the tarballs on each platform, from the same tag:
   - `./release.sh` on Linux — Linux x86_64 + aarch64.
   - `./release-macos.sh` on a Mac (Apple silicon for arm64; an Intel Mac
     for x86_64).
3. Collect every tarball into one directory and generate a single
   `checksums.txt` over the full set: `sha256sum -- *.tar.gz > checksums.txt`.
   Each build script only checksums the tarballs in its own `dist/`, so those
   per-machine files are partial — the hosted checksums.txt must list every
   asset install.sh might download, or the install aborts on a missing hash.
   Any one machine can do this: sha256 is computed over the raw bytes, so
   running `sha256sum` on the Linux box over a macOS tarball (copied over as
   binary) yields the same digest a Mac's `shasum -a 256` would, and
   install.sh verifies with either tool.
4. Upload `*.tar.gz` + `checksums.txt` to
   `sourceminder.org/releases/download/v<VERSION>/`.
5. Create (or leave a stub at) `/releases/tag/v<VERSION>` and point the
   `/releases/latest` redirect at it.
6. If install.sh changed since the last release, upload the new copy to
   `sourceminder.org/install.sh` (the repo copy is the source of truth).
