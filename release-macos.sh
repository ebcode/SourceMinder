#!/bin/sh
# release-macos.sh - build distributable SourceMinder macOS binaries.
#
# Must be run ON a Mac (native build). macOS cannot be cross-compiled from
# Linux (Mach-O format, different ABI, no static libSystem), so this script
# builds the host architecture only. Every binary still dynamically links
# Apple's system libraries (/usr/lib/libSystem.B.dylib and friends) -- those
# ship with every macOS install -- but tree-sitter and sqlite are statically
# linked in from pinned source, so recipients need NO Homebrew or other
# third-party packages.
#
# Like release.sh, the build input is a clean `git archive HEAD` export --
# working-tree state cannot leak into an artifact.
#
# Usage: ./release-macos.sh [--snapshot] [--keep-work]
#                           [--deployment-target NN.NN]
#
#   Release mode (default): HEAD must be at an annotated tag v<VERSION>
#   matching VERSION in shared/constants.h, with a clean working tree.
#   Artifacts: dist/sourceminder-<version>-macos-<arch>.tar.gz
#
#   --snapshot            for iterating on the pipeline itself: no tag
#                         required; artifacts are named with the commit
#                         (...-g<sha>-...) so they cannot be mistaken for
#                         release tarballs.
#   --keep-work           keep the temporary build directory for inspection.
#   --deployment-target   oldest macOS the binaries must run on (default:
#                         11.0 for arm64, 10.15 for x86_64).

set -eu

SNAPSHOT=0
KEEP_WORK=0

case "$(uname -m)" in
    arm64)  DEPLOY_TARGET=11.0 ;;
    x86_64) DEPLOY_TARGET=10.15 ;;
    *) echo "Error: unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac

while [ $# -gt 0 ]; do
    case "$1" in
        --snapshot) SNAPSHOT=1 ;;
        --keep-work) KEEP_WORK=1 ;;
        --deployment-target) shift; DEPLOY_TARGET="${1:?--deployment-target needs a value}" ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

# macOS-only by design: native build.
if [ "$(uname -s)" != "Darwin" ]; then
    echo "Error: release-macos.sh must run on macOS (this is $(uname -s))." >&2
    echo "       macOS cannot be cross-compiled from Linux; use release.sh for Linux." >&2
    exit 1
fi

# Xcode Command Line Tools provide clang, make, ar, unzip.
if ! command -v clang >/dev/null 2>&1; then
    echo "Error: clang not found. Install Xcode Command Line Tools:" >&2
    echo "  xcode-select --install" >&2
    exit 1
fi
for tool in make git curl unzip ar cc; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Error: '$tool' not found" >&2; exit 1; }
done

cd "$(dirname "$0")"

VERSION=$(sed -n 's/^#define VERSION "\(.*\)"$/\1/p' shared/constants.h)
if [ -z "$VERSION" ]; then
    echo "Error: cannot read VERSION from shared/constants.h" >&2
    exit 1
fi

if [ $SNAPSHOT -eq 1 ]; then
    LABEL="$VERSION-g$(git rev-parse --short HEAD)"
    if ! git diff-index --quiet HEAD --; then
        echo "Warning: working tree is dirty; snapshot builds from HEAD only --" >&2
        echo "         uncommitted changes are NOT included." >&2
    fi
else
    if ! git diff-index --quiet HEAD --; then
        echo "Error: working tree is dirty. Commit first, or use --snapshot." >&2
        exit 1
    fi
    if ! TAG=$(git describe --exact-match 2>/dev/null); then
        echo "Error: HEAD is not at an annotated tag. Tag the release first:" >&2
        echo "  git tag -a v$VERSION -m 'release v$VERSION'" >&2
        echo "or use --snapshot for a test build." >&2
        exit 1
    fi
    if [ "$TAG" != "v$VERSION" ]; then
        echo "Error: tag $TAG does not match VERSION $VERSION in shared/constants.h" >&2
        exit 1
    fi
    LABEL="$VERSION"
fi

HOST_ARCH=$(uname -m)
TOOLS="index-c index-go index-perl index-php index-python index-rust index-ts qi"

WORK=$(mktemp -d)
cleanup() {
    if [ $KEEP_WORK -eq 1 ]; then
        echo "Work dir kept: $WORK"
    else
        rm -rf "$WORK"
    fi
}
trap cleanup EXIT

echo "==> Exporting HEAD ($(git rev-parse --short HEAD)) to $WORK/src"
mkdir -p "$WORK/src"
git archive HEAD | tar -x -C "$WORK/src"

# Static toolchain, built from pinned source into a staging prefix. No
# Homebrew involved: the binaries' only dynamic dependencies must be Apple's
# system libraries, which every macOS ships.
PREFIX="$WORK/prefix"
mkdir -p "$PREFIX/include" "$PREFIX/lib"
export MACOSX_DEPLOYMENT_TARGET="$DEPLOY_TARGET"

TREE_SITTER_VERSION=v0.26.8   # same pin as tools/release-alpine.Dockerfile
SQLITE_VERSION=3530400        # sqlite 3.53.4 (amalgamation archive suffix)
SQLITE_URL="https://www.sqlite.org/2026/sqlite-amalgamation-$SQLITE_VERSION.zip"

echo "==> Building pinned tree-sitter runtime $TREE_SITTER_VERSION (deploy target $DEPLOY_TARGET)"
git clone -q --depth 1 --branch "$TREE_SITTER_VERSION" \
    https://github.com/tree-sitter/tree-sitter.git "$WORK/tree-sitter"
make -C "$WORK/tree-sitter" install PREFIX="$PREFIX"
test -f "$PREFIX/lib/libtree-sitter.a"

echo "==> Building pinned sqlite amalgamation 3.53.4 (deploy target $DEPLOY_TARGET)"
mkdir -p "$WORK/sqlite"
curl -fsSL "$SQLITE_URL" -o "$WORK/sqlite/sqlite.zip"
unzip -q "$WORK/sqlite/sqlite.zip" -d "$WORK/sqlite"
SQLITE_SRC="$WORK/sqlite/sqlite-amalgamation-$SQLITE_VERSION"
test -f "$SQLITE_SRC/sqlite3.c"
cc -O2 -c "$SQLITE_SRC/sqlite3.c" -o "$WORK/sqlite/sqlite3.o"
ar rcs "$PREFIX/lib/libsqlite3.a" "$WORK/sqlite/sqlite3.o"
cp "$SQLITE_SRC/sqlite3.h" "$PREFIX/include/"

GRAMMAR_INCLUDES="-Itree-sitter-c/src -Itree-sitter-go/src -Itree-sitter-perl/src \
-Itree-sitter-php/php/src -Itree-sitter-python/src -Itree-sitter-rust/src \
-Itree-sitter-typescript/typescript/src"

echo "==> Fetch grammars, configure --portable, make (static tree-sitter + sqlite)"
(
    cd "$WORK/src"
    sh ./fetch-grammars.sh
    CC=clang sh ./configure --enable-all --portable
    # Override the Homebrew-default include/link paths to point at the staging
    # static libs. INCLUDE_PATHS is expanded lazily inside CFLAGS, so the
    # command-line override is honored. Replacing LDFLAGS statically links
    # tree-sitter + sqlite into each binary (the normal `all` targets -- the
    # Linux-only `make static` does not apply on macOS).
    make -j"$(sysctl -n hw.ncpu)" \
        INCLUDE_PATHS="-I$PREFIX/include -I. $GRAMMAR_INCLUDES" \
        LDFLAGS="$PREFIX/lib/libtree-sitter.a $PREFIX/lib/libsqlite3.a -lm"
)

# Trust nothing: the point of this script is self-contained binaries. otool -L
# prints the binary path on line 1, then every dynamic dependency; the only
# acceptable ones are Apple's system libraries. Also confirm the arch.
echo "==> Verifying architecture and dynamic dependencies"
for tool in $TOOLS; do
    bin="$WORK/src/build/$tool"
    file "$bin" | grep -q "$HOST_ARCH" || { echo "Error: $tool is not a $HOST_ARCH binary:" >&2; file "$bin" >&2; exit 1; }
    bad=$(otool -L "$bin" | awk 'NR>1 && $1 !~ /^\/usr\/lib\// && $1 !~ /^\/System\/Library\// { print $1 }')
    if [ -n "$bad" ]; then
        echo "Error: $tool links non-system libraries:" >&2
        echo "$bad" >&2
        exit 1
    fi
done
echo "OK: all binaries are $HOST_ARCH and depend only on Apple system libraries"

# Smoke test (mirrors release.sh). Native here -- no qemu needed.
cat > "$WORK/smoke.sh" << 'EOF_SMOKE'
#!/bin/sh
set -eu
BIN=$1
EXPECTED=$2
TOOLS="index-c index-go index-perl index-php index-python index-rust index-ts qi"

for tool in $TOOLS; do
    got=$("$BIN/$tool" --version | head -n 1)
    if [ "$got" != "$EXPECTED" ]; then
        echo "SMOKE FAIL: $tool --version printed '$got', expected '$EXPECTED'" >&2
        exit 1
    fi
done

# Everything below runs in a fresh, empty temp dir: the indexers must be
# fully functional there with nothing installed (embedded config).
SMOKEDIR=$(mktemp -d)
trap 'rm -rf "$SMOKEDIR"' EXIT
cd "$SMOKEDIR"

for tool in $TOOLS; do
    case "$tool" in qi) continue ;; esac
    "$BIN/$tool" --show-config > /dev/null
done

# End-to-end index + query. Gate on the match-count line, not the bare symbol
# (qi echoes the query term in its header even on a zero-match run).
cat > sample.c << 'EOF_C'
int release_smoke_marker(int x) { return x + 1; }
EOF_C
"$BIN/index-c" . --once --silent
"$BIN/qi" release_smoke_marker | grep -qE '^Found [1-9]'

echo "smoke OK"
EOF_SMOKE
echo "==> Smoke test"
sh "$WORK/smoke.sh" "$WORK/src/build" "$VERSION"

name="sourceminder-$LABEL-macos-$HOST_ARCH"
echo "==> Packaging dist/$name.tar.gz"
stage="$WORK/$name"
mkdir -p "$stage"
for tool in $TOOLS; do
    cp "$WORK/src/build/$tool" "$stage/$tool"
done
cp LICENSE README.md "$stage/"
mkdir -p dist
tar -czf "dist/$name.tar.gz" -C "$WORK" "$name"

# checksums.txt always covers everything currently in dist/.
(
    cd dist
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -- *.tar.gz > checksums.txt
    else
        shasum -a 256 -- *.tar.gz > checksums.txt
    fi
)

echo ""
echo "Done. Artifacts in dist/:"
cat dist/checksums.txt
