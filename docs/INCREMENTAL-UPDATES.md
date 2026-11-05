# Incremental updates and Old 3DS work

## Current local updater preparation

The 2026-11-05 candidate separates preparation from installation. Choose
**Prepare / resume**, then **Keep playing**. Downloads, saved-download checks and
patch reconstruction run in the existing lower-priority worker. The progress
window distinguishes phases and shows byte totals, transfer speed and an
estimated remaining download time. **Pause** retains progress. Help > **Update
progress** reopens the panel; the room remains playable while preparation runs.

The offer includes **Use full download** and **Use smaller update** when a
compatible recipe exists. A smaller network download can require more SD work;
the player can choose either method before preparation rather than waiting for a
patch failure. Both methods keep SHA-256, length and package-identity checks.

Preparation ends at **Update ready**. It never replaces the running application
automatically. **Install now** rechecks the staged file with the same 128 KiB
worker buffer and then commits it. CIA installation protects HOME/sleep until AM
finishes; reopening activates the new executable. Closing during preparation
joins the worker before network/RomFS teardown and retains resumable files.
Sleep suspends preparation. Socket/header/TLS reads observe cancellation;
platform DNS or a blocking initial connection still depends on the system timeout.

Frame processing pressure yields preparation work. Initial work ceilings are
1 MiB/s on Old models and 2 MiB/s on New models; these are cooperative work budgets,
not promised Wi-Fi or SD speeds. No graphics quality, texture allocation budget,
console clock policy or update-buffer size was increased. The existing two
256 KiB download ranges remain bounded.

`python3 tests/update-background.py` exercises the production worker with real
files, SHA and threads plus mocked AM/transport: preparation without replacement,
manual full-download choice, legacy manifests, pause, cancellation, cross-process
resume, staged corruption rejected before AM and installer faults. Incremental
reader and real HTTP range/fault tests remain relevant. Emulator evidence is in
`../../review/modernization-20261105/`; physical-device installation, radio/SD
throughput and battery measurements remain separate. This candidate is local.

Available starting with 1.3.6. Clients through 1.3.5 need one complete bridge update
before they can use small downloads for subsequent releases. Updates are optional;
players can choose Later and keep playing on their existing version.

## Small downloads

The updater accepts optional `tupd1` offers in the existing release manifest. An
offer must match the running version exactly and select the correct CIA/3DSX kind.
Unsupported versions and old clients retain the normal complete-package path.
The panel shows the selected download size and the full package size.

The compressed recipe combines changed bytes with unchanged files from the
running application's `romfs:/`. No saved previous CIA is required. The complete
new package is reconstructed on SD, so enough space for that package is still
needed. Installation and final verification remain necessary after downloading.

The reader verifies patch SHA-256/size, each reused file, project Title ID,
source/target versions, format, exact output size and the complete output SHA-256.
Unsafe paths, truncated streams, trailing data, output over 512 MiB and more than
65,536 operations are rejected. Processing uses bounded 32 KiB buffers.
Normal CIA identity validation runs before AM installation. The 3DSX path uses
atomic replacement and restores the previous app if FAT cannot publish the new file.

Interrupted downloads resume. A recipe mismatch, missing/damaged base file or
checksum failure offers an explicit Full download action. It never silently
downloads the large package. START, HOME and sleep are guarded during update work.
The existing app stays available after a failed download or reconstruction.

## Publishing

Prepare public artifacts with a new version and retain the previous published
directory, including its original manifest and both complete artifacts. The normal
release command can generate and upload small artifacts as well:

```sh
INCREMENTAL_BASE=/path/to/previous-release \
NOTES='Optional update. Install whenever convenient; no immediate update is required.' \
./build.sh release
```

This command publishes to GitHub and requires the normal release authorization.
For local preparation only, use `tools/prepare-incremental.py --help`. It accepts
up to four `--base` directories, verifies full artifact hashes and release URLs,
and adds only offers smaller than the full download. The old flat manifest keys
stay intact, with optional `3dsx_size` and `incremental` fields. Upload `.tupd`
assets alongside the full CIA, 3DSX and manifest; preserve the full fallback.

## Old 3DS lookup work

Entity lookup caches ID-to-slot hits and validates the current slot owner before
reuse. Furniture lookup caches loaded definition/page pairs before catalogue and
texture-slot searches. Owner validation and the original fallback scan preserve
correctness during collisions, roster replacement, removal and texture eviction.
The two fixed caches consume 8 KiB total. Texture quality, texture budgets and
the server protocol stay unchanged.

Existing parsed-text caching, draw sorting, offscreen avatar culling, on-demand
furniture budgets and per-species pet textures remain in place. A host benchmark
of four million entity lookups measured 113.58 ms before and 35.25 ms after;
this is not a console FPS measurement. Physical Old 3DS acceptance remains pending.

## Checks

```sh
python3 tests/incremental.py
python3 tests/incremental-generator.py
python3 tests/incremental-worker.py
make -C tests all lookup-cache
make -C tests online SERVER=ws://127.0.0.1:ISOLATED_PORT
```

Host incremental tests need Jansson headers/runtime, OpenSSL and zlib; `CPPFLAGS`
may supply the Jansson include directory. The worker executes production updater
code with mocked network/AM services and real file/hash handling. Tests cover old
manifests, small updates, resume, explicit full fallback, wrong titles, short writes,
AM failures and atomic replacement/restore. Integration tests must use an isolated
local hotel and synthetic identities, never production or an active player database.
