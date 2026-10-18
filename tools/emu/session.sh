#!/usr/bin/env bash
# Inside the tweebbo-3ds-emu container: starts Xvfb + Azahar with the app
# and runs the steps file (one command per line):
#   wait <s> | shot <name> | key <xdotool key> [ms] | click <x> <y> | type <text>
set -u
export HOME=/emu/home DISPLAY=:99 QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1
mkdir -p "$HOME" /work/build/shots
Xvfb :99 -screen 0 1280x1024x24 >/dev/null 2>&1 &
sleep 2
/emu/squashfs-root/AppRun "${APP:-/work/out/tweebbo.3dsx}" >/work/build/shots/azahar.log 2>&1 &
EMU=$!
while read -r cmd a b c; do
  case "$cmd" in
    wait) sleep "$a" ;;
    shot) import -window root "/work/build/shots/$a.png" ;;
    key) xdotool search --sync --onlyvisible --class azahar windowactivate --sync 2>/dev/null; xdotool keydown "$a"; sleep "$(awk "BEGIN{print ${b:-120}/1000}")"; xdotool keyup "$a" ;;
    keys) xdotool search --sync --onlyvisible --class azahar windowactivate --sync 2>/dev/null; xdotool keydown "$a" "$b"; sleep "$(awk "BEGIN{print ${c:-120}/1000}")"; xdotool keyup "$b" "$a" ;;
    click) xdotool mousemove "$a" "$b" mousedown 1; sleep 0.15; xdotool mouseup 1 ;;
    type) xdotool type --delay 80 "$a $b $c" ;;
    windows) xdotool search --onlyvisible --name . getwindowname %@ 2>/dev/null > /work/build/shots/windows.txt ;;
    "#"*|"") ;;
  esac
done < "${STEPS:-/work/tools/emu/steps-online}"
kill "$EMU" 2>/dev/null || true
sleep 1
kill -KILL "$EMU" 2>/dev/null || true
wait "$EMU" 2>/dev/null || true
exit 0
