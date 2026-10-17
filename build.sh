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
BUILD_PROFILE=release
TARGET=out/tweebbo
RSF=cia/tweebbo.rsf
APP_NAME=Tweebbo
if [ "${1:-}" = debug ]; then
  : "${TWEEBBO_SERVER:?Set TWEEBBO_SERVER to your local hotel, e.g. ws://192.168.1.20:8092}"
  python3 - <<'CHECK'
import ipaddress, os, urllib.parse
u = urllib.parse.urlparse(os.environ['TWEEBBO_SERVER'])
try:
    host = ipaddress.ip_address(u.hostname)
    ranges = ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16', '127.0.0.0/8', '169.254.0.0/16')
    valid = u.scheme == 'ws' and any(host in ipaddress.ip_network(net) for net in ranges)
except ValueError:
    valid = False
if not valid or u.username or u.password or u.path not in ('', '/') or u.query or u.fragment:
    raise SystemExit('Debug builds require a private LAN IP or loopback ws:// address.')
CHECK
  BUILD_PROFILE=debug
  TARGET=out/debug/tweebbo-debug
  # Debug builds are numbered: consoles on the LAN update when tools/debug-page.py publishes a higher number.
  mkdir -p build romfs
  DEBUG_BUILD=$(( $(cat build/debug-build 2>/dev/null || echo 0) + 1 ))
  echo "$DEBUG_BUILD" > build/debug-build
  echo "$DEBUG_BUILD" > romfs/debug-build.txt
  # The number lives in romfs: repack even when no code changed.
  rm -f "$TARGET.3dsx" "$TARGET.smdh"
  RSF=cia/tweebbo-debug.rsf
  APP_NAME='Tweebbo Debug'
fi
if [ "${1:-}" = showcase ]; then
  # Poster build for photos on real hardware: plays online with the showcase cast
  # and a banner. It has its own CIA title, data folder and identity.
  BUILD_PROFILE=showcase
  TARGET=out/showcase/tweebbo-showcase
  RSF=cia/tweebbo-showcase.rsf
  APP_NAME='Tweebbo Showcase'
  mkdir -p build romfs
  rm -f "$TARGET.3dsx" "$TARGET.smdh"
fi

[ "$BUILD_PROFILE" = debug ] || rm -f romfs/debug-build.txt
run() { podman run --rm -e BUILD_PROFILE="$BUILD_PROFILE" -e TWEEBBO_SERVER="${TWEEBBO_SERVER:-}" -v "$PWD":/work:z -w /work "$IMAGE" bash -c "$*"; }
# makerom needs glibc 2.38 or newer.
run_cia() { podman run --rm -v "$PWD":/work:z -w /work docker.io/library/ubuntu:24.04 bash -c "$*"; }

case "${1:-}" in
  cia|debug|showcase) ;;
  release)
    # Consoles read releases/latest/download/latest.json and update themselves.
    REPO=Tweebbo/tweebbo-3ds
    URL=https://github.com/$REPO
    VERSION=$(cat VERSION)
    TAG=v$VERSION
    [ -f out/tweebbo.cia ] && [ -f out/tweebbo.3dsx ] || { echo "Build first with ./build.sh cia"; exit 1; }
    if strings out/tweebbo.elf | grep -E 'TWEEBBO_LOCAL_DEBUG|^ws://[0-9A-Za-z]' >/dev/null; then echo "This CIA points to a local server: build it without TWEEBBO_SERVER"; exit 1; fi
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
python3 tools/pack-font.py
python3 tools/pack-snow.py
node tools/export/snow.mjs
mkdir -p romfs/gfx romfs/furni
# Remove textures no longer present in the generated manifest.
for file in romfs/gfx/*.t3x; do [ -e "build/gfx/$(basename "$file" .t3x).png" ] || rm -f "$file"; done
for file in romfs/furni/*.t3x; do [ -e "build/gfx/furni/$(basename "$file" .t3x).png" ] || rm -f "$file"; done
# Textures are LZ-compressed: there are hundreds and they load on demand.
run 'bash tools/convert-textures.sh && make -j"$(nproc)"'
ls -la "$TARGET.3dsx"

if [ "${1:-}" = cia ] || [ "${1:-}" = debug ] || [ "${1:-}" = showcase ]; then
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
  run_cia "$TOOLS/bannertool/linux-x86_64/bannertool makebanner -i assets/banner.png -a assets/banner.wav -o $TARGET.bnr >/dev/null && \
       $TOOLS/bannertool/linux-x86_64/bannertool makesmdh -s '$APP_NAME' -l '$APP_NAME for 3DS v$VERSION' -p Tweebbo -i assets/icon.png -f visible,allow3d,recordusage -o $TARGET.icn >/dev/null && \
       $TOOLS/makerom/makerom -f cia -o $TARGET.cia -elf $TARGET.elf -rsf $RSF \
         -icon $TARGET.icn -banner $TARGET.bnr -exefslogo -target t -DAPP_ROMFS=romfs \
         -major $MAJOR -minor $MINOR -micro $MICRO"
  ls -la "$TARGET.cia"
fi
