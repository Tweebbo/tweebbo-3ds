#include "incremental.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#ifdef __3DS__
#include <mbedtls/sha256.h>
typedef mbedtls_sha256_context Digest;
static bool digest_start(Digest *d) { mbedtls_sha256_init(d); return !mbedtls_sha256_starts_ret(d, 0); }
static bool digest_add(Digest *d, const void *p, size_t n) { return !mbedtls_sha256_update_ret(d, p, n); }
static bool digest_end(Digest *d, unsigned char out[32]) { return !mbedtls_sha256_finish_ret(d, out); }
static void digest_free(Digest *d) { mbedtls_sha256_free(d); }
#else
#include <openssl/evp.h>
typedef EVP_MD_CTX *Digest;
static bool digest_start(Digest *d) { *d = EVP_MD_CTX_new(); return *d && EVP_DigestInit_ex(*d, EVP_sha256(), NULL) == 1; }
static bool digest_add(Digest *d, const void *p, size_t n) { return EVP_DigestUpdate(*d, p, n) == 1; }
static bool digest_end(Digest *d, unsigned char out[32]) { return EVP_DigestFinal_ex(*d, out, NULL) == 1; }
static void digest_free(Digest *d) { EVP_MD_CTX_free(*d); }
#endif

enum { BUFFER_BYTES = 32768, MAX_OPERATIONS = 65536, MAX_PATH = 240 };
typedef struct { FILE *file; z_stream stream; unsigned char input[BUFFER_BYTES]; bool ended, failed; } Reader;
static bool cancelled(const IncrementalHooks *h) { return h && h->cancel && h->cancel(h->user); }
static void progress(const IncrementalHooks *h, unsigned n) { if (h && h->progress) h->progress(n, h->user); }
static uint32_t le32(const unsigned char *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int hex_digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static bool hex(const char *s, unsigned char out[32]) {
    if (!s || strlen(s) != 64) return false;
    for (unsigned i = 0; i < 32; i++) {
        int a = hex_digit(s[i * 2]), b = hex_digit(s[i * 2 + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = (unsigned char)(a * 16 + b);
    }
    return true;
}

bool incremental_offer(json_t *manifest, const char *from, bool homebrew,
                       bool (*allowed_url)(const char *), IncrementalOffer *out) {
    memset(out, 0, sizeof *out);
    json_t *list = json_object_get(manifest, "incremental");
    if (!json_is_array(list) || json_array_size(list) > 4) return false;
    for (size_t i = 0; i < json_array_size(list); i++) {
        json_t *entry = json_array_get(list, i);
        const char *base = json_string_value(json_object_get(entry, "from"));
        const char *format = json_string_value(json_object_get(entry, "format"));
        if (!base || strcmp(base, from) || strlen(base) >= sizeof out->from || !format || strcmp(format, "tupd1")) continue;
        json_t *artifact = json_object_get(entry, homebrew ? "3dsx" : "cia");
        const char *url = json_string_value(json_object_get(artifact, "url"));
        const char *sha = json_string_value(json_object_get(artifact, "sha256"));
        json_t *size = json_object_get(artifact, "bytes");
        json_int_t bytes = json_integer_value(size);
        unsigned char digest[32];
        if (!url || strlen(url) >= sizeof out->url || !allowed_url || !allowed_url(url) ||
            !hex(sha, digest) || !json_is_integer(size) || bytes < 16 || bytes > INCREMENTAL_MAX_BYTES) continue;
        snprintf(out->from, sizeof out->from, "%s", base);
        snprintf(out->url, sizeof out->url, "%s", url);
        snprintf(out->sha256, sizeof out->sha256, "%s", sha);
        out->bytes = (unsigned)bytes;
        return true;
    }
    return false;
}

bool incremental_verify(const char *path, const char *sha256, unsigned bytes, const IncrementalHooks *hooks) {
    unsigned char expected[32], result[32], buffer[8192];
    if (!bytes || bytes > INCREMENTAL_MAX_BYTES || !hex(sha256, expected)) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = !fseek(f, 0, SEEK_END) && ftell(f) == (long)bytes && !fseek(f, 0, SEEK_SET);
    Digest d = {0};
    bool started = digest_start(&d);
    ok = ok && started;
    while (ok) {
        if (cancelled(hooks)) { ok = false; break; }
        size_t n = fread(buffer, 1, sizeof buffer, f);
        if (!n) { ok = !ferror(f); break; }
        ok = digest_add(&d, buffer, n);
    }
    if (ok) ok = digest_end(&d, result) && !memcmp(result, expected, 32);
    digest_free(&d);
    if (fclose(f)) ok = false;
    return ok;
}

static size_t unpack(Reader *r, void *out, unsigned n) {
    if (r->ended || r->failed) return 0;
    r->stream.next_out = out;
    r->stream.avail_out = n;
    while (r->stream.avail_out) {
        if (!r->stream.avail_in) {
            r->stream.avail_in = (unsigned)fread(r->input, 1, sizeof r->input, r->file);
            r->stream.next_in = r->input;
            if (!r->stream.avail_in) { r->failed = true; break; }
        }
        unsigned before_in = r->stream.avail_in, before_out = r->stream.avail_out;
        int result = inflate(&r->stream, Z_NO_FLUSH);
        if (result == Z_STREAM_END) { r->ended = true; break; }
        if (result != Z_OK || (before_in == r->stream.avail_in && before_out == r->stream.avail_out)) { r->failed = true; break; }
    }
    return n - r->stream.avail_out;
}
static bool exact(Reader *r, void *out, unsigned n) { return unpack(r, out, n) == n; }
static bool safe_path(const unsigned char *p, unsigned n) {
    if (!n || n > MAX_PATH || p[0] == '/' || p[n - 1] == '/') return false;
    unsigned start = 0;
    for (unsigned i = 0; i <= n; i++) {
        if (i < n && (p[i] < 32 || p[i] > 126 || p[i] == '\\' || p[i] == ':')) return false;
        if (i == n || p[i] == '/') {
            unsigned length = i - start;
            if (!length || (length == 1 && p[start] == '.') || (length == 2 && p[start] == '.' && p[start + 1] == '.')) return false;
            start = i + 1;
        }
    }
    return true;
}

IncrementalResult incremental_apply(const char *patch, const char *output, const char *source_root,
    const IncrementalOffer *offer, const IncrementalTarget *target, const IncrementalHooks *hooks,
    char *error, size_t error_size) {
    IncrementalResult result = INCREMENTAL_INVALID;
    const char *message = "The small update is damaged or incompatible.";
    FILE *file = NULL;
    Reader *reader = NULL;
    unsigned char *buffer = NULL, expected[32], digest[32], header[116];
    Digest output_hash = {0};
    bool initialized = false, created = false, hashing = false;
    unsigned total = 0, done = 0;
    if (!patch || !output || !source_root || !offer || !target || !target->title || !target->from ||
        !target->version || !source_root[0] || strlen(target->title) != 16 || !target->from[0] || !target->version[0] || strlen(target->from) > 23 || strlen(target->version) > 23 ||
        !strcmp(patch, output) || strlen(source_root) > 400 || !hex(target->sha256, expected)) goto finish;
    if (!incremental_verify(patch, offer->sha256, offer->bytes, hooks)) goto finish;
    reader = calloc(1, sizeof *reader);
    buffer = malloc(BUFFER_BYTES);
    if (!reader || !buffer) { result = INCREMENTAL_IO_ERROR; message = "Not enough memory to prepare the small update."; goto finish; }
    reader->file = fopen(patch, "rb");
    if (!reader->file || inflateInit(&reader->stream) != Z_OK) goto finish;
    initialized = true;
    if (!exact(reader, header, sizeof header) || memcmp(header, "TUPD001", 8)) goto finish;
    total = le32(header + 12);
    unsigned count = le32(header + 16);
    if (le32(header + 8) != (target->homebrew ? 2u : 1u) || !total || total > INCREMENTAL_MAX_BYTES ||
        (target->bytes && total != target->bytes) || !count || count > MAX_OPERATIONS ||
        memcmp(header + 20, target->title, 16) || !memchr(header + 36, 0, 24) || !memchr(header + 60, 0, 24) ||
        strcmp((char *)header + 36, target->from) || strcmp((char *)header + 60, target->version) ||
        strcmp(offer->from, target->from) || memcmp(header + 84, expected, 32)) goto finish;
    file = fopen(output, "wb");
    if (!file) { result = INCREMENTAL_IO_ERROR; message = "Could not prepare the update. Check the SD card's free space."; goto finish; }
    created = true;
    hashing = digest_start(&output_hash);
    if (!hashing) goto finish;
    for (unsigned i = 0; i < count; i++) {
        if (cancelled(hooks)) goto finish;
        unsigned char command[5];
        if (!exact(reader, command, sizeof command)) goto finish;
        unsigned length = le32(command + 1), left = length;
        if (length > total - done || (command[0] != 1 && command[0] != 2) || (command[0] == 1 && !length)) goto finish;
        FILE *source = NULL;
        Digest source_hash = {0};
        bool source_hashing = false, ok = true;
        unsigned char source_expected[32];
        if (command[0] == 2) {
            unsigned char path[MAX_PATH + 1], size[2];
            if (!exact(reader, size, 2)) goto finish;
            unsigned n = size[0] | (unsigned)size[1] << 8;
            if (!n || n > MAX_PATH || !exact(reader, path, n) || !safe_path(path, n) || !exact(reader, source_expected, 32)) goto finish;
            path[n] = 0;
            char full[768];
            int used = snprintf(full, sizeof full, "%s%s%s", source_root, source_root[strlen(source_root) - 1] == '/' ? "" : "/", path);
            if (used < 0 || (size_t)used >= sizeof full) goto finish;
            source = fopen(full, "rb");
            if (!source || fseek(source, 0, SEEK_END) || ftell(source) != (long)length || fseek(source, 0, SEEK_SET)) {
                if (source) fclose(source);
                result = INCREMENTAL_BASE_MISMATCH; message = "Installed resources do not match. Choose the full download."; goto finish;
            }
            source_hashing = digest_start(&source_hash);
            ok = source_hashing;
        }
        while (ok && left) {
            if (cancelled(hooks)) { ok = false; break; }
            unsigned take = left < BUFFER_BYTES ? left : BUFFER_BYTES;
            if (source) ok = fread(buffer, 1, take, source) == take && digest_add(&source_hash, buffer, take);
            else ok = exact(reader, buffer, take);
            if (ok && fwrite(buffer, 1, take, file) != take) { ok = false; result = INCREMENTAL_IO_ERROR; message = "Could not save the update. Check the SD card's free space."; }
            if (ok) ok = digest_add(&output_hash, buffer, take);
            if (ok) { left -= take; done += take; progress(hooks, done); }
        }
        if (source) {
            if (ok) ok = digest_end(&source_hash, digest) && !memcmp(digest, source_expected, 32);
            digest_free(&source_hash);
            if (fclose(source)) ok = false;
            if (!ok && !cancelled(hooks) && result == INCREMENTAL_INVALID) { result = INCREMENTAL_BASE_MISMATCH; message = "Installed resources do not match. Choose the full download."; }
        }
        if (!ok) goto finish;
    }
    unsigned char extra;
    if (done != total || unpack(reader, &extra, 1) || !reader->ended || reader->failed || reader->stream.avail_in ||
        fgetc(reader->file) != EOF || ferror(reader->file) || !digest_end(&output_hash, digest) || memcmp(digest, expected, 32)) goto finish;
    if (fclose(file)) { file = NULL; result = INCREMENTAL_IO_ERROR; message = "Could not finish saving the update on SD."; goto finish; }
    file = NULL;
    result = INCREMENTAL_OK;
    message = "";
finish:
    digest_free(&output_hash);
    if (file) fclose(file);
    if (reader) {
        if (initialized) inflateEnd(&reader->stream);
        if (reader->file) fclose(reader->file);
    }
    free(reader);
    free(buffer);
    if (cancelled(hooks)) { result = INCREMENTAL_CANCELLED; message = "Small update stopped. Choose Install again to resume."; }
    if (result != INCREMENTAL_OK && created) remove(output);
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return result;
}
