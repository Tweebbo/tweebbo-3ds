# Online: crossplay with tweebbo.lol

The 3DS speaks the same binary protocol (version 6) as the web client over a WebSocket at `/ws/<room>` and uses the same HTTP API (`/api/rooms`, `/api/identity`, `/api/social`).

## Transport (`source/conn.c`, `source/ws.c`, `source/net.c`)

- libctru sockets (`socInit`, 1 MB buffer). The console uses a blocking `connect` on the network thread, then non-blocking reads with `poll`.
- TLS with mbedTLS 2.28 (devkitPro portlibs): mandatory verification against the embedded roots (`source/certs.c`), SNI and TLS 1.2. Entropy comes from `sslcGenerateRandomData`, then `ps:ps`, then the system clock. Update `certs.c` if the domain changes certificate authority.
- Own WebSocket client (RFC 6455): HTTP/1.1 handshake, masked frames, ping/pong, fragments and close.
- HTTP/1.1 with `Connection: close`, `Content-Length` or `chunked`; large downloads use HTTP/1.0 and follow up to five redirects.
- One network thread with a higher priority than drawing (it mostly waits in `poll`) and another thread for HTTP requests, with `LightLock`-protected queues; the main loop hands messages to `world_handle`.
- Exponential reconnection with jitter (1, 2, 4, 8 s… up to 15 s) and renewal after 240 s (Vercel closes at 270 s), reusing the token so the server recovers the same entity. The chat is not cleared on renewal, only when the room changes.
- Room changes: `net_goto(slug, arrival)` closes and opens `/ws/<slug>`. TRANSFER (teleporters) uses its arrival ticket once.
- A network failure never switches to the offline simulation; the error is shown and saved in `last-error.txt`.

The same code builds on the PC: `make -C tests online` (no TLS, local server) and `make -C tests tls` (real TLS against `https://tweebbo.lol/healthz` in `ubuntu:24.04`, which ships the same mbedTLS version).

## Identity

- HELLO = `u16 version, str token, str arrival, u8 platform, str password, u8 capabilities` with `platform = 1`. Trailing password and capabilities are optional for older clients. The current native client (1.0.8) advertises `126`: structured status/identity messages (`2`), board games (`4`), the Casino (`8`), Snow Storm (`16`, and `32` for 180 ms turns) and room rights (`64`). Then `u8 device` (`1` Nintendo 3DS) and `u8 features`: `1` is the economy update (VIP Dash with credits, furniture trading with c2s TRADE 21 / s2c TRADE 139, the Tweebbo Club flag `1` and badges). The top bit of the capability byte is reserved by the server. The server preserves the console bit while checking capabilities, always plays console Casino rounds with credits, and ignores c2s for chips. s2c CHIPS (138) and DEVICE (140) are ignored or decorative on the console.
- Without a token the server creates a guest and returns it in WELCOME. Normal builds store it in `sdmc:/3ds/Tweebbo/token-<host>.txt`; debug uses `Tweebbo-debug/`. Nonstandard ports are part of the filename, preventing identities from mixing between local test hotels. Passwords remain in memory only.
- *Me → Character code*: "Show code" reveals the private code (hidden by default); "Use a code" checks it with `POST /api/identity` and, after confirming the name, reconnects as that character. The code is never logged. *Support Tweebbo* has a "Show code" shortcut to it.
- *Me → Move…*: one-time move of the character to another device (web `POST /api/transfer`, `GET /api/transfer`, `POST /api/transfer/claim`). "Get a transfer code" shows an 8-character code valid for 10 minutes and waits for the other device; "Enter a code from another device" checks the code, asks for confirmation and reconnects with the new access code. Each character moves only once.
- Looks are packed in at most 42 bits (bits 37–41 hold the high bits of the later hair, bottoms and shoes of the web wardrobe; `look_decode` reads them and items the console does not have fall back to the first one).

## Platform flag (server and web)

- `proto.rs`: optional trailing HELLO byte (`PLATFORM_3DS = 1`; older clients omit it and count as 0).
- The server keeps the platform per entity and adds bit `64` (`FLAG_3DS`) to `flags` in `ent` and DELTAS, without storing it in the posture state. When the same account comes back from another device the bit is updated.
- The web draws a 12×13 px pixel-art handheld over the head; the profile reads "This person is connected through a Nintendo 3DS system.".

## Known limits

- Casino game windows and Snow Storm still need native clients. The Casino room and furniture render, but the server deliberately rejects these game sessions on consoles. Do not remove those guards or advertise full feature parity until the implementations and crossplay tests exist.

- The system font lacks some characters: titles with emoji or some non-Latin scripts show "?".
- Sharing on X / Twitter is web-only.
- Furniture loads per room within a budget computed at start; on Old 3DS the least recently used textures are freed in heavily decorated rooms.

## Local debug connection states

Only WELCOME marks the connection online. A notice or an alert cannot do so. Locked rooms wait for a password or a return to the lobby. Capacity messages retain the server retry delay (30–3,600 seconds); only a room-capacity refusal offers an immediate lobby fallback. Moderation refusals and session takeover pause reconnection until the player acts.

Incoming/outgoing queues are bounded. Overflow requests a fresh room snapshot instead of silently losing deltas. This does not replace server-side admission limits or a real hardware load test.

`make -C tests transport` checks connection states against a local WebSocket fixture. `make -C tests online SERVER=ws://127.0.0.1:18093` also exercises native/browser board-game messages against an isolated hotel. Never use production or a live local player database for test fixtures.
