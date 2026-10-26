#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
destination=$(dirname "$5")
exec podman run --rm --network host -v "$root":/work:z -v "$destination":"$destination":z -w /work \
  localhost/tweebbo-switch-host:latest "${DOWNLOAD_BINARY:-/work/build/updater-review/download-tls}" "$@"
