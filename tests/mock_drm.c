/*
 * mock_drm.c — __wrap implementations for DRM and system call mocking
 *
 * Linked via -Wl,--wrap=open,--wrap=drmIoctl etc. System calls pass through
 * by default (protecting Criterion internals), intercepting only DRM paths/fds.
 * DRM library functions are always active (Criterion never calls them).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "mock_drm_state.h"

MockDrmState g_drm;

/* ── Real function declarations for pass-through ─────────────────────────── */

extern int    __real_open(const char *path, int flags, ...);
extern int    __real_close(int fd);
extern void  *__real_mmap(void *addr, size_t len, int prot, int flags,
                          int fd, off_t offset);
extern int    __real_munmap(void *addr, size_t len);

/* ── System call wraps ───────────────────────────────────────────────────── */

int __wrap_open(const char *path, int flags, ...)
{
    /* Intercept /dev/dri/* paths */
    if (strncmp(path, "/dev/dri/", 9) == 0) {
        return g_drm.open_ret;
    }
    /* Pass through for everything else (Criterion, libc, etc.) */
    return __real_open(path, flags);
}

int __wrap_close(int fd)
{
    /* No-op for our fake fd */
    if (fd == g_drm.open_ret && fd != -1) {
        return 0;
    }
    return __real_close(fd);
}

void *__wrap_mmap(void *addr, size_t len, int prot, int flags,
                  int fd, off_t offset)
{
    /* Intercept only for our fake fd */
    if (fd == g_drm.open_ret && fd != -1) {
        if (g_drm.mmap_fail) {
            return MAP_FAILED;
        }
        /* Allocate a heap buffer sized to match the dumb buffer */
        size_t sz = (len > 0) ? len : (size_t)g_drm.create_dumb_size;
        g_drm.mmap_ptr = calloc(1, sz);
        return g_drm.mmap_ptr;
    }
    return __real_mmap(addr, len, prot, flags, fd, offset);
}

int __wrap_munmap(void *addr, size_t len)
{
    /* Intercept our mock buffer */
    if (addr == g_drm.mmap_ptr && g_drm.mmap_ptr != NULL) {
        g_drm.munmap_count++;
        free(g_drm.mmap_ptr);
        g_drm.mmap_ptr = NULL;
        (void)len;
        return 0;
    }
    return __real_munmap(addr, len);
}

int __wrap_usleep(useconds_t usec)
{
    (void)usec;
    g_drm.usleep_count++;
    return 0;
}

/* ── DRM ioctl wrap ──────────────────────────────────────────────────────── */

int __wrap_drmIoctl(int fd, unsigned long request, void *arg)
{
    (void)fd;

    if (request == DRM_IOCTL_MODE_CREATE_DUMB) {
        struct drm_mode_create_dumb *creq = arg;
        if (g_drm.create_dumb_ret < 0) return g_drm.create_dumb_ret;
        creq->handle = g_drm.create_dumb_handle;
        creq->pitch  = g_drm.create_dumb_pitch;
        creq->size   = g_drm.create_dumb_size;
        return 0;
    }

    if (request == DRM_IOCTL_MODE_MAP_DUMB) {
        struct drm_mode_map_dumb *mreq = arg;
        if (g_drm.map_dumb_ret < 0) return g_drm.map_dumb_ret;
        mreq->offset = 0;
        return 0;
    }

    if (request == DRM_IOCTL_MODE_DESTROY_DUMB) {
        g_drm.destroy_dumb_count++;
        return 0;
    }

    /* Unknown ioctl — return success */
    return 0;
}

/* ── DRM master ──────────────────────────────────────────────────────────── */

int __wrap_drmSetMaster(int fd)
{
    (void)fd;
    return g_drm.set_master_ret;
}

int __wrap_drmDropMaster(int fd)
{
    (void)fd;
    return 0;
}

/* ── DRM mode resources ──────────────────────────────────────────────────── */

drmModeRes *__wrap_drmModeGetResources(int fd)
{
    (void)fd;
    if (g_drm.get_resources_fail) return NULL;

    drmModeRes *res = calloc(1, sizeof(*res));
    res->count_crtcs = g_drm.num_crtcs;
    res->crtcs = calloc(g_drm.num_crtcs, sizeof(uint32_t));
    for (int i = 0; i < g_drm.num_crtcs; i++) {
        res->crtcs[i] = g_drm.crtc_ids[i];
    }
    return res;
}

void __wrap_drmModeFreeResources(drmModeRes *res)
{
    if (res) {
        free(res->crtcs);
        free(res);
    }
}

/* ── DRM CRTC ────────────────────────────────────────────────────────────── */

drmModeCrtc *__wrap_drmModeGetCrtc(int fd, uint32_t crtc_id)
{
    (void)fd;

    /* Find the crtc_id in our mock state */
    for (int i = 0; i < g_drm.num_crtcs; i++) {
        if (g_drm.crtc_ids[i] == crtc_id) {
            drmModeCrtc *crtc = calloc(1, sizeof(*crtc));
            crtc->crtc_id = crtc_id;
            crtc->mode_valid = g_drm.crtc_mode_valid[i] ? 1 : 0;
            if (crtc->mode_valid) {
                crtc->mode.hdisplay = g_drm.crtc_width[i];
                crtc->mode.vdisplay = g_drm.crtc_height[i];
            }
            return crtc;
        }
    }
    return NULL;
}

void __wrap_drmModeFreeCrtc(drmModeCrtc *crtc)
{
    free(crtc);
}

/* ── DRM CRTC set ────────────────────────────────────────────────────────── */

int __wrap_drmModeSetCrtc(int fd, uint32_t crtc_id, uint32_t fb_id,
                           uint32_t x, uint32_t y,
                           uint32_t *connectors, int count,
                           drmModeModeInfoPtr mode)
{
    (void)fd; (void)crtc_id; (void)fb_id;
    (void)x; (void)y; (void)connectors; (void)count; (void)mode;
    g_drm.set_crtc_count++;
    return g_drm.set_crtc_ret;
}

/* ── DRM framebuffer ─────────────────────────────────────────────────────── */

int __wrap_drmModeAddFB(int fd, uint32_t width, uint32_t height,
                         uint8_t depth, uint8_t bpp, uint32_t pitch,
                         uint32_t bo_handle, uint32_t *buf_id)
{
    (void)fd; (void)width; (void)height; (void)depth;
    (void)bpp; (void)pitch; (void)bo_handle;
    g_drm.add_fb_count++;
    if (g_drm.add_fb_ret < 0) return g_drm.add_fb_ret;
    if (buf_id) *buf_id = g_drm.add_fb_id;
    return 0;
}

int __wrap_drmModeRmFB(int fd, uint32_t fb_id)
{
    (void)fd; (void)fb_id;
    g_drm.rm_fb_count++;
    return 0;
}
