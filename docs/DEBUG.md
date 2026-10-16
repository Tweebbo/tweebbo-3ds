# Local Nintendo 3DS debug

Build: **1.0.4-debug**, October 15, 2026. This is an unpublished test build, not approval to release the public client. First physical test target: New 3DS, New 3DS XL or New 2DS XL.

## Install on the same LAN

Open `http://192.168.1.20:8097/` on a computer or phone. On a console with the existing homebrew setup, open **FBI → Remote Install → Scan QR Code** and scan the page's CIA QR. Alternatively, download `tweebbo-debug.3dsx` and put it in `SD:/3ds/` for the Homebrew Launcher.

The PC and console must reach each other on the local network. The PC must remain on. The hotel is `ws://192.168.1.20:8092`; the browser edition is `http://192.168.1.20:8092`. If the PC's LAN address changes, change **Help → Local server…** on the console and rebuild the download page/QR for its new address.

The HOME Menu title is **Tweebbo Debug** (title ID `0004000007EBB100`). It coexists with the public Tweebbo installation. It keeps its settings and character key in `SD:/3ds/Tweebbo-debug/`. It cannot join a public hotel or run the public updater. It starts a separate local character unless you explicitly restore a local account. Help's public health check only reads `/healthz` and never joins or creates an account.

## Included in this build

- **Updates over the LAN.** From this build on, the console checks `http://<PC address>:8097/latest.json` when you enter a room and every two minutes, and offers each newer debug build (Update now / Later). Help → Check for updates checks at once. Builds before this one cannot update themselves: install this one from the QR code once.
- **Crash reports.** A crash writes `SD:/3ds/Tweebbo-debug/crash.txt` and returns to the HOME Menu; the previous session's log is kept as `log-previous.txt`. Share both files after a crash.
- Games and Casino buttons in the empty top corners of the room map (touch screen). Games opens Snow Storm and one-on-one challenges (Tic-tac-toe, Connect Four, Chess) with anyone in the room.
- Chat, Snow Storm chat and private messages use a touch keyboard on the lower screen, so the room or match keeps running on the top screen (X shift, Y space, R symbols and accents, B delete, START send).
- Password rooms: choose a password when creating a room (Y), lock or open your rooms from My Rooms, and enter locked rooms by typing their password.
- Snow Storm aims at the nearest rival automatically; tap a rival's name to target them (tap again for auto-aim), or cycle with the D-Pad.
- Catalogue: prices on every item, a large preview of the selected item on the top screen, and an offer to place it right after buying.
- B no longer opens and cancels a confirmation in the same press (leaving Snow Storm, resigning a game).
- Tweebbo's social accounts appear as text in Help and in empty corners of the room map.
- Lower-screen text baked at native pixel sizes, five-row wardrobe pages, adjusted window labels/buttons and paginated hotel alerts.
- The current web furniture catalogue and clothing export, including extended appearance bits, original animation timing, and multi-page textures for large furniture/backgrounds.
- Lossless compressed textures, exact-grey LA8 pages, furniture cache eviction before allocation, retry after temporary texture failures, off-screen furniture culling and bounded network queues.
- Locked-room password entry, separate room/hotel capacity handling, Frank status artwork, disabled Donate/Sponsor placeholders and moderated-connection pause.
- Wall/floor preview, restoration when leaving the shop, and server-validated purchases in your own room.
- VIP and Staff badges. Accounts already assigned Owner can show/hide their Founder appearance and toggle presence. Assignment remains in the owner-authorized web admin panel.
- Founder walking, dancing, waving, talking, blinking, drinking, sitting and swimming appearance, including the pale skin and teal coat from the web design.
- Tic-tac-toe, Connect Four and Chess challenges with browser players. Legal moves, credits, ownership and appearance authorization remain server-side.
- Snow Storm: the browser's original SnowWar engine runs on the console, so matches stay in lockstep with browser players (lobby, create, join, bots, chat, practice). Circle Pad moves, A throws, X makes snowballs, Y changes the trajectory, L/R zoom, SELECT mutes the original sounds.
- Casino: all five slot games, roulette, blackjack, Casino Hold'em, Video Poker and Crash, with the original cards and symbols. Bets and payouts are decided by the server. A plays/deals/spins, X stands, folds or cashes out, Y doubles, the D-pad changes game and bet, B closes. Next to a slot chair, A sits first and A again plays.
- Existing identity recovery, one-time character moves, friends, room navigation, building, pets and swimming continue using the shared hotel APIs.

## Validation recorded

Host simulation/graphics/animation/protocol tests and transport fixtures run from `tests/`. The online integration test uses the real native network/parser code against an isolated PostgreSQL hotel; it checks chat, room changes, the console badge, native/browser moves in all three board games, resignation and session takeover. `tests/native-minigames.mjs` plays every Casino game through the server and checks that a native and a browser Snow Storm client receive the same roster and turns; `make -C tests snow` checks that the console's script engine and Node produce identical results over 12 complete matches.

Server validation includes 11 cloud database contract tests and 72 legacy unit tests, plus the new capability parsing test. Emulator tours cover the catalogue, finishes, wardrobe, help, badges, Founder identity/presence, board games, the capacity screen, a Snow Storm practice match and the Casino slot machines. The Old 3DS emulation path also boots, reaches the catalogue, finishes, wardrobe and help, and plays a Snow Storm match. Screenshots on the download page are emulator captures, not photographs or console benchmarks.

The final lower-screen window blue gives white titles a computed 5.03:1 contrast ratio. The original artwork, window shapes and controls remain in use.

## Physical console checklist

1. Install beside the existing public app. Confirm the title says **Tweebbo Debug** and Help shows the local hotel.
2. Read chat, navigator, catalogue and wardrobe at the console's normal viewing distance. Check both wardrobe pages, touch controls and accents in names.
3. Change clothes, dance, wave, speak, sit, take/sip a drink, enter/leave the pool, and revisit rooms. Check for missing layers or stale textures.
4. With a browser on the same local hotel, verify movement/chat in both directions, friends and each board game. Verify the 3DS badge appears in the browser.
5. In an owned room, preview and buy finishes, place/rotate/pick up furniture, then reconnect. Previewing alone must not charge credits or change the saved room.
6. For an account assigned Owner in the local admin panel, test visible/anonymous appearance and presence. Confirm another ordinary account cannot make itself Owner.
7. Test a locked room, a temporary Wi-Fi interruption, sleep/wake and a same-account browser takeover. Reconnect without repeated session eviction or lost inventory.
8. Stay in a populated room and a heavily furnished room for at least ten minutes. Note pauses, missing textures, disconnects and whether the console becomes unresponsive.

Useful diagnostics: `SD:/3ds/Tweebbo-debug/crash.txt`, `log.txt`, `log-previous.txt` and `last-error.txt`, plus the console model and the room/action where the issue happened. Character keys (`token-*.txt`), access codes and transfer codes are private; keep them out of screenshots and shared logs.

## Remaining release gates

- Snow Storm and the Casino are native in this build and pass the automated crossplay tests, but still need a real-console session: frame rate in a full Snow Storm match, sound (the console needs the usual DSP firmware dump for homebrew audio; without it the game stays silent) and a long Casino session.
- Physical New-model testing is pending. Old 3DS / Old 2DS compatibility also needs real hardware coverage with the enlarged asset set. Emulator timing cannot establish console performance or a 50-player hardware guarantee.
- Review graphics in rooms with unusually large furniture and many distinct looks. The cache has a fixed hardware memory budget; the largest visible working sets may still require tuning.
- Native font coverage is Latin plus common punctuation; unsupported emoji/scripts fall back to the system font and may display missing glyphs.

Do not publish this build or remove the above gates as part of installing/testing it locally.

## Rebuild

```sh
TWEEBBO_SERVER=ws://192.168.1.20:8092 ./build.sh debug
make -C tests
make -C tests transport
make -C tests online SERVER=ws://127.0.0.1:18093
```

Port 18093 is an isolated fixture hotel; tests must never target a live player database. `out/debug/` contains the CIA/3DSX. The LAN download service serves only a curated `out/debug/share/` directory, not source, SD contents, logs, database backups or credentials.
