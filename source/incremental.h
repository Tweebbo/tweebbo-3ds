#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <jansson.h>

#define INCREMENTAL_MAX_BYTES (512u * 1024u * 1024u)
typedef struct {
    char from[24], url[512], sha256[65];
    unsigned bytes;
} IncrementalOffer;
typedef struct {
    const char *title, *from, *version, *sha256;
    unsigned bytes;
    bool homebrew;
} IncrementalTarget;
typedef struct {
    bool (*cancel)(void *);
    void (*progress)(unsigned, void *);
    void *user;
} IncrementalHooks;
typedef enum {
    INCREMENTAL_OK, INCREMENTAL_INVALID, INCREMENTAL_BASE_MISMATCH,
    INCREMENTAL_IO_ERROR, INCREMENTAL_CANCELLED
} IncrementalResult;

/* Missing or unsupported offers leave the normal full download available. */
bool incremental_offer(json_t *manifest, const char *from, bool homebrew,
                       bool (*allowed_url)(const char *), IncrementalOffer *out);
bool incremental_verify(const char *path, const char *sha256, unsigned bytes,
                        const IncrementalHooks *hooks);
/* Writes a separate temporary file; a failed reconstruction removes only that file. */
IncrementalResult incremental_apply(const char *patch, const char *output,
    const char *source_root, const IncrementalOffer *offer,
    const IncrementalTarget *target, const IncrementalHooks *hooks,
    char *error, size_t error_size);
