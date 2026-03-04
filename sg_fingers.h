/*
 * sg_fingers.h — Shared finger name table + security level helpers
 *
 * Used by sg_enroll.c and test files. Provides canonical finger names
 * and SDK security level string-to-enum mapping.
 */

#ifndef SG_FINGERS_H
#define SG_FINGERS_H

#include <string.h>
#include <strings.h>  /* strcasecmp */
#include "sgfplib.h"

/* ── Finger names ──────────────────────────────────────────── */

#define NUM_FINGERS 10

static const char *FINGER_NAMES[NUM_FINGERS] = {
    "right-thumb",  "right-index",  "right-middle",  "right-ring",  "right-little",
    "left-thumb",   "left-index",   "left-middle",   "left-ring",   "left-little",
};

static int valid_finger_name(const char *name)
{
    if (!name || !name[0])
        return 0;
    for (int i = 0; i < NUM_FINGERS; i++) {
        if (strcmp(name, FINGER_NAMES[i]) == 0)
            return 1;
    }
    return 0;
}

/* ── Security levels ───────────────────────────────────────── */

/*
 * SDK security level -> score threshold mapping (from FDx SDK Pro Manual 3.13)
 * Index matches SGFDxSecurityLevel enum values (SL_NONE=0 .. SL_HIGHEST=9)
 */
#define NUM_SECURITY_LEVELS 10

static const DWORD SECURITY_SCORES[NUM_SECURITY_LEVELS] = {
    0, 30, 50, 60, 70, 80, 90, 100, 120, 140
};

static const char *SECURITY_NAMES[NUM_SECURITY_LEVELS] = {
    "none", "lowest", "lower", "low", "below_normal",
    "normal", "above_normal", "high", "higher", "highest"
};

/*
 * Parse a security level name to its enum value and score threshold.
 * Case-insensitive. Returns 0 on success, -1 on invalid name.
 */
static int parse_security_level(const char *name, DWORD *level, DWORD *score)
{
    if (!name || !name[0])
        return -1;
    for (int i = 0; i < NUM_SECURITY_LEVELS; i++) {
        if (strcasecmp(name, SECURITY_NAMES[i]) == 0) {
            if (level) *level = (DWORD)i;
            if (score) *score = SECURITY_SCORES[i];
            return 0;
        }
    }
    return -1;
}

#endif /* SG_FINGERS_H */
