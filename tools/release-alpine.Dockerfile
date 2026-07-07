# Release build environment for SourceMinder static Linux binaries.
#
# Toolchain-only: no project source is baked in. release.sh mounts a clean
# export of the repo and runs fetch-grammars.sh + configure + make static
# inside this image. Alpine/musl is used because musl fully supports static
# linking, so the resulting binaries run on any Linux distro (glibc -static
# is version-fragile).
#
# The tree-sitter runtime version is a validated pin, like the grammar pins
# in fetch-grammars.sh: bumping it means re-validating the indexers.

FROM alpine:3.22

# bash is for the project's configure script (#!/bin/bash); Alpine's
# default shell is busybox ash.
RUN apk add --no-cache build-base bash git sqlite-dev sqlite-static

# Static tree-sitter runtime, pinned to the version the indexers were
# validated against. `make install` puts libtree-sitter.a and the headers
# under /usr/local, where the project's configure expects them.
ARG TREE_SITTER_VERSION=v0.26.8
RUN git clone --depth 1 --branch ${TREE_SITTER_VERSION} \
        https://github.com/tree-sitter/tree-sitter.git /tmp/tree-sitter \
    && make -C /tmp/tree-sitter install PREFIX=/usr/local \
    && rm -rf /tmp/tree-sitter \
    && test -f /usr/local/lib/libtree-sitter.a
