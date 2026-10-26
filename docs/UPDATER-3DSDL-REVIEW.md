# 3dsDL updater review

Implementation and measured local results are now recorded in
[FINAL-OPTIMIZATIONS.md](FINAL-OPTIMIZATIONS.md). The review below describes the
pre-optimization state of debug 79.

Read-only review of [3dsDL at ea5f53c](https://github.com/ankushKun/3dsDL/tree/ea5f53ccf1545dbd6282f4eefca3b0810d44c501),
2026-10-25. No third-party implementation was imported and no download-speed
change was included in debug 79.

Useful techniques, in implementation order:

1. Support real HTTP range responses on the debug download server. A GET for
   `Range: bytes=0-0` against the LAN debug CIA returned 200 and the entire
   78,975,936-byte content length, with no Content-Range. The current Python
   SimpleHTTP server ignores the range. Tweebbo's `http_download_from` preserves
   integrity by reading and discarding the prefix when a server returns 200, but
   that costs bandwidth. This observation applies to the LAN server, not to an
   unmeasured GitHub release endpoint.
2. Measure per-socket receive/send buffers configured before connect. The
   [3dsDL socket callback](https://github.com/ankushKun/3dsDL/blob/ea5f53ccf1545dbd6282f4eefca3b0810d44c501/source/net/download_engine.cpp)
   does this to establish the TCP window. Tweebbo's 1 MiB SOC allocation is not
   the same as the per-socket window. New 3DS CPU boost is already enabled.
3. Evaluate two connections, then four, only with confirmed ranges, a stable
   ETag/Last-Modified validator, bounded buffers and a single-connection fallback.
   Tweebbo's downloader has static request/URL arrays and is not reentrant;
   starting several existing downloader threads would be incorrect. Request
   state, TLS concurrency and the memory budget need review first.
4. Expose download speed and remaining bytes/time, and distinguish download,
   verification and installation. Larger buffered writes and DNS/TLS session
   reuse are candidates for measurement, not guaranteed improvements.

Keep Tweebbo's streamed SHA-256 check, build-bound partial file, retries and
verified-file installation. Those already exist. The general 3dsDL download
engine supports resume, but its reviewed self-update call explicitly sets
`allowResume = false`; copying that call would lose an existing Tweebbo feature.

The upstream [hardware notes](https://github.com/ankushKun/3dsDL/blob/ea5f53ccf1545dbd6282f4eefca3b0810d44c501/docs/HARDWARE.md)
record 500–600 KiB/s for a 38 MB video over four connections and explicitly lack
a single-connection comparison for that file/network. They do not establish a
2x or 4x speedup. Our earlier FTP upload is a different protocol and is not an
HTTPS baseline. Compare the same CIA, server, SD card and network with one, two
and four connections on Old and New 3DS before choosing a default.
