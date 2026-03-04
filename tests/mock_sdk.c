/*
 * mock_sdk.c — __wrap implementations for all SGFPM_* SDK functions
 *
 * Linked via -Wl,--wrap=SGFPM_Create etc. Each function reads from
 * the global MockState and writes output parameters accordingly.
 */

#include <string.h>
#include "mock_state.h"

MockState g_mock;

/* ── SDK lifecycle ──────────────────────────────────────────── */

DWORD __wrap_SGFPM_Create(HSGFPM *phFPM)
{
    g_mock.create_count++;
    if (phFPM && g_mock.create_rv == SGFDX_ERROR_NONE)
        *phFPM = g_mock.create_handle;
    return g_mock.create_rv;
}

DWORD __wrap_SGFPM_Terminate(HSGFPM hFPM)
{
    (void)hFPM;
    g_mock.terminate_count++;
    return g_mock.terminate_rv;
}

DWORD __wrap_SGFPM_Init(HSGFPM hFPM, DWORD devName)
{
    (void)hFPM; (void)devName;
    return g_mock.init_rv;
}

DWORD __wrap_SGFPM_SetTemplateFormat(HSGFPM hFPM, WORD format)
{
    (void)hFPM; (void)format;
    return g_mock.set_template_format_rv;
}

/* ── Device operations ──────────────────────────────────────── */

DWORD __wrap_SGFPM_OpenDevice(HSGFPM hFPM, DWORD devId)
{
    (void)hFPM; (void)devId;
    g_mock.open_device_count++;
    return g_mock.open_device_rv;
}

DWORD __wrap_SGFPM_CloseDevice(HSGFPM hFPM)
{
    (void)hFPM;
    g_mock.close_device_count++;
    return g_mock.close_device_rv;
}

DWORD __wrap_SGFPM_GetDeviceInfo(HSGFPM hFPM, SGDeviceInfoParam *pInfo)
{
    (void)hFPM;
    if (pInfo && g_mock.get_device_info_rv == SGFDX_ERROR_NONE) {
        memset(pInfo, 0, sizeof(*pInfo));
        pInfo->ImageWidth  = g_mock.devinfo_width;
        pInfo->ImageHeight = g_mock.devinfo_height;
        pInfo->ImageDPI    = g_mock.devinfo_dpi;
    }
    return g_mock.get_device_info_rv;
}

/* ── Image capture ──────────────────────────────────────────── */

DWORD __wrap_SGFPM_GetImageEx(HSGFPM hFPM, BYTE *buffer,
                               DWORD timeout, HWND dispWnd, DWORD quality)
{
    (void)hFPM; (void)buffer; (void)timeout;
    (void)dispWnd; (void)quality;
    g_mock.get_image_ex_count++;
    return g_mock.get_image_ex_rv;
}

DWORD __wrap_SGFPM_GetImageQuality(HSGFPM hFPM, DWORD width, DWORD height,
                                    BYTE *imgBuf, DWORD *quality)
{
    (void)hFPM; (void)width; (void)height; (void)imgBuf;
    if (quality)
        *quality = g_mock.image_quality;
    return g_mock.get_image_quality_rv;
}

/* ── Template operations ────────────────────────────────────── */

DWORD __wrap_SGFPM_GetMaxTemplateSize(HSGFPM hFPM, DWORD *size)
{
    (void)hFPM;
    if (size)
        *size = g_mock.max_template_size;
    return g_mock.get_max_template_size_rv;
}

DWORD __wrap_SGFPM_CreateTemplate(HSGFPM hFPM, SGFingerInfo *fpInfo,
                                   BYTE *rawImage, BYTE *minTemplate)
{
    (void)hFPM; (void)fpInfo; (void)rawImage; (void)minTemplate;
    g_mock.create_template_count++;
    return g_mock.create_template_rv;
}

DWORD __wrap_SGFPM_GetTemplateSize(HSGFPM hFPM, BYTE *minTemplate, DWORD *size)
{
    (void)hFPM; (void)minTemplate;
    if (size)
        *size = g_mock.template_size;
    return g_mock.get_template_size_rv;
}

/* ── Matching ───────────────────────────────────────────────── */

DWORD __wrap_SGFPM_MatchTemplate(HSGFPM hFPM, BYTE *minTemplate1,
                                  BYTE *minTemplate2, DWORD secuLevel,
                                  BOOL *matched)
{
    (void)hFPM; (void)minTemplate1; (void)minTemplate2; (void)secuLevel;
    int idx = g_mock.match_template_count;
    g_mock.match_template_count++;
    if (matched) {
        if (g_mock.match_results && idx < g_mock.match_results_len)
            *matched = g_mock.match_results[idx];
        else
            *matched = g_mock.match_result;
    }
    return g_mock.match_template_rv;
}

DWORD __wrap_SGFPM_GetMatchingScore(HSGFPM hFPM, BYTE *minTemplate1,
                                     BYTE *minTemplate2, DWORD *score)
{
    (void)hFPM; (void)minTemplate1; (void)minTemplate2;
    if (score)
        *score = g_mock.matching_score;
    return g_mock.get_matching_score_rv;
}
