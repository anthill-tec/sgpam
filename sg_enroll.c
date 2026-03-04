/*
 * sg_enroll.c — Enroll a fingerprint for a user
 *
 * Captures two samples, confirms they match with confidence validation,
 * then writes the template to
 * /etc/security/sg_fingerprints/<username>_<finger>.tpl (root-only, mode 0600).
 * Optionally verifies the saved template works with a fresh scan.
 *
 * Usage:
 *   sudo sg_enroll <username> [finger-name] [-s LEVEL]
 *   sudo sg_enroll --list <username>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <glob.h>

#include "sgfplib.h"
#include "sg_fingers.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* ── tunables ─────────────────────────────────────────────── */
#ifndef TEMPLATE_DIR
#define TEMPLATE_DIR     "/etc/security/sg_fingerprints"
#endif
#define DEVICE_NAME      SG_DEV_FDU05
#define CAPTURE_TIMEOUT  15000
#define CAPTURE_QUALITY  60               /* raised to 60 for enrollment */
#define DEFAULT_SECURITY SL_NORMAL        /* default security level      */
#define TEMPLATE_FORMAT  TEMPLATE_FORMAT_SG400

/* ── utilities ────────────────────────────────────────────── */

#define USERNAME_MAX 256  /* LOGIN_NAME_MAX on Linux */

static int valid_username(const char *name)
{
    if (!name || !name[0])
        return 0;

    size_t len = 0;
    for (const char *p = name; *p; p++, len++) {
        char c = *p;
        int ok = (c >= 'a' && c <= 'z') ||
                 (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') ||
                 c == '_' || c == '-' || c == '.';
        if (!ok)
            return 0;
    }
    return len > 0 && len < USERNAME_MAX;
}

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

/* ── finger selection ─────────────────────────────────────── */

static const char *select_finger(void)
{
    printf("Select finger to enroll:\n");
    for (int i = 0; i < NUM_FINGERS; i++)
        printf("  %2d. %s\n", i + 1, FINGER_NAMES[i]);
    printf("\nChoice [1-%d]: ", NUM_FINGERS);
    fflush(stdout);

    int choice = 0;
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > NUM_FINGERS) {
        fprintf(stderr, "Invalid choice.\n");
        return NULL;
    }
    /* consume trailing newline */
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
    return FINGER_NAMES[choice - 1];
}

/* ── list enrolled fingers ────────────────────────────────── */

static int list_enrolled_fingers(const char *username)
{
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s/%s_*.tpl", TEMPLATE_DIR, username);

    glob_t globbuf;
    int grc = glob(pattern, 0, NULL, &globbuf);

    /* Also check legacy */
    char legacy_path[512];
    snprintf(legacy_path, sizeof(legacy_path), "%s/%s.tpl", TEMPLATE_DIR, username);
    int has_legacy = (access(legacy_path, R_OK) == 0);

    int total = (grc == 0 ? (int)globbuf.gl_pathc : 0) + (has_legacy ? 1 : 0);

    if (total == 0) {
        printf("No enrolled fingers for '%s'\n", username);
        if (grc == 0) globfree(&globbuf);
        return 0;
    }

    printf("Enrolled fingers for '%s':\n", username);

    if (grc == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
            /* Extract finger name from path: <dir>/<user>_<finger>.tpl */
            const char *base = strrchr(globbuf.gl_pathv[i], '/');
            base = base ? base + 1 : globbuf.gl_pathv[i];
            /* Skip "<user>_" prefix */
            const char *finger = base + strlen(username) + 1;
            /* Remove .tpl suffix for display */
            size_t flen = strlen(finger);
            if (flen > 4)
                printf("  - %.*s\n", (int)(flen - 4), finger);
        }
        globfree(&globbuf);
    }

    if (has_legacy)
        printf("  - (legacy single-finger)\n");

    return 0;
}

/* ── usage ────────────────────────────────────────────────── */

static void print_usage(const char *argv0)
{
    fprintf(stderr,
        "Usage:\n"
        "  sudo %s <username> [finger-name] [-s LEVEL]\n"
        "  sudo %s --list <username>\n"
        "\n"
        "Options:\n"
        "  -s LEVEL   Security level: lowest, lower, low, below_normal,\n"
        "             normal (default), above_normal, high, higher, highest\n"
        "  --list     List enrolled fingers\n"
        "\n"
        "Finger names:\n"
        "  right-thumb  right-index  right-middle  right-ring  right-little\n"
        "  left-thumb   left-index   left-middle   left-ring   left-little\n",
        argv0, argv0);
}

/* ── main ─────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    /* Parse arguments */
    const char *username = NULL;
    const char *finger = NULL;
    const char *sec_name = NULL;
    int list_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            list_mode = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option -s requires an argument\n");
                return 1;
            }
            sec_name = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else if (!username) {
            username = argv[i];
        } else if (!finger) {
            finger = argv[i];
        } else {
            fprintf(stderr, "Too many arguments\n");
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!username) {
        print_usage(argv[0]);
        return 1;
    }

    /* Root required for all operations (template dir is 0700) */
    if (geteuid() != 0) {
        fprintf(stderr, "Must be run as root: sudo %s ...\n", argv[0]);
        return 1;
    }

    if (!valid_username(username)) {
        fprintf(stderr, "Invalid username '%s' — "
                "only alphanumeric, underscore, hyphen, dot allowed\n", username);
        return 1;
    }

    if (list_mode)
        return list_enrolled_fingers(username);

    /* Parse security level */
    DWORD securityLevel = DEFAULT_SECURITY;
    DWORD scoreThreshold = SECURITY_SCORES[DEFAULT_SECURITY];
    if (sec_name) {
        if (parse_security_level(sec_name, &securityLevel, &scoreThreshold) != 0) {
            fprintf(stderr, "Unknown security level '%s'\n", sec_name);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Validate or interactively select finger */
    if (finger) {
        if (!valid_finger_name(finger)) {
            fprintf(stderr, "Unknown finger name '%s'\n", finger);
            print_usage(argv[0]);
            return 1;
        }
    } else if (isatty(STDIN_FILENO)) {
        finger = select_finger();
        if (!finger) return 1;
    } else {
        fprintf(stderr, "Finger name required in non-interactive mode\n");
        print_usage(argv[0]);
        return 1;
    }

    umask(0077);  /* ensure template dir and files are never world-accessible */

    /* Build template path */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_%s.tpl", TEMPLATE_DIR, username, finger);

    /* Check existing enrollment */
    if (access(path, F_OK) == 0 && isatty(STDIN_FILENO)) {
        printf("Template already exists for '%s' finger '%s'.\n", username, finger);
        printf("Overwrite? [y/N] ");
        fflush(stdout);
        int c = getchar();
        /* consume rest of line */
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
            ;
        if (c != 'y' && c != 'Y') {
            printf("Cancelled.\n");
            return 0;
        }
    }

    /* SDK init */
    HSGFPM hFPM    = NULL;
    int devOpened   = 0;
    BYTE *imgBuf    = NULL;
    BYTE *tmpl1     = NULL;
    BYTE *tmpl2     = NULL;
    BYTE *verifyTmpl = NULL;
    DWORD maxTmplSize = 0;
    int ret = 1;

    DWORD err = SGFPM_Create(&hFPM);
    if (err != SGFDX_ERROR_NONE) die("SGFPM_Create", err);

    err = SGFPM_Init(hFPM, DEVICE_NAME);
    if (err != SGFDX_ERROR_NONE) die("SGFPM_Init — check SDK libs are installed", err);

    SGFPM_SetTemplateFormat(hFPM, TEMPLATE_FORMAT);

    err = SGFPM_OpenDevice(hFPM, USB_AUTO_DETECT);
    if (err != SGFDX_ERROR_NONE) die("SGFPM_OpenDevice — is the U20 plugged in?", err);
    devOpened = 1;

    SGDeviceInfoParam devInfo;
    memset(&devInfo, 0, sizeof(devInfo));
    SGFPM_GetDeviceInfo(hFPM, &devInfo);
    printf("Scanner ready: %lu x %lu px @ %lu DPI\n\n",
           devInfo.ImageWidth, devInfo.ImageHeight, devInfo.ImageDPI);

    /* Guard against overflow: U20 is 260x300; reject anything absurd */
    if (devInfo.ImageWidth == 0 || devInfo.ImageHeight == 0 ||
        devInfo.ImageWidth > 4096 || devInfo.ImageHeight > 4096) {
        fprintf(stderr, "Suspicious image dimensions %lux%lu\n",
                devInfo.ImageWidth, devInfo.ImageHeight);
        goto cleanup;
    }

    imgBuf = malloc(devInfo.ImageWidth * devInfo.ImageHeight);
    SGFPM_GetMaxTemplateSize(hFPM, &maxTmplSize);
    tmpl1 = malloc(maxTmplSize);
    tmpl2 = malloc(maxTmplSize);

    if (!imgBuf || !tmpl1 || !tmpl2) {
        fprintf(stderr, "Memory allocation failed\n");
        goto cleanup;
    }

    printf("Enrolling '%s' finger '%s' (security: %s, threshold: %lu/199)\n\n",
           username, finger, SECURITY_NAMES[securityLevel], scoreThreshold);

recapture:
    /* Capture sample 1 — retry up to 3 times */
    ;
    int ok = 0;
    for (int i = 0; i < 3 && !ok; i++) {
        ok = (capture_and_extract(hFPM, imgBuf,
                                  devInfo.ImageWidth, devInfo.ImageHeight,
                                  tmpl1,
                                  "-> Place finger on scanner (sample 1)...") == 0);
    }
    if (!ok) { fprintf(stderr, "Failed to capture sample 1.\n"); goto cleanup; }

    printf("  Sample 1 captured. Remove finger and wait...\n\n");
    sleep(2);

    /* Capture sample 2 — retry up to 3 times */
    ok = 0;
    for (int i = 0; i < 3 && !ok; i++) {
        ok = (capture_and_extract(hFPM, imgBuf,
                                  devInfo.ImageWidth, devInfo.ImageHeight,
                                  tmpl2,
                                  "-> Place the same finger again (sample 2)...") == 0);
    }
    if (!ok) { fprintf(stderr, "Failed to capture sample 2.\n"); goto cleanup; }

    /* Confirm the two samples match at the chosen security level */
    printf("\nVerifying samples match...\n");
    BOOL matched = FALSE;
    err = SGFPM_MatchTemplate(hFPM, tmpl1, tmpl2, securityLevel, &matched);
    if (err != SGFDX_ERROR_NONE || !matched) {
        fprintf(stderr,
                "Samples did not match (err %lu, matched=%d).\n"
                "Please run sg_enroll again with the same finger.\n",
                err, matched);
        goto cleanup;
    }

    /* Confidence validation: check score against threshold */
    DWORD score = 0;
    SGFPM_GetMatchingScore(hFPM, tmpl1, tmpl2, &score);
    printf("  Match confirmed (score: %lu/199)\n", score);

    if (score < scoreThreshold) {
        printf("  Low confidence (score: %lu/199, threshold: %lu/199 [%s])\n",
               score, scoreThreshold, SECURITY_NAMES[securityLevel]);
        if (isatty(STDIN_FILENO)) {
            printf("  Re-capture for better quality? [Y/n] ");
            fflush(stdout);
            int c = getchar();
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF)
                ;
            if (c != 'n' && c != 'N') {
                /* Free verification buffer if allocated from previous round */
                free(verifyTmpl);
                verifyTmpl = NULL;
                goto recapture;
            }
        }
        printf("  Proceeding with current templates.\n");
    }
    printf("\n");

    /* Save template */
    if (mkdir(TEMPLATE_DIR, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "Cannot create %s: %s\n", TEMPLATE_DIR, strerror(errno));
        goto cleanup;
    }
    chmod(TEMPLATE_DIR, 0700);

    /* Get actual template size rather than writing maxTmplSize of padding */
    DWORD tmplSize = 0;
    err = SGFPM_GetTemplateSize(hFPM, tmpl1, &tmplSize);
    if (err != SGFDX_ERROR_NONE || tmplSize == 0)
        tmplSize = maxTmplSize;  /* fallback to max if query fails */

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        fprintf(stderr, "Cannot write %s: %s\n", path, strerror(errno));
        goto cleanup;
    }
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        fprintf(stderr, "fdopen failed: %s\n", strerror(errno));
        close(fd);
        goto cleanup;
    }
    if (fwrite(tmpl1, 1, tmplSize, f) != tmplSize) {
        fprintf(stderr, "Write failed for %s\n", path);
        fclose(f);
        goto cleanup;
    }
    fclose(f);

    printf("Fingerprint enrolled for '%s' finger '%s'\n  Template saved: %s (%lu bytes)\n",
           username, finger, path, tmplSize);

    /* Post-save verification */
    if (isatty(STDIN_FILENO)) {
        printf("\nVerification: place the same finger on the scanner...\n");

        verifyTmpl = malloc(maxTmplSize);
        if (verifyTmpl) {
            ok = 0;
            for (int i = 0; i < 3 && !ok; i++) {
                ok = (capture_and_extract(hFPM, imgBuf,
                                          devInfo.ImageWidth, devInfo.ImageHeight,
                                          verifyTmpl,
                                          "-> Scan finger for verification...") == 0);
            }

            if (ok) {
                /* Load the saved template from disk and match */
                FILE *vf = fopen(path, "rb");
                if (vf) {
                    BYTE *savedTmpl = malloc(tmplSize);
                    if (savedTmpl) {
                        if (fread(savedTmpl, 1, tmplSize, vf) == tmplSize) {
                            BOOL vmatch = FALSE;
                            err = SGFPM_MatchTemplate(hFPM, savedTmpl, verifyTmpl,
                                                      securityLevel, &vmatch);
                            if (err == SGFDX_ERROR_NONE && vmatch) {
                                DWORD vscore = 0;
                                SGFPM_GetMatchingScore(hFPM, savedTmpl, verifyTmpl,
                                                       &vscore);
                                printf("  Verification PASSED (score: %lu/199)\n", vscore);
                            } else {
                                printf("  WARNING: Verification FAILED\n");
                                printf("  Re-enroll? [Y/n] ");
                                fflush(stdout);
                                int c = getchar();
                                int ch;
                                while ((ch = getchar()) != '\n' && ch != EOF)
                                    ;
                                if (c != 'n' && c != 'N') {
                                    unlink(path);
                                    free(savedTmpl);
                                    fclose(vf);
                                    free(verifyTmpl);
                                    verifyTmpl = NULL;
                                    goto recapture;
                                }
                                printf("  Template kept despite failed verification.\n");
                            }
                        }
                        free(savedTmpl);
                    }
                    fclose(vf);
                }
            } else {
                printf("  Verification capture failed — template was saved.\n");
            }
        }
    }

    ret = 0;

cleanup:
    if (hFPM) {
        if (devOpened)
            SGFPM_CloseDevice(hFPM);
        SGFPM_Terminate(hFPM);
    }
    free(imgBuf);
    free(tmpl1);
    free(tmpl2);
    free(verifyTmpl);
    return ret;
}
