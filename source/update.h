// In-game updates: each GitHub release publishes latest.json
// (version, CIA URL, SHA-256 and notes). When there is a newer version the console
// downloads it, verifies it and installs it without FBI.
#pragma once
#include "common.h"

/** Manifest of the latest release (version, cia, 3dsx, sha256, notes). */
#define UPDATE_REPO "https://github.com/Tweebbo/tweebbo-3ds"
#define UPDATE_MANIFEST UPDATE_REPO "/releases/latest/download/latest.json"

typedef enum { UPD_IDLE, UPD_CHECKING, UPD_LATEST, UPD_AVAILABLE, UPD_DOWNLOADING, UPD_INSTALLED, UPD_FAILED } UpdateState;

/** Fetches the update manifest (in the background). */
void update_check(void);
UpdateState update_state(void);
const char *update_version(void);
/** The running version as compared by the updater (debug builds add their build number). */
const char *update_current(void);
/** Background check that never shows a checking state or a transient error. */
void update_check_quiet(void);
/** Increases whenever a different update is found (re-shows a dismissed offer). */
int update_offer(void);
const char *update_notes(void);
const char *update_error(void);
/** 0–1 while downloading. */
float update_progress(void);
/** Downloads and installs (CIA through AM; .3dsx by overwriting the file). */
void update_start(void);
/** After installing: restarts Tweebbo (CIA) or asks to reopen it (.3dsx). Returns true when the app must exit. */
bool update_restart(void);
/** Compares "a.b.c" versions: >0 if a is newer. */
int version_cmp(const char *a, const char *b);
/** Path of the running .3dsx (empty for the installed CIA). */
void update_set_launch_path(const char *argv0);

/** User-visible update phase and chosen network transfer size. */
const char *update_status(void);
bool update_small(void);
bool update_full_fallback(void);
long update_download_bytes(void);
long update_full_bytes(void);
