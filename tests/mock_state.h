/*
 * mock_state.h — Global mock control struct for --wrap mocking
 *
 * Every __wrap_SGFPM_* and __wrap_pam_* function reads from this struct
 * to decide return values and output parameters. Reset before each test.
 */

#ifndef MOCK_STATE_H
#define MOCK_STATE_H

#include <stddef.h>
#include <string.h>
#include "sgfplib.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef struct {
    /* ── SDK return values ─────────────────────────────────────── */
    DWORD create_rv;
    DWORD init_rv;
    DWORD set_template_format_rv;
    DWORD open_device_rv;
    DWORD close_device_rv;
    DWORD get_device_info_rv;
    DWORD get_max_template_size_rv;
    DWORD get_image_ex_rv;
    DWORD get_image_quality_rv;
    DWORD create_template_rv;
    DWORD match_template_rv;
    DWORD get_matching_score_rv;
    DWORD get_template_size_rv;
    DWORD terminate_rv;

    /* ── SDK output parameters ────────────────────────────────── */
    HSGFPM  create_handle;         /* written to *phFPM by Create    */
    DWORD   devinfo_width;         /* DeviceInfoParam.ImageWidth     */
    DWORD   devinfo_height;        /* DeviceInfoParam.ImageHeight    */
    DWORD   devinfo_dpi;           /* DeviceInfoParam.ImageDPI       */
    DWORD   max_template_size;     /* written by GetMaxTemplateSize  */
    DWORD   image_quality;         /* written by GetImageQuality     */
    BOOL    match_result;          /* written by MatchTemplate       */
    DWORD   matching_score;        /* written by GetMatchingScore    */
    DWORD   template_size;         /* written by GetTemplateSize     */

    /* ── SDK call counters ────────────────────────────────────── */
    int create_count;
    int terminate_count;
    int open_device_count;
    int close_device_count;
    int match_template_count;
    int get_image_ex_count;
    int create_template_count;

    /* ── PAM mock state ───────────────────────────────────────── */
    int          pam_get_user_rv;  /* PAM_SUCCESS, PAM_AUTH_ERR, etc */
    const char  *pam_username;     /* returned via second param      */

} MockState;

extern MockState g_mock;

static inline void mock_state_reset(void)
{
    /* Zero everything first */
    memset(&g_mock, 0, sizeof(g_mock));

    /* Sensible defaults: all SDK calls succeed */
    g_mock.create_rv               = SGFDX_ERROR_NONE;
    g_mock.init_rv                 = SGFDX_ERROR_NONE;
    g_mock.set_template_format_rv  = SGFDX_ERROR_NONE;
    g_mock.open_device_rv          = SGFDX_ERROR_NONE;
    g_mock.close_device_rv         = SGFDX_ERROR_NONE;
    g_mock.get_device_info_rv      = SGFDX_ERROR_NONE;
    g_mock.get_max_template_size_rv = SGFDX_ERROR_NONE;
    g_mock.get_image_ex_rv         = SGFDX_ERROR_NONE;
    g_mock.get_image_quality_rv    = SGFDX_ERROR_NONE;
    g_mock.create_template_rv      = SGFDX_ERROR_NONE;
    g_mock.match_template_rv       = SGFDX_ERROR_NONE;
    g_mock.get_matching_score_rv   = SGFDX_ERROR_NONE;
    g_mock.get_template_size_rv    = SGFDX_ERROR_NONE;
    g_mock.terminate_rv            = SGFDX_ERROR_NONE;

    /* Typical U20 dimensions */
    g_mock.create_handle     = (HSGFPM)0xDEAD;
    g_mock.devinfo_width     = 260;
    g_mock.devinfo_height    = 300;
    g_mock.devinfo_dpi       = 500;
    g_mock.max_template_size = 400;
    g_mock.image_quality     = 80;
    g_mock.match_result      = TRUE;
    g_mock.matching_score    = 150;
    g_mock.template_size     = 400;

    /* PAM defaults */
    g_mock.pam_get_user_rv = 0;  /* PAM_SUCCESS */
    g_mock.pam_username    = "testuser";
}

#endif /* MOCK_STATE_H */
