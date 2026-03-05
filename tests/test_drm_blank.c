/*
 * test_drm_blank.c — Unit tests for sg-drm-blank DRM screen blanking tool
 *
 * sg-drm-blank's main() is renamed to sg_drm_blank_main via -Dmain=sg_drm_blank_main
 * to avoid collision with Criterion's main. All DRM and system calls are mocked
 * via --wrap to test without hardware.
 *
 * 3 suites, 17 tests:
 *   create_black_fb (5) — framebuffer creation + partial failure cleanup
 *   destroy_fb      (5) — selective cleanup based on populated fields
 *   blank_device    (7) — end-to-end device blanking flow
 */

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <criterion/criterion.h>

#include "mock_drm_state.h"

/* Pull in sg-drm-blank.c — main renamed via -Dmain=sg_drm_blank_main */
#include "../sg-drm-blank.c"

/* ══════════════════════════════════════════════════════════════════════════
 *  Suite: create_black_fb
 * ══════════════════════════════════════════════════════════════════════════ */

Test(create_black_fb, success, .init = mock_drm_reset)
{
    BlackFB fb = {0};
    int ret = create_black_fb(MOCK_DRM_FAKE_FD, 1920, 1080, &fb);

    cr_assert_eq(ret, 0, "expected success, got %d", ret);
    cr_assert_eq(fb.handle, g_drm.create_dumb_handle);
    cr_assert_eq(fb.stride, g_drm.create_dumb_pitch);
    cr_assert_eq(fb.size, g_drm.create_dumb_size);
    cr_assert_eq(fb.fb_id, g_drm.add_fb_id);
    cr_assert_not_null(fb.map, "mmap should have returned a buffer");

    /* Clean up */
    destroy_fb(MOCK_DRM_FAKE_FD, &fb);
}

Test(create_black_fb, create_dumb_fails, .init = mock_drm_reset)
{
    g_drm.create_dumb_ret = -1;

    BlackFB fb = {0};
    int ret = create_black_fb(MOCK_DRM_FAKE_FD, 1920, 1080, &fb);

    cr_assert_eq(ret, -1, "should fail when CREATE_DUMB fails");
    /* No resources should have been allocated */
    cr_assert_eq(fb.handle, 0);
    cr_assert_eq(fb.fb_id, 0);
    cr_assert_null(fb.map);
}

Test(create_black_fb, add_fb_fails, .init = mock_drm_reset)
{
    g_drm.add_fb_ret = -1;

    BlackFB fb = {0};
    int ret = create_black_fb(MOCK_DRM_FAKE_FD, 1920, 1080, &fb);

    cr_assert_eq(ret, -1, "should fail when AddFB fails");
    /* handle was allocated but FB was not — verify handle is set for cleanup */
    cr_assert_neq(fb.handle, 0, "handle should be set for cleanup");
    cr_assert_eq(fb.fb_id, 0, "fb_id should not be set");
}

Test(create_black_fb, map_dumb_fails, .init = mock_drm_reset)
{
    g_drm.map_dumb_ret = -1;

    BlackFB fb = {0};
    int ret = create_black_fb(MOCK_DRM_FAKE_FD, 1920, 1080, &fb);

    cr_assert_eq(ret, -1, "should fail when MAP_DUMB fails");
    /* handle + FB were allocated */
    cr_assert_neq(fb.handle, 0);
    cr_assert_neq(fb.fb_id, 0);
    cr_assert_null(fb.map, "map should not be set");
}

Test(create_black_fb, mmap_fails, .init = mock_drm_reset)
{
    g_drm.mmap_fail = true;

    BlackFB fb = {0};
    int ret = create_black_fb(MOCK_DRM_FAKE_FD, 1920, 1080, &fb);

    cr_assert_eq(ret, -1, "should fail when mmap fails");
    /* handle + FB were allocated, map should be NULL (MAP_FAILED is reset) */
    cr_assert_neq(fb.handle, 0);
    cr_assert_neq(fb.fb_id, 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Suite: destroy_fb
 * ══════════════════════════════════════════════════════════════════════════ */

Test(destroy_fb, full_cleanup, .init = mock_drm_reset)
{
    /* Simulate a fully populated BlackFB */
    void *buf = calloc(1, 4096);
    g_drm.mmap_ptr = buf;  /* so munmap intercept recognizes it */

    BlackFB fb = {
        .fb_id  = 100,
        .handle = 1,
        .stride = 7680,
        .size   = 4096,
        .map    = buf,
    };

    destroy_fb(MOCK_DRM_FAKE_FD, &fb);

    cr_assert_eq(g_drm.munmap_count, 1, "munmap should be called");
    cr_assert_eq(g_drm.rm_fb_count, 1, "RmFB should be called");
    cr_assert_eq(g_drm.destroy_dumb_count, 1, "DESTROY_DUMB should be called");
}

Test(destroy_fb, null_map, .init = mock_drm_reset)
{
    BlackFB fb = {
        .fb_id  = 100,
        .handle = 1,
        .stride = 7680,
        .size   = 4096,
        .map    = NULL,
    };

    destroy_fb(MOCK_DRM_FAKE_FD, &fb);

    cr_assert_eq(g_drm.munmap_count, 0, "munmap should NOT be called");
    cr_assert_eq(g_drm.rm_fb_count, 1, "RmFB should be called");
    cr_assert_eq(g_drm.destroy_dumb_count, 1, "DESTROY_DUMB should be called");
}

Test(destroy_fb, zero_fb_id, .init = mock_drm_reset)
{
    void *buf = calloc(1, 4096);
    g_drm.mmap_ptr = buf;

    BlackFB fb = {
        .fb_id  = 0,
        .handle = 1,
        .stride = 7680,
        .size   = 4096,
        .map    = buf,
    };

    destroy_fb(MOCK_DRM_FAKE_FD, &fb);

    cr_assert_eq(g_drm.munmap_count, 1, "munmap should be called");
    cr_assert_eq(g_drm.rm_fb_count, 0, "RmFB should NOT be called");
    cr_assert_eq(g_drm.destroy_dumb_count, 1, "DESTROY_DUMB should be called");
}

Test(destroy_fb, zero_handle, .init = mock_drm_reset)
{
    void *buf = calloc(1, 4096);
    g_drm.mmap_ptr = buf;

    BlackFB fb = {
        .fb_id  = 100,
        .handle = 0,
        .stride = 7680,
        .size   = 4096,
        .map    = buf,
    };

    destroy_fb(MOCK_DRM_FAKE_FD, &fb);

    cr_assert_eq(g_drm.munmap_count, 1, "munmap should be called");
    cr_assert_eq(g_drm.rm_fb_count, 1, "RmFB should be called");
    cr_assert_eq(g_drm.destroy_dumb_count, 0, "DESTROY_DUMB should NOT be called");
}

Test(destroy_fb, all_zero, .init = mock_drm_reset)
{
    BlackFB fb = {0};

    destroy_fb(MOCK_DRM_FAKE_FD, &fb);

    cr_assert_eq(g_drm.munmap_count, 0, "munmap should NOT be called");
    cr_assert_eq(g_drm.rm_fb_count, 0, "RmFB should NOT be called");
    cr_assert_eq(g_drm.destroy_dumb_count, 0, "DESTROY_DUMB should NOT be called");
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Suite: blank_device
 * ══════════════════════════════════════════════════════════════════════════ */

Test(blank_device, one_active_crtc, .init = mock_drm_reset)
{
    int ret = blank_device("/dev/dri/card0");

    cr_assert_eq(ret, 1, "should blank 1 CRTC, got %d", ret);
    cr_assert_eq(g_drm.set_crtc_count, 1);
    cr_assert_geq(g_drm.usleep_count, 1, "should have called usleep");
}

Test(blank_device, open_fails, .init = mock_drm_reset)
{
    g_drm.open_ret = -1;

    int ret = blank_device("/dev/dri/card0");

    cr_assert_eq(ret, -1, "should return -1 when open fails");
    cr_assert_eq(g_drm.set_crtc_count, 0);
}

Test(blank_device, set_master_fails, .init = mock_drm_reset)
{
    g_drm.set_master_ret = -1;

    int ret = blank_device("/dev/dri/card0");

    cr_assert_eq(ret, 0, "should return 0 (not error) when SetMaster fails");
    cr_assert_eq(g_drm.set_crtc_count, 0, "should not attempt blanking");
}

Test(blank_device, get_resources_null, .init = mock_drm_reset)
{
    g_drm.get_resources_fail = true;

    int ret = blank_device("/dev/dri/card0");

    cr_assert_eq(ret, -1, "should return -1 when GetResources returns NULL");
}

Test(blank_device, no_active_crtcs, .init = mock_drm_reset)
{
    g_drm.crtc_mode_valid[0] = false;

    int ret = blank_device("/dev/dri/card0");

    cr_assert_eq(ret, 0, "should return 0 with no active CRTCs");
    cr_assert_eq(g_drm.set_crtc_count, 0);
}

Test(blank_device, multiple_crtcs_mixed, .init = mock_drm_reset)
{
    g_drm.num_crtcs = 3;
    g_drm.crtc_ids[0]  = 50;  g_drm.crtc_mode_valid[0] = true;
    g_drm.crtc_width[0] = 1920; g_drm.crtc_height[0] = 1080;
    g_drm.crtc_ids[1]  = 51;  g_drm.crtc_mode_valid[1] = false;
    g_drm.crtc_ids[2]  = 52;  g_drm.crtc_mode_valid[2] = true;
    g_drm.crtc_width[2] = 2560; g_drm.crtc_height[2] = 1440;

    int ret = blank_device("/dev/dri/card0");

    cr_assert_eq(ret, 2, "should blank 2 active CRTCs, got %d", ret);
    cr_assert_eq(g_drm.set_crtc_count, 2);
}

Test(blank_device, set_crtc_fails, .init = mock_drm_reset)
{
    g_drm.set_crtc_ret = -1;

    int ret = blank_device("/dev/dri/card0");

    cr_assert_eq(ret, 0, "should return 0 (not counted) when SetCrtc fails");
    cr_assert_eq(g_drm.set_crtc_count, 1, "SetCrtc should still be called");
}
