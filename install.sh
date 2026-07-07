#!/bin/sh
# install.sh - download and install the SourceMinder binaries.
#
#   curl -fsSL https://sourceminder.org/install.sh | sh
#
# Installs the eight tools (index-c, index-go, index-perl, index-php,
# index-python, index-rust, index-ts, qi) to ~/.local/bin. No sudo needed.
#
# Environment overrides:
#   PREFIX=/usr/local            install to $PREFIX/bin instead (may need sudo)
#   SOURCEMINDER_VERSION=vX.Y.Z  install a specific release (default: latest)
#   SOURCEMINDER_BASE_URL=...    download from a different host
#
# URL contract (what the host behind BASE_URL must serve; today these are
# redirects to the code forge's release pages):
#   $BASE_URL/install.sh                        this script
#   $BASE_URL/releases/latest                   redirect ending in /tag/<tag>
#   $BASE_URL/releases/download/<tag>/<file>    release assets

set -eu

BASE_URL="${SOURCEMINDER_BASE_URL:-https://sourceminder.org}"
BIN_DIR="${PREFIX:-$HOME/.local}/bin"
TOOLS="index-c index-go index-perl index-php index-python index-rust index-ts qi"

fail() {
    echo "Error: $1" >&2
    exit 1
}

# --- platform detection ----------------------------------------------------

OS=$(uname -s)
case "$OS" in
    Linux) OS=linux ;;
    Darwin) fail "no prebuilt macOS binaries yet -- see the README for building from source" ;;
    *) fail "unsupported platform '$OS' -- see the README for building from source" ;;
esac

ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64) ARCH=x86_64 ;;
    aarch64|arm64) ARCH=aarch64 ;;
    *) fail "unsupported architecture '$ARCH' -- see the README for building from source" ;;
esac

# --- downloader (curl preferred, wget fallback) ----------------------------

if command -v curl >/dev/null 2>&1; then
    download() { curl -fsSL -o "$2" "$1"; }
elif command -v wget >/dev/null 2>&1; then
    download() { wget -q -O "$2" "$1"; }
else
    fail "need curl or wget"
fi

# --- resolve version --------------------------------------------------------

TAG="${SOURCEMINDER_VERSION:-}"
if [ -z "$TAG" ]; then
    # The /releases/latest redirect chain ends at .../tag/<tag>; the final
    # URL's basename is the tag. Requires curl (-w url_effective).
    command -v curl >/dev/null 2>&1 || \
        fail "resolving the latest version requires curl; with wget, set SOURCEMINDER_VERSION=vX.Y.Z"
    final_url=$(curl -fsSL -o /dev/null -w '%{url_effective}' "$BASE_URL/releases/latest") || \
        fail "cannot reach $BASE_URL/releases/latest"
    TAG="${final_url##*/}"
    case "$TAG" in
        v*) ;;
        *) fail "could not determine latest version from $final_url; set SOURCEMINDER_VERSION=vX.Y.Z" ;;
    esac
fi

VERSION="${TAG#v}"
ASSET="sourceminder-$VERSION-$OS-$ARCH.tar.gz"
ASSET_URL="$BASE_URL/releases/download/$TAG/$ASSET"

# --- download and verify ----------------------------------------------------

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "Downloading sourceminder $TAG ($OS-$ARCH)..."
download "$ASSET_URL" "$TMP/$ASSET" || fail "download failed: $ASSET_URL"
download "$BASE_URL/releases/download/$TAG/checksums.txt" "$TMP/checksums.txt" || \
    fail "download failed: checksums.txt for $TAG"

grep " $ASSET\$" "$TMP/checksums.txt" > "$TMP/expected.txt" || \
    fail "$ASSET not listed in checksums.txt"
(
    cd "$TMP"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -c expected.txt > /dev/null
    else
        shasum -a 256 -c expected.txt > /dev/null
    fi
) || fail "checksum verification FAILED for $ASSET -- aborting"
echo "Checksum OK."

# --- install -----------------------------------------------------------------

tar -xzf "$TMP/$ASSET" -C "$TMP"
mkdir -p "$BIN_DIR" || fail "cannot create $BIN_DIR (try PREFIX=... or sudo)"
for tool in $TOOLS; do
    cp "$TMP/sourceminder-$VERSION-$OS-$ARCH/$tool" "$BIN_DIR/$tool" || \
        fail "cannot write to $BIN_DIR (try PREFIX=... or sudo)"
    chmod 755 "$BIN_DIR/$tool"
done

echo "Installed to $BIN_DIR: $TOOLS"

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *) echo "Note: $BIN_DIR is not on your PATH. Add it with:"
       echo "  export PATH=\"$BIN_DIR:\$PATH\"" ;;
esac

echo ""
echo "Quickstart:"
echo "  index-c ./src --once     # index a C project (or index-go, index-perl, index-php, index-python, index-rust, index-ts)"
echo "  qi some_symbol           # find a symbol"
