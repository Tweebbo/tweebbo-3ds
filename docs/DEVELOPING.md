# Building and testing Tweebbo 3DS

[Player guide](GUIDE.md) · [Online protocol](ONLINE.md) · [Release notes](RELEASE.md)

The native client uses C, libctru, citro2d and mbedTLS. Read [AGENTS.md](../AGENTS.md) before contributing. Host and emulator checks do not replace physical-console validation.

The unpublished [native power work](POWER.md) covers idle rendering and HTTP worker waits on 3DS, Switch and Vita, with a hardware battery comparison procedure.

## Build

Requirements: Podman, Node 22, Python 3 with Pillow, Google Chrome for graphics export and a checkout of the Tweebbo web edition, selected with `TWEEBBO_WEB`.

```bash
./build.sh export   # Export web graphics/data and generate source/gen/assets.c
./build.sh brand    # Generate the HOME Menu icon, banner and sound
./build.sh          # Convert textures and build out/tweebbo.3dsx
./build.sh cia      # Also build out/tweebbo.cia; version comes from VERSION
```

The build configuration stamp tracks the server, profile and version, rebuilding objects when they change. Never edit `source/gen/` by hand.

## Local testing

The public client connects to `wss://tweebbo.lol` and rejects saved LAN server addresses. Use a separate debug or showcase build for integration tests:

```bash
TWEEBBO_SERVER=ws://192.168.1.20:8092 ./build.sh debug
TWEEBBO_SERVER=ws://192.168.1.20:8096 ./build.sh showcase
make -C tests
make -C tests online
make -C tests tls
```

Debug and showcase profiles accept only private LAN or loopback `ws://` addresses. They have separate outputs, title IDs and SD data directories. Public updates are disabled; the debug profile has its own LAN updater. Never run test fixtures against the public hotel.

`make -C tests` covers simulation, protocol, state, furniture graphics and animations. The online target expects an isolated server on port 8092 and creates a test guest. The TLS target checks `https://tweebbo.lol/healthz` without joining the hotel.

For Azahar, use the LAN build with `NET=host tools/emu/run.sh tools/emu/steps-online`. See the [emulator guide](../tools/emu/README.md), [game-port contracts and budgets](GAME-PORTS.md) and [optimization evidence](FINAL-OPTIMIZATIONS.md).

### Showcase profile

`./build.sh showcase` creates `out/showcase/tweebbo-showcase.{3dsx,cia}` with title ID `0004000007EBB200` and data in `sdmc:/3ds/Tweebbo-showcase/`. Without an explicit server, it selects this PC's LAN IP and port 8096 (`SHOWCASE_PORT` can override the port).

The cross-play demo cycles the five brand models, gestures and movement in a shared room. If the LAN hotel is unavailable, Play offline (Y) opens the simulation. Help → LAN hotel returns to the connection screen. The `steps-showcase`, `steps-showcase-offline`, `steps-showcase-lan` and `steps-crossplay` emulator scripts cover these flows.

## Graphics and memory

`tools/export/run.mjs` runs the web client's drawing code in headless Chrome. It exports avatars, swimmers, catalogue furniture, animation frames, room scenes, pets, clothing and classic interface data. `tools/pack.py` converts the export into console textures and C tables.

- Avatar/interface pages use LA8 for exactly grey parts and RGBA8 for colour, preserving source pixels.
- Furniture textures load on demand from `romfs:/furni/` with an LRU cache sized to free linear memory. Eviction preserves textures queued in the current frame.
- Room backgrounds, scenes and pets are released when leaving. Pet textures are packed per species family so multiple pets fit together on Old 3DS.
- Wall/floor patterns and room masks are lossless. The Noto Sans glyph atlas uses native integer sizes; its license is in `assets/fonts/`.
- Parsed-text and avatar-composition caches reduce repeated lookups. New 3DS uses 804 MHz mode.

`sdmc:/3ds/Tweebbo/log.txt` records free memory, texture failures and average frame time every 30 seconds. Debug data uses `sdmc:/3ds/Tweebbo-debug/`. Emulator frame rates are not hardware performance measurements.

## Source map

| Files | Responsibility |
| --- | --- |
| `main.c` | Main loop and SD storage |
| `net.c`, `ws.c`, `conn.c`, `certs.c` | Networking, TLS, HTTP and certificates |
| `proto.c`, `world.c` | Binary protocol v6 and client state |
| `scene.c`, `avatar.c`, `pets.c`, `gfx.c` | Room rendering, sprites, textures and text |
| `build.c` | Furniture placement |
| `ui.c`, `ui_panels.c`, `ui_social.c`, `skin.c` | Touch interface, windows and social panels |
| `update.c` | GitHub release updater |
| `sim.c`, `path.c` | Offline simulation and pathfinding |
| `gen/` | Generated assets and tables |

## Release publishing

The updater supports optional incremental downloads and validated lookup
caches. See [Incremental updates and Old 3DS work](INCREMENTAL-UPDATES.md) for
compatibility, publication and tests; player notes are in
[RELEASE.md](RELEASE.md).

After the release checks and authorization, build the public artifacts without a LAN server override. `NOTES="..." ./build.sh release` uploads the CIA, 3DSX, `latest.json` and download QR code to GitHub. It reads the release body from `docs/RELEASE.md` (`NOTES_FILE` overrides it) and refuses a CIA containing a local-server address.

Keep the README as a stable player introduction. Replace the current release notes for each release; keep older announcements in GitHub Releases and development evidence in the relevant technical guide.
