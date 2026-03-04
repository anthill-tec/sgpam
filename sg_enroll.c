/*
 * sg_enroll.c — Enroll a fingerprint for a user
 *
 * Captures two samples, confirms they match, then writes the template to
 * /etc/security/sg_fingerprints/<username>.tpl (root-only, mode 0600).
 *
 * Usage:  sudo sg_enroll <username>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "sgfplib.h"

/* ── tunables ─────────────────────────────────────────────── */
#define TEMPLATE_DIR     "/etc/security/sg_fingerprints"
#define DEVICE_NAME      SG_DEV_FDU05
#define CAPTURE_TIMEOUT  15000
#define CAPTURE_QUALITY  50               /* higher floor for enrolment */
#define SECURITY_LEVEL   SL_NORMAL        /* confirm two samples match  */
#define TEMPLATE_FORMAT  TEMPLATE_FORMAT_SG400

/* ── utilities ────────────────────────────────────────────── */

static void die(const char *msg, DWORD code)
{
    fprintf(stderr, "ERROR: %s (code %lu)\n", msg, code);
    exit(1);
}

static int capture_and_extract(HSGFPM hFPM,
                                BYTE *imgBuf, DWORD imgW, DWORD imgH,
                                BYTE *tmplBuf, const char *prompt)
{
    printf("%s\n", prompt);
    fflush(stdout);

    DWORD err = SGFPM_GetImageEx(hFPM, imgBuf, CAPTURE_TIMEOUT, NULL, CAPTURE_QUALITY);
    if (err != SGFDX_ERROR_NONE) {
        fprintf(stderr, "  Capture failed (err %lu). Try again.\n", err);
        return -1;
    }

    DWORD quality = 0;
    SGFPM_GetImageQuality(hFPM, imgW, imgH, imgBuf, &quality);
    printf("  Image quality: %lu/100\n", quality);
    if (quality < CAPTURE_QUALITY) {
        fprintf(stderr, "  Quality too low (%lu). Please try again.\n", quality);
        return -1;
    }

    err = SGFPM_CreateTemplate(hFPM, NULL, imgBuf, tmplBuf);
    if (err != SGFDX_ERROR_NONE) {
        fprintf(stderr, "  Minutiae extraction failed (err %lu).\n", err);
        return -1;
    }
    return 0;
}

/* ── main ─────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <username>\n", argv[0]);
        return 1;
    }
    if (geteuid() != 0) {
        fprintf(stderr, "Must be run as root: sudo %s %s\n", argv[0], argv[1]);
        return 1;
    }

    const char *username = argv[1];

    /* SDK init */
    HSGFPM hFPM = NULL;
    DWORD err = SGFPM_Create(&hFPM);
    if (err != SGFDX_ERROR_NONE) die("SGFPM_Create", err);

    err = SGFPM_Init(hFPM, DEVICE_NAME);
    if (err != SGFDX_ERROR_NONE) die("SGFPM_Init — check SDK libs are installed", err);

    SGFPM_SetTemplateFormat(hFPM, TEMPLATE_FORMAT);

    err = SGFPM_OpenDevice(hFPM, USB_AUTO_DETECT);
    if (err != SGFDX_ERROR_NONE) die("SGFPM_OpenDevice — is the U20 plugged in?", err);

    SGDeviceInfoParam devInfo;
    memset(&devInfo, 0, sizeof(devInfo));
    SGFPM_GetDeviceInfo(hFPM, &devInfo);
    printf("Scanner ready: %lu x %lu px @ %lu DPI\n\n",
           devInfo.ImageWidth, devInfo.ImageHeight, devInfo.ImageDPI);

    BYTE *imgBuf = malloc(devInfo.ImageWidth * devInfo.ImageHeight);
    DWORD maxTmplSize = 0;
    SGFPM_GetMaxTemplateSize(hFPM, &maxTmplSize);
    BYTE *tmpl1 = malloc(maxTmplSize);
    BYTE *tmpl2 = malloc(maxTmplSize);

    if (!imgBuf || !tmpl1 || !tmpl2) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    /* Capture sample 1 — retry up to 3 times */
    int ok = 0;
    for (int attempt = 0; attempt < 3 && !ok; attempt++) {
        ok = (capture_and_extract(hFPM, imgBuf,
                                  devInfo.ImageWidth, devInfo.ImageHeight,
                                  tmpl1,
                                  "→ Place finger on scanner (sample 1)...") == 0);
    }
    if (!ok) { fprintf(stderr, "Failed to capture sample 1.\n"); return 1; }

    printf("  Sample 1 captured. Remove finger and wait...\n\n");
    sleep(2);

    /* Capture sample 2 — retry up to 3 times */
    ok = 0;
    for (int attempt = 0; attempt < 3 && !ok; attempt++) {
        ok = (capture_and_extract(hFPM, imgBuf,
                                  devInfo.ImageWidth, devInfo.ImageHeight,
                                  tmpl2,
                                  "→ Place the same finger again (sample 2)...") == 0);
    }
    if (!ok) { fprintf(stderr, "Failed to capture sample 2.\n"); return 1; }

    /* Confirm the two samples match before saving */
    printf("\nVerifying samples match...\n");
    BOOL matched = FALSE;
    err = SGFPM_MatchTemplate(hFPM, tmpl1, tmpl2, SECURITY_LEVEL, &matched);
    if (err != SGFDX_ERROR_NONE || !matched) {
        fprintf(stderr,
                "Samples did not match (err %lu, matched=%d).\n"
                "Please run sg_enroll again with the same finger.\n",
                err, matched);
        SGFPM_CloseDevice(hFPM);
        SGFPM_Terminate(hFPM);
        free(imgBuf); free(tmpl1); free(tmpl2);
        return 1;
    }

    DWORD score = 0;
    SGFPM_GetMatchingScore(hFPM, tmpl1, tmpl2, &score);
    printf("  Match confirmed (score: %lu/199)\n\n", score);

    /* Save template */
    if (mkdir(TEMPLATE_DIR, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "Cannot create %s: %s\n", TEMPLATE_DIR, strerror(errno));
        return 1;
    }
    chmod(TEMPLATE_DIR, 0700);

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.tpl", TEMPLATE_DIR, username);

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Cannot write %s: %s\n", path, strerror(errno));
        return 1;
    }
    fwrite(tmpl1, 1, maxTmplSize, f);
    fclose(f);
    chmod(path, 0600);

    printf("✓ Fingerprint enrolled for '%s'\n  Template saved: %s\n", username, path);

    SGFPM_CloseDevice(hFPM);
    SGFPM_Terminate(hFPM);
    free(imgBuf); free(tmpl1); free(tmpl2);
    return 0;
}
