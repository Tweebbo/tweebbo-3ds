# Local Nintendo 3DS debug

Build: **1.2.5-debug.79**, October 25, 2026. This is a LAN test build, not approval to release the public client. First physical test target: New 3DS, New 3DS XL or New 2DS XL.

## Update an existing debug installation

Set **Help → Local server…** to `ws://192.168.1.165:18877`, then use **Help → Check for updates → Update now**. Restart when installation finishes. The saved server from an older installation is retained, so correct its address before checking if it still points at the old PC address. The updater obtains `latest.json` from the same host on port 8097 and verifies the CIA/3DSX SHA-256. No FBI reinstall is needed for an existing debug client with LAN updates.

This build adds Fast Food, Battle Ball, their original lobbies, the shared Snow Storm contract and the six-language selector with Chinese/Japanese font pages. Read `GAME-PORTS.md` and the linked language integration guide for the reproducible import method. Production featured rooms remain the regional reception, Casino and After Hours; the new game rooms are ordinary official rooms. The server uses an isolated candidate database, separate from production and other local hotels.

## Install on the same LAN

Open `http://192.168.1.165:8097/` on a computer or phone. On a console with the existing homebrew setup, open **FBI → Remote Install → Scan QR Code** and scan the page's CIA QR. Alternatively, download `tweebbo-debug.3dsx` and put it in `SD:/3ds/` for the Homebrew Launcher.

The PC and console must reach each other on the local network. The PC must remain on. The hotel is `ws://192.168.1.165:18877`; the browser edition is `http://192.168.1.165:18877`. If the PC's LAN address changes, change **Help → Local server…** on the console and rebuild the download page/QR for its new address.

The HOME Menu title is **Tweebbo Debug** (title ID `0004000007EBB100`). It coexists with the public Tweebbo installation. It keeps its settings and character key in `SD:/3ds/Tweebbo-debug/`. It cannot join a public hotel or run the public updater. It starts a separate local character unless you explicitly restore a local account. Help's public health check only reads `/healthz` and never joins or creates an account.

## Included in this build

- **Updates over the LAN.** The console checks `http://<PC address>:8097/latest.json` when you enter a room and every two minutes, and offers each newer debug build (Update now / Later). Help → Check for updates checks at once. Older installations without the LAN updater need a one-time installation from the QR code.
- **Crash reports.** A crash writes `SD:/3ds/Tweebbo-debug/crash.txt` and returns to the HOME Menu; the previous session's log is kept as `log-previous.txt`. Share both files after a crash.
- Games and Casino buttons in the empty top corners of the room map (touch screen). Game Center opens Fast Food, Battle Ball, Snow Storm and their lobby links; challenges include Tic-tac-toe, Connect Four and Chess.
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
- Native fonts include Latin, common punctuation and the Chinese/Japanese glyphs in the imported language package. Other user-supplied glyphs still use the platform fallback and may be missing. Some untranslated dynamic messages remain in English.

Do not publish this build or remove the above gates as part of installing/testing it locally.

## LAN publication verification, build 72

The owner requested this LAN update on October 25. Both served artifacts matched their manifest SHA-256. The native HTTP download fixture fetched the complete 78,959,552-byte CIA and recovered a transfer interrupted after 3,145,728 bytes, with the same final hash. See `debug-lan-72/publication.json` and `native-download.log`. Actual AM installation on the physical console remains the owner's test. The previously served build 31 remains archived privately under `build/debug-published/31`.

## Emulator review, build 72

The Old 3DS Azahar profile completed the general UI/game tour and the corrected room/online Snow tour without an application crash. It visited the Spanish reception, Snow Storm lobby, Battle Ball lobby and Casino; exercised Snow practice and online create/start-with-bots/move/throw/leave; opened Fast Food and Battle Ball practices; and returned to the reception. The navigator showed the three required featured destinations. Japanese selection rendered its font and returned to Spanish. Both completed runs recorded `alive-after-steps` before their planned stop. The tested artifact was the debug 3DSX, not an AM installation of the CIA.

Localization remains incomplete: navigator tabs and Here/Go, catalogue introduction/categories, Snow creation fields and action buttons, and Battle controls retain English. Imported Spanish wording also needs editing (for example, Wave became Onda). These are open findings, not a fully localized acceptance result. Sound remains unverified because DSP initialization fails in this emulator. Emulator speed was below 100%, so the screenshots' FPS do not establish console performance. No texture-load or allocation failure appeared in either client log. Linear free memory increased again after leaving Snow; the short tour does not prove long-session stability.

See [the review and measurements](debug-lan-72/emulator-general/review.json), [Snow lobby](debug-lan-72/emulator-general/rooms-02-snow-overview.png), [online Snow actions](debug-lan-72/emulator-general/rooms-08-snow-online-actions.png), and the adjacent client logs. Reproduce using `tools/emu/steps-debug72-general` and `tools/emu/steps-debug72-rooms` with the matching build-72 artifact. This historical review preceded the corrections below.

## Builds 77–78 corrections

Build 79 adds recovery from a removed saved room to the selected language's
reception and includes imported game rooms in the offline navigator. The actual
console log confirmed build 78 was installed; its obsolete saved room caused
the reported 404. Debug 79 is available through the same LAN updater. See
[the recovery checks](debug-lan-79/README.md) and the separate
[3dsDL download review](UPDATER-3DSDL-REVIEW.md). Production remains unpublished.

Navigator tabs, catalogue groups and front page, Snow settings/controls, Battle teams/results, and the corner buttons use the selected language. Control labels retain the physical button letters. Wobble's animation asset keys no longer pass through translation. Plain translated messages are copied with `%s`; native pack generation rejects missing literal labels and incompatible printf arguments. See [the cross-console review](../../Tweebbo-games-candidate-20261025/docs/games-expansion/PORT-REVIEW.md) and `debug-lan-77/` for retained evidence. Build 78 additionally bounds Japanese toolbar labels and uses compact room-action labels so the physical button letters remain visible. Its exact 3DSX completed a Snow lobby/practice/return and Japanese-to-Spanish switch in Old 3DS emulation; see `debug-lan-78/emulator/` and `debug-lan-78/artifact.sha256`. Six repeated Japanese Snow practice sessions on build 77 retained the same 3,193 KiB linear-free margin in the sampled active matches. These short runs do not certify long-session hardware stability. Physical acceptance and DSP audio remain separate from emulator checks.

## LAN publication verification, build 78

Both served files match the build-78 SHA-256 manifest. The native HTTP fixture downloaded the complete 78,975,936-byte CIA and resumed after 3,145,728 bytes with the same final hash. The 88,934,012-byte 3DSX was independently verified over HTTP. Evidence: `debug-lan-78/publication.json`, `native-download.log` and `artifact.sha256`. The previously served build 72 is archived privately under `build/debug-published/72`. Actual AM installation still requires the physical console.

## Rebuild commands

```sh
TWEEBBO_WEB=../Tweebbo-games-candidate-20261025 TWEEBBO_SERVER=ws://192.168.1.165:18877 ./build.sh debug
make -C tests
make -C tests transport
make -C tests online SERVER=ws://127.0.0.1:18877 WEB_BUILD=18
```

Port 18877 is the isolated candidate hotel; tests must never target a live player database. `out/debug/` contains the CIA/3DSX. The LAN download service serves only a curated `out/debug/share/` directory, not source, SD contents, logs, database backups or credentials.

## Autumn local review — 2026-10-18

The current debug package includes the tabbed wardrobe, fixed half-scale Overview, VIP Dash amounts/animations and the twelve-slot Safe Trading interface with two confirmation phases. The emulator completed a real browser/3DS transfer, including acceptance and both final confirmations, and a server-driven Dash stake/step/cashout. Native host and transport tests, online browser crossplay, all seven Casino games and twelve Node/QuickJS Snow Storm matches passed. The matching web/server source passed 126 Rust tests, 24 PostgreSQL contracts and the complete browser validation pipeline. See `docs/autumn-validation.json` and `../Tweebbo-Vercel/docs/performance/autumn-implementation.json`.

The global M moderator tool remains browser-only. Existing real-device release gates still apply; emulator screenshots do not establish physical-console performance. No public release was uploaded.

## Historical featured Navigator rooms — 2026-10-18 (superseded)

Debug build 29 reads the shared room API metadata: Lobby, Casino, Pool, Diving and Rooftop appear before ordinary rooms. Casino has the original credits bag and ochre background; featured activity labels accompany the color. My Rooms keeps its owner filter and occupancy order. Older servers without the optional metadata still work. The shared policy and required work for future activity rooms are in `../Tweebbo-Vercel/docs/navigator-featured-rooms.md`.
