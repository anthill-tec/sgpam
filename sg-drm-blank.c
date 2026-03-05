/*
 * sg-drm-blank.c — Black out all active CRTCs via DRM before compositor handoff
 *
 * Driver-agnostic: auto-enumerates all DRM devices via libdrm and blanks
 * every active CRTC found. Works with amdgpu, i915, nouveau, vc4, etc.
 *
 * Build:
 *   gcc -O2 -o sg-drm-blank sg-drm-blank.c $(pkg-config --cflags --libs libdrm)
 *
 * Usage:
 *   sg-drm-blank                   — auto-detect all DRM devices
 *   sg-drm-blank /dev/dri/card0    — target a specific device
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

/* ── dumb buffer + framebuffer ───────────────────────────────────────────── */

typedef struct {
    uint32_t fb_id;
    uint32_t handle;
    uint32_t stride;
    uint64_t size;
    void    *map;
} BlackFB;

static int create_black_fb(int fd, uint32_t width, uint32_t height, BlackFB *out)
{
    struct drm_mode_create_dumb creq = {
        .width  = width,
        .height = height,
        .bpp    = 32,
    };

    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB");
        return -1;
    }

    out->handle = creq.handle;
    out->stride = creq.pitch;
    out->size   = creq.size;

    if (drmModeAddFB(fd, width, height, 24, 32, creq.pitch,
                     creq.handle, &out->fb_id) < 0) {
        perror("drmModeAddFB");
        return -1;
    }

    struct drm_mode_map_dumb mreq = { .handle = creq.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB");
        return -1;
    }

    out->map = mmap(NULL, creq.size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, mreq.offset);
    if (out->map == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    memset(out->map, 0, creq.size);   /* pure black */
    return 0;
}

static void destroy_fb(int fd, BlackFB *fb)
{
    if (fb->map)    munmap(fb->map, fb->size);
    if (fb->fb_id)  drmModeRmFB(fd, fb->fb_id);
    if (fb->handle) {
        struct drm_mode_destroy_dumb dreq = { .handle = fb->handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    }
}

/* ── blank one DRM device ────────────────────────────────────────────────── */

static int blank_device(const char *path)
{
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "sg-drm-blank: cannot open %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    /* Must be DRM master — only possible when no compositor is running */
    if (drmSetMaster(fd) < 0) {
        /* Not an error if device is in use by another compositor */
        close(fd);
        return 0;
    }

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        drmDropMaster(fd);
        close(fd);
        return -1;
    }

    int blanked = 0;

    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (!crtc) continue;

        if (!crtc->mode_valid || crtc->mode.hdisplay == 0) {
            drmModeFreeCrtc(crtc);
            continue;
        }

        uint32_t w = crtc->mode.hdisplay;
        uint32_t h = crtc->mode.vdisplay;

        BlackFB fb = {0};
        if (create_black_fb(fd, w, h, &fb) < 0) {
            destroy_fb(fd, &fb);   /* clean up partial allocation */
            drmModeFreeCrtc(crtc);
            continue;
        }

        if (drmModeSetCrtc(fd, crtc->crtc_id, fb.fb_id,
                           0, 0, NULL, 0, &crtc->mode) == 0) {
            blanked++;
        }

        /* Hold black frame briefly so display settles before handoff */
        usleep(200000);   /* 200 ms */

        destroy_fb(fd, &fb);
        drmModeFreeCrtc(crtc);
    }

    drmModeFreeResources(res);
    drmDropMaster(fd);
    close(fd);

    return blanked;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    /* Explicit device path provided */
    if (argc > 1) {
        int ret = blank_device(argv[1]);
        return (ret > 0) ? 0 : 1;
    }

    /* Auto-enumerate all DRM devices */
    drmDevicePtr devices[16];
    int n = drmGetDevices2(0, devices, 16);
    if (n <= 0) {
        fprintf(stderr, "sg-drm-blank: no DRM devices found\n");
        return 1;
    }

    int total_blanked = 0;

    for (int i = 0; i < n; i++) {
        /* Only process primary nodes (card0, card1, etc.) */
        if (!(devices[i]->available_nodes & (1 << DRM_NODE_PRIMARY)))
            continue;

        const char *path = devices[i]->nodes[DRM_NODE_PRIMARY];
        int ret = blank_device(path);
        if (ret > 0)
            total_blanked += ret;
    }

    drmFreeDevices(devices, n);

    return (total_blanked > 0) ? 0 : 1;
}
