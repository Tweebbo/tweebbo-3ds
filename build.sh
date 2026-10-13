#!/usr/bin/env bash
# Builds Tweebbo 3DS inside the devkitpro/devkitarm container.
#   ./build.sh            converts the textures and builds out/tweebbo.3dsx
#   ./build.sh export     re-exports graphics and data from the web edition (TWEEBBO_WEB)
#   ./build.sh brand      redraws the HOME Menu icon, banner and banner sound
#   ./build.sh cia        also packs out/tweebbo.cia (makerom + bannertool)
#   ./build.sh release    publishes out/ as a GitHub release: CIA, 3DSX, latest.json and QR code
#   ./build.sh clean
set -euo pipefail
cd "$(dirname "$0")"
IMAGE=docker.io/devkitpro/devkitarm:latest

run() { podman run --rm -e TWEEBBO_SERVER="${TWEEBBO_SERVER:-}" -v "$PWD":/work:Z -w /work "$IMAGE" bash -c "$*"; }
# makerom needs glibc 2.38 or newer.
run_cia() { podman run --rm -v "$PWD":/work:Z -w /work docker.io/library/ubuntu:24.04 bash -c "$*"; }

case "${1:-}" in
  cia) ;;
  release)
    # Consoles read releases/latest/download/latest.json and update themselves.
    REPO=Tweebbo/tweebbo-3ds
    URL=https://github.com/$REPO
    VERSION=$(cat VERSION)
    TAG=v$VERSION
    [ -f out/tweebbo.cia ] && [ -f out/tweebbo.3dsx ] || { echo "Build first with ./build.sh cia"; exit 1; }
    if strings out/tweebbo.elf | grep -q '^ws://[0-9A-Za-z]'; then echo "This CIA points to a local server: build it without TWEEBBO_SERVER"; exit 1; fi
    REL=build/release
    rm -rf $REL && mkdir -p $REL
    cp out/tweebbo.cia out/tweebbo.3dsx $REL/
    python3 - "$REL" "$URL" "$TAG" "$VERSION" "${NOTES:-}" <<'PY'
import hashlib, json, os, sys
rel, url, tag, version, notes = sys.argv[1:6]
sha = lambda f: hashlib.sha256(open(os.path.join(rel, f), 'rb').read()).hexdigest()
json.dump({
    'version': version,
    'cia': f'{url}/releases/download/{tag}/tweebbo.cia',
    'size': os.path.getsize(os.path.join(rel, 'tweebbo.cia')),
    'sha256': sha('tweebbo.cia'),
    '3dsx': f'{url}/releases/download/{tag}/tweebbo.3dsx',
    '3dsx_sha256': sha('tweebbo.3dsx'),
    'notes': notes,
}, open(os.path.join(rel, 'latest.json'), 'w'), indent=1)
PY
    node tools/export/qr.mjs "$URL/releases/latest/download/tweebbo.cia" $REL/qr.png
    cp $REL/qr.png docs/download-qr.png
    GH=${GH:-$(command -v gh || echo ~/.local/bin/gh)}
    $GH release create "$TAG" $REL/tweebbo.cia $REL/tweebbo.3dsx $REL/latest.json $REL/qr.png \
      --repo $REPO --title "Tweebbo for Nintendo 3DS $VERSION" --notes-file "${NOTES_FILE:-docs/RELEASE.md}" --latest
    echo "Published $TAG at $URL/releases/tag/$TAG"
    exit 0
    ;;
  export)
    node tools/export/run.mjs build/export all
    python3 tools/pack.py build/export
    ;;
  brand)
    node tools/export/brand.mjs "$(cat tools/brand-avatars.json)"
    python3 tools/brand.py
    exit 0
    ;;
  clean)
    run make clean
    rm -rf romfs/gfx
    exit 0
    ;;
esac

[ -f source/gen/assets.c ] || { echo "source/gen/assets.c is missing: run ./build.sh export"; exit 1; }
mkdir -p romfs/gfx romfs/furni
# Textures are LZ-compressed: there are hundreds and they load on demand.
run 'rm -f romfs/gfx/atlas*.t3x.stale; for f in build/gfx/*.png; do n=$(basename "$f" .png); fmt=rgba8888; case $n in atlasg*) fmt=la8;; esac; [ romfs/gfx/$n.t3x -nt "$f" ] || tex3ds -f $fmt -z auto -o romfs/gfx/$n.t3x "$f"; done && \
     for f in build/gfx/furni/*.png; do n=$(basename "$f" .png); [ romfs/furni/$n.t3x -nt "$f" ] || tex3ds -f rgba8888 -z auto -o romfs/furni/$n.t3x "$f"; done && \
     make -j"$(nproc)"'
ls -la out/tweebbo.3dsx

if [ "${1:-}" = cia ]; then
  TOOLS=build/ctrtools
  if [ ! -x $TOOLS/makerom/makerom ] || [ ! -x $TOOLS/bannertool/linux-x86_64/bannertool ]; then
    mkdir -p $TOOLS/dl
    curl -sL -o $TOOLS/dl/makerom.zip https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-ubuntu_x86_64.zip
    curl -sL -o $TOOLS/dl/bannertool.zip https://github.com/diasurgical/bannertool/releases/download/1.2.0/bannertool.zip
    unzip -o -q $TOOLS/dl/makerom.zip -d $TOOLS/makerom
    unzip -o -q $TOOLS/dl/bannertool.zip -d $TOOLS/bannertool
    chmod +x $TOOLS/makerom/makerom $TOOLS/bannertool/linux-x86_64/bannertool
  fi
  VERSION=$(cat VERSION)
  IFS=. read -r MAJOR MINOR MICRO <<< "$VERSION"
  run_cia "$TOOLS/bannertool/linux-x86_64/bannertool makebanner -i assets/banner.png -a assets/banner.wav -o out/tweebbo.bnr >/dev/null && \
       $TOOLS/bannertool/linux-x86_64/bannertool makesmdh -s Tweebbo -l 'Tweebbo for 3DS v$VERSION' -p Tweebbo -i assets/icon.png -f visible,allow3d,recordusage -o out/tweebbo.icn >/dev/null && \
       $TOOLS/makerom/makerom -f cia -o out/tweebbo.cia -elf out/tweebbo.elf -rsf cia/tweebbo.rsf \
         -icon out/tweebbo.icn -banner out/tweebbo.bnr -exefslogo -target t -DAPP_ROMFS=romfs \
         -major $MAJOR -minor $MINOR -micro $MICRO"
  ls -la out/tweebbo.cia
fi
