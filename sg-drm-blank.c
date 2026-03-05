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
 *   sg-drm-blank -v                — verbose logging for debugging
 *   sg-drm-blank --verbose /dev/dri/card0
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

static int verbose = 0;

#define VLOG(...) do { if (verbose) fprintf(stderr, __VA_ARGS__); } while (0)

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
    VLOG("  open OK fd=%d path=%s\n", fd, path);

    /* Must be DRM master — only possible when no compositor is running */
    if (drmSetMaster(fd) < 0) {
        VLOG("  drmSetMaster FAILED: %s\n", strerror(errno));
        /* Not an error if device is in use by another compositor */
        close(fd);
        return 0;
    }
    VLOG("  drmSetMaster OK\n");

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        VLOG("  GetResources FAILED\n");
        drmDropMaster(fd);
        close(fd);
        return -1;
    }
    VLOG("  GetResources OK, %d CRTCs\n", res->count_crtcs);

    int blanked = 0;

    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (!crtc) {
            VLOG("  GetCrtc[%d] NULL\n", i);
            continue;
        }
        VLOG("  CRTC[%d] id=%u valid=%d %ux%u\n",
             i, crtc->crtc_id, crtc->mode_valid,
             crtc->mode.hdisplay, crtc->mode.vdisplay);

        if (!crtc->mode_valid || crtc->mode.hdisplay == 0) {
            drmModeFreeCrtc(crtc);
            continue;
        }

        uint32_t w = crtc->mode.hdisplay;
        uint32_t h = crtc->mode.vdisplay;

        BlackFB fb = {0};
        if (create_black_fb(fd, w, h, &fb) < 0) {
            VLOG("  create_black_fb FAILED\n");
            destroy_fb(fd, &fb);   /* clean up partial allocation */
            drmModeFreeCrtc(crtc);
            continue;
        }
        VLOG("  create_black_fb OK fb_id=%u\n", fb.fb_id);

        /* Look up connectors bound to this CRTC via encoders.
         * Some drivers (e.g. VKMS) require connectors to be passed. */
        uint32_t conn_ids[16];
        int conn_count = 0;
        for (int j = 0; j < res->count_connectors && conn_count < 16; j++) {
            drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[j]);
            if (!conn) continue;
            if (conn->encoder_id) {
                drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
                if (enc) {
                    if (enc->crtc_id == crtc->crtc_id)
                        conn_ids[conn_count++] = conn->connector_id;
                    drmModeFreeEncoder(enc);
                }
            }
            drmModeFreeConnector(conn);
        }
        VLOG("  found %d connectors for CRTC %u\n", conn_count, crtc->crtc_id);

        if (drmModeSetCrtc(fd, crtc->crtc_id, fb.fb_id, 0, 0,
                           conn_count > 0 ? conn_ids : NULL,
                           conn_count, &crtc->mode) == 0) {
            blanked++;
            VLOG("  SetCrtc OK blanked=%d\n", blanked);
        } else {
            fprintf(stderr, "sg-drm-blank: SetCrtc failed on CRTC %u: %s "
                    "(non-fatal, compositor will handle display)\n",
                    crtc->crtc_id, strerror(errno));
        }

        /* Hold black frame briefly so display settles before handoff */
        usleep(200000);   /* 200 ms */

        destroy_fb(fd, &fb);
        drmModeFreeCrtc(crtc);
    }

    drmModeFreeResources(res);
    drmDropMaster(fd);
    close(fd);

    VLOG("  returning %d\n", blanked);
    return blanked;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *device_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
            verbose = 1;
        else
            device_path = argv[i];
    }

    /* Explicit device path provided */
    if (device_path) {
        blank_device(device_path);
        return 0;   /* best-effort: never block boot */
    }

    /* Auto-enumerate all DRM devices */
    drmDevicePtr devices[16];
    int n = drmGetDevices2(0, devices, 16);
    if (n <= 0)
        return 0;   /* no devices is fine — compositor will handle display */

    for (int i = 0; i < n; i++) {
        if (!(devices[i]->available_nodes & (1 << DRM_NODE_PRIMARY)))
            continue;
        blank_device(devices[i]->nodes[DRM_NODE_PRIMARY]);
    }

    drmFreeDevices(devices, n);
    return 0;
}
