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

- HELLO = `u16 version, str token, str arrival, u8 platform` with `platform = 1`. Without a token the server creates a guest and returns it in WELCOME; it is saved in `sdmc:/3ds/Tweebbo/token-<host>.txt` (one per server).
- *Me → Save / restore character*: "Show code" reveals the private code (hidden by default); "Use a code" checks it with `POST /api/identity` and, after confirming the name, reconnects as that character. The code is never logged.

## Platform flag (server and web)

- `proto.rs`: optional trailing HELLO byte (`PLATFORM_3DS = 1`; older clients omit it and count as 0).
- The server keeps the platform per entity and adds bit `64` (`FLAG_3DS`) to `flags` in `ent` and DELTAS, without storing it in the posture state. When the same account comes back from another device the bit is updated.
- The web draws a 12×13 px pixel-art handheld over the head; the profile reads "This person is connected through a Nintendo 3DS system.".

## Known limits

- The system font lacks some characters: titles with emoji or some non-Latin scripts show "?".
- Sharing on X / Twitter is web-only.
- Furniture loads per room within a budget computed at start; on Old 3DS the least recently used textures are freed in heavily decorated rooms.
