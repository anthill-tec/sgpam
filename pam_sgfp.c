/*
 * pam_sgfp.c — PAM authentication module for SecuGen U20 fingerprint reader
 *
 * Captures a live fingerprint and matches it against an enrolled template
 * stored in /etc/security/sg_fingerprints/<username>.tpl
 *
 * Build:  see Makefile
 * Install: sudo cp pam_sgfp.so /usr/lib/security/
 */

#define PAM_SM_AUTH
#include <security/pam_modules.h>
#include <security/pam_ext.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#include "sgfplib.h"

/* ── tunables ─────────────────────────────────────────────── */
#define TEMPLATE_DIR     "/etc/security/sg_fingerprints"
#define DEVICE_NAME      SG_DEV_FDU05      /* U20 = fdu05 driver  */
#define CAPTURE_TIMEOUT  10000             /* ms – 10 s           */
#define CAPTURE_QUALITY  40               /* verification floor  */
#define SECURITY_LEVEL   SL_ABOVE_NORMAL  /* score ≥ 90          */
#define TEMPLATE_FORMAT  TEMPLATE_FORMAT_SG400  /* 400 B, encrypted */

/* ── helpers ──────────────────────────────────────────────── */

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

static int load_template(const char *username, BYTE **tmpl, DWORD *size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.tpl", TEMPLATE_DIR, username);

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    fseek(f, 0, SEEK_END);
    *size = (DWORD)ftell(f);
    rewind(f);

    *tmpl = malloc(*size);
    if (!*tmpl) { fclose(f); return -1; }

    if (fread(*tmpl, 1, *size, f) != *size) {
        free(*tmpl);
        *tmpl = NULL;
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* ── PAM entry points ─────────────────────────────────────── */

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags,
                                   int argc, const char **argv)
{
    (void)flags; (void)argc; (void)argv;

    const char *username   = NULL;
    HSGFPM      hFPM       = NULL;
    int         devOpened  = 0;
    BYTE       *imgBuf     = NULL;
    BYTE       *liveTmpl   = NULL;
    BYTE       *storedTmpl = NULL;
    DWORD       storedSize = 0;
    DWORD       maxTmplSize = 0;
    DWORD       err;
    BOOL        matched    = FALSE;
    int         result     = PAM_AUTH_ERR;
    SGDeviceInfoParam devInfo;

    /* 1. Resolve PAM username */
    if (pam_get_user(pamh, &username, NULL) != PAM_SUCCESS || !username) {
        syslog(LOG_AUTH | LOG_ERR, "pam_sgfp: could not get username");
        return PAM_AUTH_ERR;
    }

    /* 2. Validate username — reject path traversal attempts */
    if (!valid_username(username)) {
        syslog(LOG_AUTH | LOG_WARNING,
               "pam_sgfp: invalid username rejected (path traversal?)");
        return PAM_AUTH_ERR;
    }

    /* 3. Load enrolled template — fail silently so non-enrolled users
       fall through to password auth via the next PAM rule               */
    if (load_template(username, &storedTmpl, &storedSize) != 0) {
        syslog(LOG_AUTH | LOG_NOTICE,
               "pam_sgfp: no enrolled fingerprint for user '%s'", username);
        return PAM_USER_UNKNOWN;
    }

    /* 4. Initialise SDK */
    err = SGFPM_Create(&hFPM);
    if (err != SGFDX_ERROR_NONE) {
        syslog(LOG_AUTH | LOG_ERR, "pam_sgfp: SGFPM_Create failed (%lu)", err);
        goto cleanup;
    }

    err = SGFPM_Init(hFPM, DEVICE_NAME);
    if (err != SGFDX_ERROR_NONE) {
        syslog(LOG_AUTH | LOG_ERR, "pam_sgfp: SGFPM_Init failed (%lu)", err);
        goto cleanup;
    }

    SGFPM_SetTemplateFormat(hFPM, TEMPLATE_FORMAT);

    /* 5. Open scanner */
    err = SGFPM_OpenDevice(hFPM, USB_AUTO_DETECT);
    if (err != SGFDX_ERROR_NONE) {
        syslog(LOG_AUTH | LOG_ERR,
               "pam_sgfp: SGFPM_OpenDevice failed (%lu) — scanner connected?", err);
        goto cleanup;
    }
    devOpened = 1;

    /* 6. Query image dimensions */
    memset(&devInfo, 0, sizeof(devInfo));
    err = SGFPM_GetDeviceInfo(hFPM, &devInfo);
    if (err != SGFDX_ERROR_NONE) {
        syslog(LOG_AUTH | LOG_ERR, "pam_sgfp: GetDeviceInfo failed (%lu)", err);
        goto cleanup;
    }

    /* Guard against overflow: U20 is 260x300 = 78000; reject anything absurd */
    if (devInfo.ImageWidth == 0 || devInfo.ImageHeight == 0 ||
        devInfo.ImageWidth > 4096 || devInfo.ImageHeight > 4096) {
        syslog(LOG_AUTH | LOG_ERR,
               "pam_sgfp: suspicious image dimensions %lux%lu",
               devInfo.ImageWidth, devInfo.ImageHeight);
        goto cleanup;
    }

    imgBuf = malloc(devInfo.ImageWidth * devInfo.ImageHeight);
    if (!imgBuf) goto cleanup;

    err = SGFPM_GetMaxTemplateSize(hFPM, &maxTmplSize);
    if (err != SGFDX_ERROR_NONE) goto cleanup;

    liveTmpl = malloc(maxTmplSize);
    if (!liveTmpl) goto cleanup;

    /* 7. Capture fingerprint */
    pam_info(pamh, "Place finger on scanner...");
    err = SGFPM_GetImageEx(hFPM, imgBuf, CAPTURE_TIMEOUT, NULL, CAPTURE_QUALITY);
    if (err != SGFDX_ERROR_NONE) {
        syslog(LOG_AUTH | LOG_NOTICE,
               "pam_sgfp: capture failed for '%s' (%lu)", username, err);
        goto cleanup;
    }

    /* 8. Extract minutiae */
    err = SGFPM_CreateTemplate(hFPM, NULL, imgBuf, liveTmpl);
    if (err != SGFDX_ERROR_NONE) {
        syslog(LOG_AUTH | LOG_NOTICE,
               "pam_sgfp: template extraction failed (%lu)", err);
        goto cleanup;
    }

    /* 9. Match against stored template */
    err = SGFPM_MatchTemplate(hFPM, storedTmpl, liveTmpl, SECURITY_LEVEL, &matched);
    if (err == SGFDX_ERROR_NONE && matched) {
        syslog(LOG_AUTH | LOG_INFO,
               "pam_sgfp: fingerprint accepted for user '%s'", username);
        result = PAM_SUCCESS;
    } else {
        syslog(LOG_AUTH | LOG_NOTICE,
               "pam_sgfp: fingerprint rejected for user '%s' (err=%lu matched=%d)",
               username, err, matched);
    }

cleanup:
    if (hFPM) {
        if (devOpened)
            SGFPM_CloseDevice(hFPM);
        SGFPM_Terminate(hFPM);
    }
    free(imgBuf);
    free(liveTmpl);
    free(storedTmpl);
    return result;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags,
                               int argc, const char **argv)
{
    (void)pamh; (void)flags; (void)argc; (void)argv;
    return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags,
                                  int argc, const char **argv)
{
    (void)pamh; (void)flags; (void)argc; (void)argv;
    return PAM_SUCCESS;
}
