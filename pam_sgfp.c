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
#include <glob.h>

#include "sgfplib.h"

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
#define DEVICE_NAME      SG_DEV_FDU05      /* U20 = fdu05 driver  */
#define CAPTURE_TIMEOUT  10000             /* ms – 10 s           */
#define CAPTURE_QUALITY  40               /* verification floor  */
#define SECURITY_LEVEL   SL_NORMAL        /* score ≥ 80 (SDK recommended) */
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

/* Kept for backward compatibility with test_load_template.c */
static int __attribute__((unused))
load_template(const char *username, BYTE **tmpl, DWORD *size)
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

/*
 * Load all templates for a user: finger-specific (<user>_*.tpl) + legacy (<user>.tpl)
 * Returns 0 on success (at least one template found), -1 on failure.
 * Caller must free each tmpls[i], then tmpls and sizes arrays.
 *
 * If prompt_out is non-NULL, builds a PAM prompt string listing enrolled
 * finger names (e.g. "Place finger on scanner (right-index, left-thumb)...").
 * Caller must free *prompt_out.
 */
static int load_templates(const char *username,
                          BYTE ***tmpls, DWORD **sizes, int *count,
                          char **prompt_out)
{
    *tmpls = NULL;
    *sizes = NULL;
    *count = 0;
    if (prompt_out) *prompt_out = NULL;

    /* Glob for finger-specific templates: <user>_*.tpl */
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s/%s_*.tpl", TEMPLATE_DIR, username);

    glob_t globbuf;
    int grc = glob(pattern, 0, NULL, &globbuf);

    /* Also check for legacy template: <user>.tpl */
    char legacy_path[512];
    snprintf(legacy_path, sizeof(legacy_path), "%s/%s.tpl", TEMPLATE_DIR, username);
    int has_legacy = (access(legacy_path, R_OK) == 0);

    int total = (grc == 0 ? (int)globbuf.gl_pathc : 0) + (has_legacy ? 1 : 0);
    if (total == 0) {
        if (grc == 0) globfree(&globbuf);
        return -1;
    }

    *tmpls = calloc(total, sizeof(BYTE *));
    *sizes = calloc(total, sizeof(DWORD));
    if (!*tmpls || !*sizes) {
        free(*tmpls); free(*sizes);
        *tmpls = NULL; *sizes = NULL;
        if (grc == 0) globfree(&globbuf);
        return -1;
    }

    /* Build finger names list for prompt */
    char names_buf[512];
    int names_pos = 0;
    names_buf[0] = '\0';
    size_t ulen = strlen(username);

    int loaded = 0;

    /* Load finger-specific templates */
    if (grc == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
            FILE *f = fopen(globbuf.gl_pathv[i], "rb");
            if (!f) continue;

            fseek(f, 0, SEEK_END);
            DWORD sz = (DWORD)ftell(f);
            rewind(f);

            if (sz == 0) { fclose(f); continue; }

            BYTE *buf = malloc(sz);
            if (!buf) { fclose(f); continue; }

            if (fread(buf, 1, sz, f) != sz) {
                free(buf); fclose(f); continue;
            }
            fclose(f);

            (*tmpls)[loaded] = buf;
            (*sizes)[loaded] = sz;
            loaded++;

            /* Extract finger name: <dir>/<user>_<finger>.tpl */
            if (prompt_out) {
                const char *base = strrchr(globbuf.gl_pathv[i], '/');
                base = base ? base + 1 : globbuf.gl_pathv[i];
                const char *finger = base + ulen + 1;  /* skip "<user>_" */
                size_t flen = strlen(finger);
                if (flen > 4) {  /* strip ".tpl" */
                    int n = snprintf(names_buf + names_pos,
                                     sizeof(names_buf) - names_pos,
                                     "%s%.*s",
                                     names_pos > 0 ? ", " : "",
                                     (int)(flen - 4), finger);
                    if (n > 0) names_pos += n;
                }
            }
        }
        globfree(&globbuf);
    }

    /* Load legacy template */
    if (has_legacy) {
        FILE *f = fopen(legacy_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            DWORD sz = (DWORD)ftell(f);
            rewind(f);

            if (sz > 0) {
                BYTE *buf = malloc(sz);
                if (buf && fread(buf, 1, sz, f) == sz) {
                    (*tmpls)[loaded] = buf;
                    (*sizes)[loaded] = sz;
                    loaded++;
                } else {
                    free(buf);
                }
            }
            fclose(f);
        }
    }

    if (loaded == 0) {
        free(*tmpls); free(*sizes);
        *tmpls = NULL; *sizes = NULL;
        *count = 0;
        return -1;
    }

    /* Build the prompt string */
    if (prompt_out) {
        char prompt[600];
        if (names_pos > 0)
            snprintf(prompt, sizeof(prompt),
                     "Place finger on scanner (%s)...", names_buf);
        else
            snprintf(prompt, sizeof(prompt),
                     "Place finger on scanner...");
        *prompt_out = strdup(prompt);
    }

    *count = loaded;
    return 0;
}

/* ── PAM entry points ─────────────────────────────────────── */

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags,
                                   int argc, const char **argv)
{
    (void)flags; (void)argc; (void)argv;

    const char *username    = NULL;
    HSGFPM      hFPM        = NULL;
    int         devOpened   = 0;
    BYTE       *imgBuf      = NULL;
    BYTE       *liveTmpl    = NULL;
    BYTE      **storedTmpls = NULL;
    DWORD      *storedSizes = NULL;
    int         tmplCount   = 0;
    char       *scanPrompt  = NULL;
    DWORD       maxTmplSize = 0;
    DWORD       err;
    BOOL        matched     = FALSE;
    int         result      = PAM_AUTH_ERR;
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

    /* 3. Load enrolled templates — fail silently so non-enrolled users
       fall through to password auth via the next PAM rule               */
    if (load_templates(username, &storedTmpls, &storedSizes, &tmplCount,
                       &scanPrompt) != 0) {
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
    pam_info(pamh, "%s", scanPrompt ? scanPrompt : "Place finger on scanner...");
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

    /* 9. Match against stored templates — first match wins */
    for (int i = 0; i < tmplCount; i++) {
        matched = FALSE;
        err = SGFPM_MatchTemplate(hFPM, storedTmpls[i], liveTmpl,
                                  SECURITY_LEVEL, &matched);
        if (err == SGFDX_ERROR_NONE && matched) {
            syslog(LOG_AUTH | LOG_INFO,
                   "pam_sgfp: fingerprint accepted for user '%s' "
                   "(template %d/%d)", username, i + 1, tmplCount);
            result = PAM_SUCCESS;
            break;
        }
    }
    if (result != PAM_SUCCESS) {
        syslog(LOG_AUTH | LOG_NOTICE,
               "pam_sgfp: fingerprint rejected for user '%s' "
               "(%d templates tried)", username, tmplCount);
    }

cleanup:
    if (hFPM) {
        if (devOpened)
            SGFPM_CloseDevice(hFPM);
        SGFPM_Terminate(hFPM);
    }
    free(imgBuf);
    free(liveTmpl);
    for (int i = 0; i < tmplCount; i++)
        free(storedTmpls[i]);
    free(storedTmpls);
    free(storedSizes);
    free(scanPrompt);
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
