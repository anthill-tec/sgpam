/*
 * test_drm_blank_vkms.c — VKMS integration tests for sg-drm-blank
 *
 * Unlike the unit tests (test_drm_blank.c) which use --wrap mocking,
 * these tests call real kernel DRM ioctls against a VKMS virtual display.
 *
 * DRM master contention: the Linux kernel tracks DRM master history
 * per-process. Once a process has opened a DRM device with O_RDWR
 * (which auto-grants master), subsequent drmSetMaster calls on new fds
 * in the same process may fail. To work around this:
 *
 *   1. VKMS detection uses sysfs (no DRM device open)
 *   2. CRTC activation/deactivation runs in background subprocesses
 *   3. Tests that call blank_device() never open the DRM device directly
 *
 * drm_lastclose: when the last DRM fd on a device closes, the kernel
 * restores the fbdev console (drm_fb_helper_lastclose), re-enabling
 * the CRTC. Background subprocesses keep their fd open to prevent this.
 *
 * Requirements:
 *   - vkms kernel module loaded:  sudo modprobe vkms
 *   - root privileges:            sudo make test-vkms
 *
 * Suite: vkms_integration (5 tests)
 *   card_detected        — VKMS device is found via sysfs
 *   blank_active_crtc    — blank_device() returns 1 after activating CRTC
 *   blank_inactive_crtc  — blank_device() returns 0 when CRTC is inactive
 *   explicit_path_arg    — sg_drm_blank_main(2, argv) returns 0
 *   resources_accessible — drmModeGetResources succeeds, has connectors + CRTCs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <criterion/criterion.h>

/* Pull in sg-drm-blank.c — main renamed via -Dmain=sg_drm_blank_main */
#include "../sg-drm-blank.c"

/* ── Shared state ────────────────────────────────────────────────────── */

static char vkms_path[256];

/* PID of subprocess holding the CRTC active */
static pid_t helper_pid = -1;

/* ── Helper: find VKMS card via sysfs ────────────────────────────────── *
 *
 * Detects VKMS by resolving the /sys/class/drm/cardN symlink and
 * checking if the device path contains "vkms". This avoids opening
 * the DRM device (which would auto-grant master and contaminate the
 * process's DRM master history).
 *
 * Note: on kernel 6.19+, VKMS uses the "faux" bus so the uevent
 * shows DRIVER=faux_driver, but the device path still contains "vkms".
 */

static bool find_vkms_card(char *path, size_t len)
{
    for (int i = 0; i < 16; i++) {
        char sysfs[256], resolved[512];
        snprintf(sysfs, sizeof(sysfs), "/sys/class/drm/card%d", i);

        ssize_t rlen = readlink(sysfs, resolved, sizeof(resolved) - 1);
        if (rlen < 0)
            continue;
        resolved[rlen] = '\0';

        if (strstr(resolved, "vkms") != NULL) {
            snprintf(path, len, "/dev/dri/card%d", i);
            return true;
        }
    }
    return false;
}

/* ── Helper: activate VKMS CRTC in a subprocess ─────────────────────── *
 *
 * Forks a child that opens the VKMS device, activates a CRTC with a
 * dumb framebuffer, drops DRM master, and pauses. The child keeps the
 * fd + FB alive so the CRTC stays active. This isolates the DRM master
 * history from the test process, allowing blank_device() to call
 * drmSetMaster() on a fresh fd without contention.
 *
 * Uses a pipe to synchronize: child writes 'Y' when ready, 'N' on failure.
 * Call stop_vkms_helper() to kill the child and release resources.
 */

static int activate_vkms_crtc_background(const char *path)
{
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* ── Child process ─────────────────────────────────────────── */
        close(pipefd[0]);

        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            write(pipefd[1], "N", 1);
            close(pipefd[1]);
            _exit(1);
        }

        if (drmSetMaster(fd) < 0) {
            close(fd);
            write(pipefd[1], "N", 1);
            close(pipefd[1]);
            _exit(1);
        }

        drmModeRes *res = drmModeGetResources(fd);
        if (!res) {
            drmDropMaster(fd);
            close(fd);
            write(pipefd[1], "N", 1);
            close(pipefd[1]);
            _exit(1);
        }

        char status = 'N';

        for (int i = 0; i < res->count_connectors; i++) {
            drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
            if (!conn)
                continue;

            if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
                drmModeFreeConnector(conn);
                continue;
            }

            drmModeModeInfo *mode = &conn->modes[0];

            uint32_t crtc_id = 0;
            if (conn->encoder_id) {
                drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
                if (enc) {
                    crtc_id = enc->crtc_id;
                    drmModeFreeEncoder(enc);
                }
            }
            if (crtc_id == 0 && res->count_crtcs > 0)
                crtc_id = res->crtcs[0];

            if (crtc_id == 0) {
                drmModeFreeConnector(conn);
                continue;
            }

            struct drm_mode_create_dumb creq = {
                .width  = mode->hdisplay,
                .height = mode->vdisplay,
                .bpp    = 32,
            };
            if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
                drmModeFreeConnector(conn);
                continue;
            }

            uint32_t fb_id = 0;
            if (drmModeAddFB(fd, mode->hdisplay, mode->vdisplay, 24, 32,
                             creq.pitch, creq.handle, &fb_id) < 0) {
                struct drm_mode_destroy_dumb dreq = { .handle = creq.handle };
                drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
                drmModeFreeConnector(conn);
                continue;
            }

            uint32_t conn_id = conn->connector_id;
            if (drmModeSetCrtc(fd, crtc_id, fb_id, 0, 0,
                               &conn_id, 1, mode) == 0) {
                drmDropMaster(fd);
                status = 'Y';
            } else {
                drmModeRmFB(fd, fb_id);
                struct drm_mode_destroy_dumb dreq = { .handle = creq.handle };
                drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
            }

            drmModeFreeConnector(conn);
            break;
        }

        drmModeFreeResources(res);

        write(pipefd[1], &status, 1);
        close(pipefd[1]);

        if (status == 'Y') {
            /* Keep fd + FB alive — kernel deactivates CRTC if we exit.
             * pause() until parent sends SIGTERM. */
            pause();
        }

        close(fd);
        _exit(status == 'Y' ? 0 : 1);
    }

    /* ── Parent process ────────────────────────────────────────────── */
    close(pipefd[1]);
    char status = 'N';
    read(pipefd[0], &status, 1);
    close(pipefd[0]);

    if (status != 'Y') {
        waitpid(pid, NULL, 0);
        return -1;
    }

    helper_pid = pid;
    return 0;
}

static void stop_vkms_helper(void)
{
    if (helper_pid > 0) {
        kill(helper_pid, SIGTERM);
        waitpid(helper_pid, NULL, 0);
        helper_pid = -1;
    }
}

/* ── Helper: deactivate VKMS CRTC via background subprocess ─────────── *
 *
 * Like activate_vkms_crtc_background, this forks a child that disables
 * the CRTC and then pauses with the fd held open. Keeping the fd open
 * prevents drm_lastclose from firing — without this, the kernel's fbdev
 * helper restores the console mode and re-enables the CRTC immediately
 * after the last DRM fd closes.
 *
 * Call stop_vkms_helper() to kill the child (which closes the fd).
 */

static int deactivate_vkms_crtc_background(const char *path)
{
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* ── Child process ─────────────────────────────────────────── */
        close(pipefd[0]);

        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            write(pipefd[1], "N", 1);
            close(pipefd[1]);
            _exit(1);
        }

        if (drmSetMaster(fd) < 0) {
            close(fd);
            write(pipefd[1], "N", 1);
            close(pipefd[1]);
            _exit(1);
        }

        drmModeRes *res = drmModeGetResources(fd);
        if (!res) {
            drmDropMaster(fd);
            close(fd);
            write(pipefd[1], "N", 1);
            close(pipefd[1]);
            _exit(1);
        }

        char status = 'Y';
        for (int i = 0; i < res->count_crtcs; i++) {
            if (drmModeSetCrtc(fd, res->crtcs[i], 0, 0, 0,
                               NULL, 0, NULL) < 0)
                status = 'N';
        }

        drmModeFreeResources(res);
        drmDropMaster(fd);

        write(pipefd[1], &status, 1);
        close(pipefd[1]);

        if (status == 'Y') {
            /* Keep fd open — prevents drm_lastclose from restoring fbdev.
             * pause() until parent sends SIGTERM. */
            pause();
        }

        close(fd);
        _exit(status == 'Y' ? 0 : 1);
    }

    /* ── Parent process ────────────────────────────────────────────── */
    close(pipefd[1]);
    char status = 'N';
    read(pipefd[0], &status, 1);
    close(pipefd[0]);

    if (status != 'Y') {
        waitpid(pid, NULL, 0);
        return -1;
    }

    helper_pid = pid;
    return 0;
}

/* ── Skip macro ──────────────────────────────────────────────────────── */

#define SKIP_IF_NO_VKMS()                                        \
    do {                                                         \
        if (geteuid() != 0)                                      \
            cr_skip_test("requires root");                       \
        if (!find_vkms_card(vkms_path, sizeof(vkms_path)))       \
            cr_skip_test("vkms module not loaded");              \
    } while (0)

/* ── Cleanup callback for .fini — ensures helper is killed on failure ── */

static void cleanup_helper(void)
{
    stop_vkms_helper();
}

/* ══════════════════════════════════════════════════════════════════════
 *  Suite: vkms_integration
 *
 *  CRITICAL: tests that call blank_device() must NEVER open the DRM
 *  device directly. Opening with O_RDWR auto-grants master, and the
 *  kernel tracks this per-process — subsequent drmSetMaster() calls
 *  on new fds will fail. All DRM device access in these tests runs
 *  in subprocesses to keep the test process's DRM state clean.
 *
 *  Tests with a helper use .fini = cleanup_helper so the subprocess is
 *  always killed, even when an assertion fails mid-test.
 * ══════════════════════════════════════════════════════════════════════ */

Test(vkms_integration, card_detected)
{
    SKIP_IF_NO_VKMS();

    cr_assert(strncmp(vkms_path, "/dev/dri/card", 13) == 0,
              "VKMS path should start with /dev/dri/card, got: %s", vkms_path);
}

Test(vkms_integration, blank_active_crtc, .fini = cleanup_helper)
{
    SKIP_IF_NO_VKMS();

    int act = activate_vkms_crtc_background(vkms_path);
    cr_assert_eq(act, 0, "failed to activate VKMS CRTC in subprocess");

    verbose = 1;
    int ret = blank_device(vkms_path);
    verbose = 0;
    cr_assert_eq(ret, 1, "blank_device should blank 1 active CRTC, got %d", ret);
}

Test(vkms_integration, blank_inactive_crtc, .fini = cleanup_helper)
{
    SKIP_IF_NO_VKMS();

    /* Deactivate in background — subprocess keeps fd open to prevent
     * drm_lastclose from restoring fbdev mode (which would re-enable CRTC) */
    int deact = deactivate_vkms_crtc_background(vkms_path);
    cr_assert_eq(deact, 0, "failed to deactivate VKMS CRTC");

    int ret = blank_device(vkms_path);
    cr_assert_eq(ret, 0,
                 "blank_device should return 0 with no active CRTCs, got %d", ret);
}

Test(vkms_integration, explicit_path_arg, .fini = cleanup_helper)
{
    SKIP_IF_NO_VKMS();

    int act = activate_vkms_crtc_background(vkms_path);
    cr_assert_eq(act, 0, "failed to activate VKMS CRTC");

    char *argv[] = { "sg-drm-blank", vkms_path };
    int ret = sg_drm_blank_main(2, argv);
    cr_assert_eq(ret, 0, "main with explicit path should return 0, got %d", ret);
}

Test(vkms_integration, resources_accessible)
{
    SKIP_IF_NO_VKMS();

    /* Open in a subprocess to avoid DRM master contamination */
    int pipefd[2];
    cr_assert_eq(pipe(pipefd), 0, "pipe failed");

    pid_t pid = fork();
    cr_assert_geq(pid, 0, "fork failed");

    if (pid == 0) {
        close(pipefd[0]);

        int fd = open(vkms_path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            write(pipefd[1], "\x00\x00", 2);
            close(pipefd[1]);
            _exit(1);
        }

        drmModeRes *res = drmModeGetResources(fd);
        if (!res) {
            write(pipefd[1], "\x00\x00", 2);
            close(pipefd[1]);
            close(fd);
            _exit(1);
        }

        char data[2] = { (char)res->count_connectors, (char)res->count_crtcs };
        write(pipefd[1], data, 2);
        close(pipefd[1]);

        drmModeFreeResources(res);
        close(fd);
        _exit(0);
    }

    close(pipefd[1]);
    char data[2] = {0, 0};
    read(pipefd[0], data, 2);
    close(pipefd[0]);

    int wstatus;
    waitpid(pid, &wstatus, 0);
    cr_assert(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0,
              "subprocess failed to query DRM resources");

    int connectors = (unsigned char)data[0];
    int crtcs = (unsigned char)data[1];

    cr_assert_geq(connectors, 1, "VKMS should have at least 1 connector, got %d", connectors);
    cr_assert_geq(crtcs, 1, "VKMS should have at least 1 CRTC, got %d", crtcs);
}
