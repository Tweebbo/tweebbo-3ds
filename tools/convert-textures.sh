#!/usr/bin/env bash
set -euo pipefail
convert_texture() {
  local file=$1 name format=rgba8888 target
  name=$(basename "$file" .png)
  case "$file" in
    build/snow/*) target=romfs/snow/$name.t3x; case "$name" in gray*) format=la8;; esac ;;
    build/gfx/furni/*) target=romfs/furni/$name.t3x; grep -qx "$name" build/gfx/soft-alpha.txt || format=rgba5551 ;;
    *) target=romfs/gfx/$name.t3x
       case "$name" in
         atlasg*|font*) format=la8;;
         atlass*|atlask*) ;;
         atlas*|scene_*) format=rgba5551;;
         room_*) grep -qx "$name" build/gfx/soft-alpha.txt || format=rgba5551;;
       esac ;;
  esac
  # The format is part of the target: a texture converted with another (or an unknown) format
  # is redone. The record lives in build/, so romfs only carries the textures.
  local record="build/texfmt/$target.fmt"
  if [ ! -f "$record" ] || [ "$(cat "$record")" != "$format" ]; then rm -f "$target"; fi
  if [ ! "$target" -nt "$file" ]; then
    tex3ds -f "$format" -z auto -o "$target.tmp" "$file" >/dev/null && mv "$target.tmp" "$target" && mkdir -p "$(dirname "$record")" && echo "$format" > "$record"
  fi
}
export -f convert_texture
printf '%s\0' build/gfx/*.png build/gfx/furni/*.png | xargs -0 -n 1 -P "${TEXTURE_JOBS:-6}" bash -c 'convert_texture "$1"' _
printf '%s\0' build/snow/*.png | xargs -0 -n 1 -P "${TEXTURE_JOBS:-6}" bash -c 'convert_texture "$1"' _
