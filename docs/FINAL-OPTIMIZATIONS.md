# Final candidate: download throughput and Fast Food scaling

2026-10-25. Local, unpublished candidate. Debug 79 remains the build served by
the LAN updater and installed for the owner's ongoing test. This work does not
change production's featured-room registry or migrate player data.

## Fast Food

The old native export reduced source art to one quarter of its dimensions with
box filtering and rendered it with nearest-neighbour sampling. The new export
uses one third of the dimensions, premultiplied-alpha Lanczos downsampling and
bilinear GPU sampling. That provides approximately 78% more source texels per
sprite. Deterministic rectangle packing keeps all 205 sprites in the same
1024×1024 RGBA atlas: **4 MiB resident texture memory**, still one page.
Extruded one-pixel borders prevent neighbouring sprites bleeding into one
another. Fonts and other hotel atlases keep their existing sampling.

The generator, lookup table and game bundle must be rebuilt together. Never
change generated coordinates by hand. `tests/fastfood-atlas.py` checks the page
budget, every rectangle and the extruded edges. The native simulation and
rendering tests also passed. Switch and Vita use the same atlas and enable
linear filtering only for Fast Food.

Azahar's Old 3DS/2DS profile reported 11,897 KiB of free linear memory in Fast
Food both before and after this change. Both samples reported 0.5 ms GPU time;
the later practice samples were 9.9 ms CPU / 56 fps before and 10.1 ms / 56 fps
after. These short emulator samples are not physical-device benchmarks.
The screenshots use different guest names and animation phases, so they are
visual references, not a frame-identical image-difference test.

The final Switch shared client and SDL renderer passed three Fast Food
practice open/action/close cycles. The captured scene uses the new atlas and
filtering. This desktop harness does not measure Switch hardware performance.

The instrumented Vita3K run exercised the actual Vita renderer at 6, 12 and 18
Fast Food players. Each close returned to 59,506,688 bytes of free CDRAM and
639,672 bytes of used newlib heap. Fast Food used exactly 4,194,304 additional
CDRAM bytes. These allocation readings are from the Spanish-font test profile;
they should not be compared directly with earlier Japanese-font baselines.
Instrumentation is confined to `out/test/updater-games-backend.vpk`; the normal
VPK is rebuilt without its flag.
The same run also passed all five Battle Ball arenas, Snow Storm, room returns
and Chinese/English/Japanese font replacement.

The local final CIA is 79,795,136 bytes, versus 78,975,936 bytes for debug 79.
Those are different build profiles, but the comparison makes clear that the
improved image detail does not mean a smaller installation/download package.

## Downloads

3DS uses two reusable HTTP/TLS connections for validated range downloads of
at least 1 MiB. Each has a bounded 256 KiB buffer. A single worker delivers
completed pieces in order to the existing SHA-256 and SD-writing path. No new
download threads are created. Bulk sockets request 48 KiB send/receive buffers
before connecting; game sockets retain their settings.

The engine probes range support, requires an exact Content-Range and length,
and sends If-Range with a strong ETag or Last-Modified validator. Unsupported,
interrupted or malformed responses fall back to the existing sequential path
from the last committed byte. The final manifest SHA-256 remains mandatory;
the optimization never authorizes installation of an unchecked file. Redirect
handling, cancellation and partial-file resume were included in fault tests.

All three ports receive the common HTTP length/range validation and buffered
SD-write improvements. Switch and Vita retain one connection by default:
their fast host TLS fixture did not benefit from more connections. Existing
Switch socket configuration and platform TLS verification policies are kept.

The LAN server now supports real ranges, stable SHA-256 ETags, If-Range,
keep-alive and conditional requests. Resuming debug 79 no longer requires
discarding a re-downloaded prefix when served by this server.

## Measurements and checks

The native 3DS probe downloaded and SHA-256-verified a 4 MiB fixture in Azahar:

| Transport | One connection | Two connections | Four connections |
| --- | ---: | ---: | ---: |
| HTTP | 2,892 ms | 1,109 ms | 1,125 ms |
| HTTPS | 8,694 ms | 4,146 ms | 4,434 ms |

Resume from 1 MiB also passed over both transports. Four connections did not
improve this native emulator run, so 3DS defaults to two. A separate controlled
8 MiB fixture limited each connection to roughly 512 KiB/s: two runs took
16.57/16.56 seconds with one connection, 8.08/8.06 with two, and 4.07/4.07 with
four. These numbers demonstrate behavior under those fixture conditions, not
guaranteed speeds over Nintendo hardware, Wi-Fi, GitHub or an SD card.

Completed checks include HTTP fault cases, HTTPS fault cases with both the
3DS and Switch connection backends, ASan/UBSan, the LAN server's range and
atomic-replacement tests, full/resumed debug-79 CIA SHA-256 verification,
native room recovery/transport/online tests, device announcements, language
audit, Fast Food/Battle simulation and render tests, and native package builds.

The initial legacy Switch `tests/run.sh` attempt stopped at linking its UI
fixture. The subsequent publication preparation updated its fixture dependencies,
English-only feature fixtures and offline-room expectations. The full ASan/UBSan
suite now passes on both UI canvases, including the 13-room navigator and its
three featured destinations. Game Center routing is also checked. These fixture
adapters do not replace the real game/renderer integration tests. The original
failure log remains archived as historical evidence. Physical-device acceptance
is still unverified and is disclosed in the public release notes.

Evidence is in [final-optimization/](final-optimization/), including unedited
before/after screenshots, native probe results, fixture reports and build
logs. Ephemeral TLS private keys and player identity files are excluded.
`../../Tweebbo-games-candidate-20261025/docs/games-expansion/candidate-manifest.json`
records current package hashes and shared-source consistency when generated
with `python3 tools/audit-games-candidate.py --3ds-profile release`.

## Reproduction

From the 3DS repository:

```sh
make -C tests downloads
python3 tests/download-server.py
python3 tests/fastfood-atlas.py
make -C tests games
make -C tests recovery transport online SERVER=ws://127.0.0.1:18877 WEB_BUILD=18
python3 tools/audit-language-ui.py source ../Tweebbo-Switch/source ../Tweebbo-Vita/source
```

The native probe is `tests/3ds-download.c`; its isolated fixture server is
`tests/download-probe-server.py`. Use the archived build recipe with ephemeral
local certificate paths. For graphics, use a LAN-only showcase build and
`tools/emu/steps-fastfood-quality`. Never launch the public-profile final CIA
against production as an automated test.
