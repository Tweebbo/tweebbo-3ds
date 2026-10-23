# Tweebbo for Nintendo 3DS

Header follow-up, 2026-10-23: the official T and yellow Donate button from the Smart+Classic proof of concept are now included. The T is packed at 10 × 16 pixels and drawn at 1:1 without a circular frame. Donate opens the existing support panel from the room or another panel. The classic two-screen layout is retained. Host checks and CIA/3DSX builds pass; a LAN-only Azahar run verifies opening and closing Donate without creating a payment code. Reference: the owner's `Tweebbo 3DS Smart+Classic.pdf` and `../Tweebbo-3DS-smart-poc/`.

Local follow-up, 2026-10-23: Lia’s profile opens Suggestions using the existing bottom-screen layout and keyboard. Wave, kiss, laugh, thumbs-up and sign retain seated posture. The common server keeps five virtual residents outside rooms. A LAN-only Azahar run shows Lia, the form and validation; its closure was a scripted stop after a live-process check, with no crash reproduced. CIA/3DSX rebuilt locally; physical-device validation remains pending. Review: `../Tweebbo-Vercel/docs/reviews/console-polish-20261023/`.

Native Nintendo 3DS client for [Tweebbo](https://tweebbo.lol) (C + libctru + citro2d + mbedTLS). It plays **in the same hotel as tweebbo.lol**: the same rooms, characters, credits, inventory, friends and pets as the web, the X player and PC. The top screen shows the room at native resolution (64×32 tiles, like the web) and the bottom screen is the classic interface of the web edition, adapted to touch and buttons.

> Tweebbo is in development and may be unstable.

The current working tree includes an **unpublished local debug build** for the October 2026 hotel. Start with [the debug installation and test guide](docs/DEBUG.md). The public downloads below are separate releases.

![Building with the buttons](docs/visual/build-mode.png)
![Crossplay: the web sees the 3DS player](docs/visual/crossplay-web.png)

## Download

Latest version: **[releases](https://github.com/Tweebbo/tweebbo-3ds/releases/latest)**. On the console: FBI → *Remote Install* → *Scan QR Code*:

![Download QR code](docs/download-qr.png)

The QR code points to `https://github.com/Tweebbo/tweebbo-3ds/releases/latest/download/tweebbo.cia` (always the latest version). After that, Tweebbo updates itself from inside the game.

A console with custom firmware (Luma3DS) is required. `tweebbo.3dsx` works from the Homebrew Launcher (`sdmc:/3ds/tweebbo.3dsx`).

## Features

- **Online**: WebSocket over TLS (mbedTLS, embedded root certificates) on its own thread, with exponential reconnection, planned socket renewals and session recovery. HELLO carries platform byte `1` (Nintendo 3DS): the web shows a small pixel-art handheld over the player and their profile reads "This person is connected through a Nintendo 3DS system". The 3DS shows it over other console players too.
- **Shared hotel features**:
  - *Navigator*: All / Public / My Rooms with occupancy, *Go*, refresh every 15 s and **Create a room**. Featured official rooms appear first; Casino uses the original credits bag and an ochre row, matching the browser.
  - *Catalogue*: every page and offer of the catalogue, icons, prices and purchase confirmation.
  - *Hand*: grouped inventory and **place in room**.
  - **Build mode**: Circle Pad / D-Pad moves the ghost furniture (green when it fits, red when it does not, same rules as the web), L/R rotate, A places, B cancels; wall items slide along the wall and L/R lower or raise them. Tapping the minimap jumps to that tile.
  - Furniture panel: sit or lie down, use (switches, dice, teleporters, gates), choose a drink at dispensers, move, rotate, pick up and redeem.
  - *Me*: gestures, drinks, the full wardrobe (category tabs, real clothing previews, colours and gender filters), renaming and the **character code** (save / restore).
  - *Friends Console*: friends with their room and *Visit*, requests, search, remove, ignore and private messages.
  - *Pets*: adopt (species, coat, color and name), place, commands and status; pets are drawn and walk in the room.
  - Rooms: the lobby, private rooms, Classic Park with its animated fountain and The Pool with swimming.
  - Chat bubbles, chat history, ignore list, hotel alerts and server notices.
  - *Games*: Snow Storm (the original SnowWar, in sync with web players, with bots, lobbies and practice; the nearest rival is targeted automatically and you can tap a rival to choose) and one-on-one Tic-tac-toe, Connect Four and Chess challenges with anyone in the room.
  - *Casino*: five slot machines, roulette, blackjack, Casino Hold'em, Video Poker, Crash and the **VIP Dash** (climb the red carpet on one of four risk ladders and cash out before the trap, with the original furniture, figures, coin and trap animations), with the original cards and symbols. On the console every game plays with credits; chips (real money) are only in the PC edition. Bets and payouts are decided by the hotel.
  - **Trading**: *Trade* on another player's card offers up to twelve furniture slots from your hand; both players accept and then confirm again, any change to an offer resets both confirmations, and credits are never traded.
  - **Tweebbo Club**: Club members are marked on their card, and the Club and Club Gold badges show on profiles. Members get 100 credits instead of 10 for hanging out.
  - **Support Tweebbo** (Me → *Support*): what a donation gives (Tweebbo Club for life, the VIP badge and 10,000 credits) and how chips work, with a QR code to tweebbo.lol, where donations and chips are available with the same character code. A *Show code* shortcut opens your character code.
  - Password rooms: create a room with a password, lock or open your rooms from My Rooms, and enter locked rooms by typing the password.
  - Walls and floors, the expanded furniture and clothing, VIP/Staff/Club badges and the new lobby.
  - A touch keyboard for chat, names and passwords: the hotel keeps moving on the top screen while you type.
  - Catalogue preview: the selected item is shown large on the top screen, with its price.
- **"Press A" (3DS only)**: next to a dispenser, seat, bed, dice, switch, gate or teleporter (or facing another player) the top screen shows "Ⓐ Get a drink · Mochamaster", "Ⓐ Sit", "Ⓐ Roll the dice"… and A does it. With nothing nearby, A talks.
- **In-game updates**: when entering the hotel the game reads `latest.json` from the latest GitHub release. If there is a newer version it offers **Update now**, downloads the CIA (or the `.3dsx` when launched from the Homebrew Launcher), checks its SHA-256 and installs it through AM without FBI, then asks to restart. If the console refuses to install, the CIA is saved to `SD:/cias/` to install with FBI. Also available in *Help → Check for updates*.
- **Offline practice**: a local simulation of the public rooms (lobby, park, both pools, rooftop, casino and After Hours Bar) with bots, only when chosen in *Help → Offline*.

## Controls

| Control | Room | With a window open | Building |
| --- | --- | --- | --- |
| Circle Pad | Walk (8 directions) | Walk | Move the item |
| D-Pad | Walk | Move through lists, tabs and values | Move the item |
| A | Talk / "Press A" action | Main action | Place |
| B | Sit / stand up | Close the window | Cancel |
| X / Y | Wave / dance | Shortcuts (e.g. *Create a room*) | — |
| L / R | Overview | Overview | Rotate (lower/raise on walls) |
| SELECT | Next window | Next window | — |
| START | Quit | Quit | Quit |

Touch screen: bottom bar (Me, Navigator, Catalogue, Hand, Friends, Chat, Help), the Games and Casino buttons in the top corners of the map, lists that scroll by dragging, and the minimap (tap to walk, sit or select people and furniture).

Local console update (unpublished): drag the chat history with the stylus, use D-pad
up/down for lines and left/right for pages. New messages keep your reading position;
scroll to the bottom to resume following. Igloo graphics and animated visitors are
included, with feature-gated admission on the matching local server. See
[local validation](../Tweebbo-Vercel/docs/performance/console-input/README.md).

## Saved progress

Everything done on the console (credits, inventory, rooms, looks, name, friends, pets) is saved on the server, exactly like the web. The console only keeps the character key (`sdmc:/3ds/Tweebbo/token-<server>.txt`, **private**) and the last room, so reopening, updating or reinstalling keeps the same character. If the SD card is lost, *Me → Character code* → *Use a code* recovers the character with its private code (also from the web).

## Performance and compatibility

The released client has been tested on real hardware. **The current expanded debug build still needs physical-console validation**, starting with New 3DS / New 3DS XL / New 2DS XL. It keeps the original-model code path. Emulator results are not hardware performance measurements.

- Exactly grey avatar parts use LA8; colored parts use RGBA8. Both preserve source pixels. Five avatar/interface pages plus the native-sized glyph atlas replace the older asset set.
- Caches for parsed text and avatar compositions avoid hundreds of lookups per frame, which matters most on the 268 MHz Old 3DS CPU.
- Furniture loads per room with an LRU cache; backgrounds, scenes and pets are freed when leaving.
- New 3DS runs in 804 MHz mode.
- `sdmc:/3ds/Tweebbo/log.txt` records the console model, free memory, texture failures, updates and, every 30 s, the average frame time. Debug logs live in `sdmc:/3ds/Tweebbo-debug/`.

## Server

The default server is `wss://tweebbo.lol`. For testing against another server: build with `TWEEBBO_SERVER=ws://<host>:<port> ./build.sh cia`, or on the console *Help → Local server…*. *Help → Check tweebbo.lol* tests the secure connection (GET `/healthz`, creates no data). Files in `sdmc:/3ds/Tweebbo/`: `server.txt`, `room.txt`, `token-<server>.txt`, `last-error.txt`, `log.txt`, and optionally `update-url.txt` to read updates from another manifest.

## Building

Requirements: Podman, Node 22, Python 3 with Pillow, Google Chrome (for the graphics export) and a checkout of the Tweebbo web edition (`TWEEBBO_WEB`).

```bash
./build.sh export   # exports graphics and data from the web edition and generates source/gen/assets.c
./build.sh brand    # redraws the HOME Menu icon, banner and banner sound with the game's avatars
./build.sh          # converts textures (tex3ds, compressed) and builds out/tweebbo.3dsx
./build.sh cia      # also packs out/tweebbo.cia (version from VERSION)
./build.sh showcase # Showcase build for photos and cross-play demos (separate title and data)
TWEEBBO_SERVER=ws://192.168.1.20:8092 ./build.sh debug # separate local-only CIA and 3DSX
NOTES="…" ./build.sh release   # publishes the GitHub release (CIA, 3DSX, latest.json, QR code)
make -C tests                  # simulation, protocol, state, every furniture graphic and animations
make -C tests online           # real online client against ws://127.0.0.1:8092 (creates a guest)
make -C tests tls              # TLS with mbedTLS 2.28 against https://tweebbo.lol/healthz
```

The build configuration stamp tracks the server, profile and version and rebuilds objects when they change. `./build.sh release` refuses to publish a CIA that points to a local server. Debug artifacts use a separate output directory, object directory, title ID and SD directory; public updates are disabled.

### Showcase builds (3DS · Switch · Vita)

`./build.sh showcase` produces `out/showcase/tweebbo-showcase.{3dsx,cia}` (title
`0004000007EBB200`, data in `sdmc:/3ds/Tweebbo-showcase/`, no update checks). It never connects to
tweebbo.lol: like the debug build it only accepts private LAN `ws://` addresses, and the build bakes
in the hotel of this PC's Wi-Fi (`ws://<LAN IP>:8096`, or `TWEEBBO_SERVER`). When the hotel does not
answer, the connection screen offers **Play offline (Y)** (the simulation with bots, in the same
build); Help › LAN hotel goes back. Inside the shared room it rotates the five brand models every ~5 s with
`SET_LOOK` (Bobba, Cyber, Director, Street, VIP Girl), waves or dances and walks a little, with a
"TWEEBBO SHOWCASE" banner. Every console in the room sees the same character change: it is the
cross-play demo used for the Switch and Vita proof-of-concept photos. `tools/emu/steps-showcase`
captures one screenshot per model on the emulator against a local hotel; `steps-showcase-offline`
and `steps-showcase-lan` check the offline option and that a stale tweebbo.lol `server.txt` is
ignored, and `steps-crossplay` takes the cross-play shots.

The showcase clients identify themselves in `HELLO` with a trailing device byte (1 3DS, 2 Switch,
3 Vita) and the server announces each player's device with the `DEVICE` (140) message, so the web,
the 3DS and the consoles draw the correct pixel-art badge over each other.

### Graphics shared with the web

`tools/export/run.mjs` copies a snapshot of the web client and runs its own drawing code in headless Chrome: avatar parts, swimming bodies, catalogue furniture (every rotation, state and distinct animation frame), the floors of rooms without an original background (`Renderer.buildFloor`), plus the catalogue, public rooms, pets and classic interface data. The October 15 snapshot contains 1,906 furniture definitions with graphics and 32,064 variants. `tools/pack.py` converts it for the console:

- Avatar and classic interface atlas (`romfs:/gfx/atlas*.t3x`, RGBA8 and LA8 pages).
- Furniture textures (`romfs:/furni/d<id>.t3x`, with extra pages for large definitions), loaded on demand with an LRU cache sized to free linear memory. Eviction preserves textures queued in the current frame.
- Room backgrounds, layered park and pool scenes, and one texture per pet breed palette.
- C tables: catalogue and offers, wardrobe, dances, drinks, pets and rooms.
- Original wall/floor patterns and lossless room masks, applied once when a finish changes.
- A Noto Sans glyph atlas at integer sizes 10–16 for the lower screen. Its font license is included in `assets/fonts/`.

## Emulator tests

`tools/emu/` runs Azahar headless (Xvfb + xdotool); see [tools/emu/README.md](tools/emu/README.md). With `NET=host` the emulator reaches a server on the same machine.

## Layout

```
source/
  main.c        Main loop and SD card storage
  net.c         Network thread, queues, reconnection, rooms and HTTP requests (or the offline simulation)
  ws.c conn.c   WebSocket/HTTP over TCP with TLS (mbedTLS); also build on the PC
  certs.c       Root certificates for tweebbo.lol and GitHub
  update.c      In-game updates from GitHub releases
  world.c       Client state
  proto.c       Binary protocol v6 (+ platform byte in HELLO)
  scene.c       Top screen: camera, layers, depth, bubbles, ghost, 3DS badge, "Press A" prompt
  avatar.c      Avatars and swimmers
  pets.c        Pets
  build.c       Build mode (placement rules, walls)
  ui.c          Classic frame, room, chat, Me, wardrobe, help, alerts, updates, "Press A"
  ui_panels.c   Navigator, Catalogue, Hand, furniture panel, Pets
  ui_social.c   Friends Console
  skin.c        Windows, buttons and lists with the classic graphics
  gfx.c         Textures (atlas, on-demand furniture, scenes, pets) and text
  sim.c path.c  Offline simulated server
  gen/          Generated by tools/pack.py (not in the repository)
```

### Local room names and Classic missions (2026-10-20)

Owners can rename rooms without changing their links or contents; the five-room creation limit is removed on the server. Room titles update live through optional server packet 144. My info in Friends edits the personal mission (up to 50 Unicode characters, including an empty mission). These changes are local only and require the updated local server.

Use Rename in Navigator → My Rooms (Y on the selected room). In Friends, My info opens the mission editor. Private letters use Older/Newer and Start/More pages so long messages remain readable; new arrivals preserve the selected letter.
