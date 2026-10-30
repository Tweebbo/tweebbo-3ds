#include "incremental.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned done, stop;
static bool cancel(void *p) { (void)p; return stop && done >= stop; }
static void progress(unsigned n, void *p) { (void)p; done = n; }
static bool url(const char *s) { return !strncmp(s, "https://updates.example/", 24); }
int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "offer")) {
        json_error_t error; json_t *j = json_load_file(argv[2], JSON_REJECT_DUPLICATES, &error);
        IncrementalOffer offer = {0}; bool found = j && incremental_offer(j, "1.2.1", atoi(argv[3]) != 0, url, &offer);
        printf("%s %u\n", found ? "MATCH" : "FULL", offer.bytes); if (j) json_decref(j); return 0;
    }
    if (argc < 13) return 10;
    IncrementalOffer offer = {0}; snprintf(offer.from, sizeof offer.from, "%s", argv[6]);
    snprintf(offer.sha256, sizeof offer.sha256, "%s", argv[11]); offer.bytes = (unsigned)strtoul(argv[12], NULL, 10);
    IncrementalTarget target = {argv[5], argv[6], argv[7], argv[8], (unsigned)strtoul(argv[9], NULL, 10), atoi(argv[10]) != 0};
    if (argc > 13) stop = (unsigned)strtoul(argv[13], NULL, 10);
    IncrementalHooks hooks = {cancel, progress, NULL}; char error[200];
    IncrementalResult result = incremental_apply(argv[2], argv[3], argv[4], &offer, &target, &hooks, error, sizeof error);
    printf("%u %u %s\n", (unsigned)result, done, error);
    return result;
}
