/*
 * mock_drm_state.h — Global mock control struct for DRM --wrap mocking
 *
 * Every __wrap_drmMode* and __wrap_open/mmap/etc function reads from this
 * struct to decide return values and output parameters. Reset before each test.
 */

#ifndef MOCK_DRM_STATE_H
#define MOCK_DRM_STATE_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MOCK_DRM_MAX_CRTCS 8
#define MOCK_DRM_FAKE_FD   9999

typedef struct {
    /* ── open/close ─────────────────────────────────────────── */
    int open_ret;              /* fd to return (-1 = fail) */

    /* ── drmIoctl (CREATE_DUMB) ─────────────────────────────── */
    int      create_dumb_ret;
    uint32_t create_dumb_handle;
    uint32_t create_dumb_pitch;
    uint64_t create_dumb_size;

    /* ── drmIoctl (MAP_DUMB) ────────────────────────────────── */
    int map_dumb_ret;

    /* ── mmap ───────────────────────────────────────────────── */
    bool  mmap_fail;
    void *mmap_ptr;            /* heap-allocated buffer */

    /* ── drmModeAddFB ───────────────────────────────────────── */
    int      add_fb_ret;
    uint32_t add_fb_id;

    /* ── drmSetMaster ───────────────────────────────────────── */
    int set_master_ret;

    /* ── drmModeGetResources ────────────────────────────────── */
    bool     get_resources_fail;
    int      num_crtcs;
    uint32_t crtc_ids[MOCK_DRM_MAX_CRTCS];

    /* ── drmModeGetCrtc (per crtc_id) ───────────────────────── */
    bool     crtc_mode_valid[MOCK_DRM_MAX_CRTCS];
    uint32_t crtc_width[MOCK_DRM_MAX_CRTCS];
    uint32_t crtc_height[MOCK_DRM_MAX_CRTCS];

    /* ── drmModeSetCrtc ─────────────────────────────────────── */
    int set_crtc_ret;

    /* ── Counters ───────────────────────────────────────────── */
    int set_crtc_count;
    int usleep_count;
    int add_fb_count;
    int rm_fb_count;
    int munmap_count;
    int destroy_dumb_count;

} MockDrmState;

extern MockDrmState g_drm;

static inline void mock_drm_reset(void)
{
    /* Free any prior mmap buffer */
    if (g_drm.mmap_ptr) {
        free(g_drm.mmap_ptr);
    }

    /* Zero everything */
    memset(&g_drm, 0, sizeof(g_drm));

    /* Sensible defaults: one active 1920x1080 CRTC, all ops succeed */
    g_drm.open_ret          = MOCK_DRM_FAKE_FD;
    g_drm.create_dumb_ret   = 0;
    g_drm.create_dumb_handle = 1;
    g_drm.create_dumb_pitch  = 1920 * 4;
    g_drm.create_dumb_size   = 1920 * 4 * 1080;
    g_drm.map_dumb_ret      = 0;
    g_drm.mmap_fail         = false;
    g_drm.add_fb_ret        = 0;
    g_drm.add_fb_id         = 100;
    g_drm.set_master_ret    = 0;
    g_drm.get_resources_fail = false;
    g_drm.set_crtc_ret      = 0;

    g_drm.num_crtcs    = 1;
    g_drm.crtc_ids[0]  = 50;
    g_drm.crtc_mode_valid[0] = true;
    g_drm.crtc_width[0]     = 1920;
    g_drm.crtc_height[0]    = 1080;
}

#endif /* MOCK_DRM_STATE_H */
