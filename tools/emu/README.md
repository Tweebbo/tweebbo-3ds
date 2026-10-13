# Emulator for testing

```bash
podman build -t tweebbo-3ds-emu -f tools/emu/Containerfile tools/emu
mkdir -p build/emu && cd build/emu
curl -LO https://github.com/azahar-emu/azahar/releases/download/2126.2/azahar.AppImage
podman run --rm -v "$PWD":/emu:Z -w /emu tweebbo-3ds-emu bash -c 'chmod +x azahar.AppImage && ./azahar.AppImage --appimage-extract'
```

After the first start, set `lle_applets=false` in `build/emu/home/.config/azahar-emu/qt-config.ini` so the system keyboard uses the emulator's built-in version. To emulate an Old 3DS, set both `is_new_3ds=false` and `is_new_3ds\default=false`.

Steps (`tools/emu/steps-*`): `wait <s>`, `shot <name>`, `key <key> [ms]`, `click <x> <y>`, `type <text>`. Azahar keys: A=`a`, B=`s`, X=`z`, Y=`x`, L=`q`, R=`w`, START=`m`, SELECT=`n`, Circle Pad=arrow keys, D-Pad=`t g f h`. In the 1280×1024 screenshot the top screen starts at (440, 250) and the bottom screen at (480, 490).

```bash
NET=host tools/emu/run.sh tools/emu/steps-online            # .3dsx against a server on this machine
APP=/work/out/tweebbo.cia tools/emu/run.sh tools/emu/steps-update
```

The emulated SD card is `build/emu/home/.local/share/azahar-emu/sdmc/`; Tweebbo reads `3ds/Tweebbo/server.txt` (server URL) and `3ds/Tweebbo/room.txt` (room to enter) there.
