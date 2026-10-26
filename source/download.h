#pragma once
#include "ws.h"

/* Ordered sink delivery with bounded parallel ranges; unsupported servers use one connection.
   The caller must verify the completed file against its trusted manifest checksum. */
int http_download_optimized_from(const char *url, long from, int connections, HttpSink sink,
                                 void *ctx, long *total, char *err, int err_cap);
