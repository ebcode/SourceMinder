#!/bin/sh
# release.sh - build distributable static Linux release tarballs.
#
# Host-neutral by design: the whole build runs in a pinned Alpine container
# (tools/release-alpine.Dockerfile) driven by podman or docker, so the same
# script produces identical artifacts on a dev box or in any CI. The build
# input is a clean `git archive HEAD` export -- working-tree state cannot
# leak into an artifact.
#
# Usage: ./release.sh [--arch x86_64|aarch64|all] [--snapshot] [--keep-work]
#
#   Release mode (default): HEAD must be at an annotated tag v<VERSION>
#   matching VERSION in shared/constants.h, with a clean working tree.
#   Artifacts: dist/sourceminder-<version>-linux-<arch>.tar.gz
#
#   --snapshot   for iterating on the pipeline itself: no tag required;
#                artifacts are named with the commit (...-g<sha>-...) so
#                they cannot be mistaken for release tarballs.
#   --keep-work  keep the temporary build directory for inspection.
#
# Cross-arch builds (aarch64 on an x86_64 host) need one-time qemu binfmt
# registration; the script prints setup instructions if it is missing.

set -eu

ARCH=all
SNAPSHOT=0
KEEP_WORK=0

while [ $# -gt 0 ]; do
    case "$1" in
        --arch) shift; ARCH="${1:?--arch needs a value}" ;;
        --snapshot) SNAPSHOT=1 ;;
        --keep-work) KEEP_WORK=1 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

case "$ARCH" in
    x86_64|aarch64|all) ;;
    *) echo "Error: --arch must be x86_64, aarch64, or all" >&2; exit 1 ;;
esac

# Container engine: podman preferred (rootless), docker fallback.
if command -v podman >/dev/null 2>&1; then
    ENGINE=podman
elif command -v docker >/dev/null 2>&1; then
    ENGINE=docker
else
    echo "Error: podman or docker is required" >&2
    exit 1
fi

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

# Smoke-test script. Run on the host for the native arch (proving the
# musl-static binaries work outside Alpine -- the point of the exercise)
# and inside the container (qemu) for foreign arches.
# Args: $1 = dir containing the *-static binaries, $2 = expected version.
cat > "$WORK/smoke.sh" << 'EOF_SMOKE'
#!/bin/sh
set -eu
BIN=$1
EXPECTED=$2
TOOLS="index-c index-go index-perl index-php index-python index-rust index-ts qi"

for tool in $TOOLS; do
    got=$("$BIN/$tool-static" --version | head -n 1)
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
    "$BIN/$tool-static" --show-config > /dev/null
done

# End-to-end index + query: sample.c (the only file in this dir) defines
# release_smoke_marker; after indexing, qi must find that C symbol.
cat > sample.c << 'EOF_C'
int release_smoke_marker(int x) { return x + 1; }
EOF_C
"$BIN/index-c-static" . --once --silent
"$BIN/qi-static" release_smoke_marker | grep -q release_smoke_marker

echo "smoke OK"
EOF_SMOKE

build_arch() {
    arch=$1
    case "$arch" in
        x86_64)  platform=linux/amd64 ;;
        aarch64) platform=linux/arm64 ;;
    esac

    if [ "$arch" != "$HOST_ARCH" ] && [ ! -e "/proc/sys/fs/binfmt_misc/qemu-$arch" ]; then
        echo "Error: cross-building $arch needs qemu binfmt. One-time setup:" >&2
        echo "  $ENGINE run --privileged --rm tonistiigi/binfmt --install arm64" >&2
        echo "  (or: sudo apt install qemu-user-static binfmt-support)" >&2
        exit 1
    fi

    image="sourceminder-release:alpine-$arch"
    echo "==> [$arch] building toolchain image $image"
    $ENGINE build --platform "$platform" -t "$image" \
        -f tools/release-alpine.Dockerfile tools

    # Fresh copy of the export per arch so build trees never mix.
    rm -rf "$WORK/build-$arch"
    cp -a "$WORK/src" "$WORK/build-$arch"

    # Rootful docker would otherwise write root-owned files into the mount;
    # rootless podman already maps container root to the invoking user.
    USER_FLAG=""
    if [ "$ENGINE" = "docker" ]; then
        USER_FLAG="--user $(id -u):$(id -g)"
    fi

    echo "==> [$arch] fetch grammars, configure --portable, make static"
    # shellcheck disable=SC2086  # USER_FLAG is intentionally word-split
    $ENGINE run --rm --platform "$platform" $USER_FLAG -e HOME=/tmp \
        -v "$WORK/build-$arch:/src" -w /src "$image" \
        sh -ec 'sh ./fetch-grammars.sh && ./configure --enable-all --portable && make -j"$(nproc)" static SQLITE_STATIC=/usr/lib/libsqlite3.a'

    echo "==> [$arch] smoke test"
    if [ "$arch" = "$HOST_ARCH" ]; then
        sh "$WORK/smoke.sh" "$WORK/build-$arch/build" "$VERSION"
    else
        # shellcheck disable=SC2086
        $ENGINE run --rm --platform "$platform" $USER_FLAG -e HOME=/tmp \
            -v "$WORK:/work" "$image" \
            sh /work/smoke.sh "/work/build-$arch/build" "$VERSION"
    fi

    name="sourceminder-$LABEL-linux-$arch"
    echo "==> [$arch] packaging dist/$name.tar.gz"
    stage="$WORK/$name"
    mkdir -p "$stage"
    for tool in $TOOLS; do
        cp "$WORK/build-$arch/build/$tool-static" "$stage/$tool"
    done
    cp LICENSE README.md "$stage/"
    mkdir -p dist
    tar -czf "dist/$name.tar.gz" -C "$WORK" "$name"
}

case "$ARCH" in
    all) build_arch x86_64; build_arch aarch64 ;;
    *)   build_arch "$ARCH" ;;
esac

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
