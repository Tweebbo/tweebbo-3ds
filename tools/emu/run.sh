#!/usr/bin/env bash
# Runs an emulator session with the given steps and leaves the screenshots in build/shots.
#   tools/emu/run.sh [steps file]      (NET=host to reach a server on this machine)
set -euo pipefail
cd "$(dirname "$0")/../.."
STEPS_FILE=${1:-tools/emu/steps-online}
rm -f build/shots/*.png
timeout 600 podman run --rm ${NET:+--network $NET} -e STEPS=/work/$STEPS_FILE -e APP="${APP:-/work/out/tweebbo.3dsx}" -v "$PWD":/work:z -v "$PWD/build/emu":/emu:z tweebbo-3ds-emu /work/tools/emu/session.sh
podman run --rm -v "$PWD/build/emu":/emu:z tweebbo-3ds-emu bash -c 'grep -E "Critical|Exception Type" /emu/home/.local/share/azahar-emu/log/azahar_log.txt | head -5' || true
