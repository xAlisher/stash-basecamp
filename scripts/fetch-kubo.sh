#!/usr/bin/env bash
set -euo pipefail
KUBO_VERSION="v0.33.0"
KUBO_ARCH="linux-amd64"
DEST_DIR="$(cd "$(dirname "$0")/.." && pwd)/third_party/kubo/bin"
mkdir -p "$DEST_DIR"
curl -fsSL "https://github.com/ipfs/kubo/releases/download/${KUBO_VERSION}/kubo_${KUBO_VERSION}_${KUBO_ARCH}.tar.gz" \
  | tar -xz -C "$DEST_DIR" --strip-components=1 kubo/ipfs
chmod +x "$DEST_DIR/ipfs"
echo "kubo ${KUBO_VERSION} → $DEST_DIR/ipfs"
