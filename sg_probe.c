/*
 * sg_probe.c — interactive SDK diagnostic tool for the SecuGen U20.
 *
 * This is a DEVELOPER/DEBUG tool, NOT part of the package: it never touches
 * /etc/pam.d, templates, or any system state. It only opens the device and
 * exercises the SDK so we can confirm the FDx SDK behaves the way pam_sgfp.c
 * and sg_enroll.c assume — in particular whether SGFPM_GetDeviceInfo() reports
 * the LED brightness live (i.e. reflects a prior SGFPM_SetBrightness).
 *
 * Build:  make sg-probe SGDK=...
 * Run  :  sudo ./sg-probe <command>
 *
 * Commands:
 *   info                     Open device, print one device-info snapshot.
 *   sweep [start end step]    Set brightness across a range; after each set,
 *                             re-read device info and print set vs reported.
 *                             Default range: 0 100 10.
 *   ladder [retries]          Reproduce pam_sgfp's adaptive ladder: read the
 *                             floor from the device, compute the rungs for N
 *                             retries (floor->100), set each and report back.
 *                             Default retries: 3.
 *   capture [brightness]      Optionally set brightness, then capture one image
 *                             and print its quality plus the reported brightness.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include "sgfplib.h"

#define SECUGEN_VID 0x1162

#define DEVICE_NAME     SG_DEV_FDU05
#define CAPTURE_TIMEOUT 10000
#define BR_MAX          100

/* Human-readable names for the SDK error codes we are likely to see. */
static const char *err_name(DWORD e)
{
    switch (e) {
        case SGFDX_ERROR_NONE:             return "NONE";
        case SGFDX_ERROR_FUNCTION_FAILED:  return "FUNCTION_FAILED";
        case SGFDX_ERROR_INVALID_PARAM:    return "INVALID_PARAM";
        case SGFDX_ERROR_INITIALIZE_FAILED:return "INITIALIZE_FAILED";
        case SGFDX_ERROR_TIME_OUT:         return "TIME_OUT";
        case SGFDX_ERROR_DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
        case SGFDX_ERROR_WRONG_IMAGE:      return "WRONG_IMAGE";
        default:                           return "?";
    }
}

static HSGFPM open_device(void)
{
    HSGFPM h = NULL;
    DWORD err;

    err = SGFPM_Create(&h);
    if (err != SGFDX_ERROR_NONE) {
        fprintf(stderr, "SGFPM_Create failed: %lu (%s)\n", err, err_name(err));
        exit(1);
    }
    err = SGFPM_Init(h, DEVICE_NAME);
    if (err != SGFDX_ERROR_NONE) {
        fprintf(stderr, "SGFPM_Init failed: %lu (%s) — are the SDK libs installed?\n",
                err, err_name(err));
        exit(1);
    }
    err = SGFPM_OpenDevice(h, USB_AUTO_DETECT);
    if (err != SGFDX_ERROR_NONE) {
        fprintf(stderr, "SGFPM_OpenDevice failed: %lu (%s) — U20 plugged in? running as root?\n",
                err, err_name(err));
        exit(1);
    }
    return h;
}

static void close_device(HSGFPM h)
{
    SGFPM_CloseDevice(h);
    SGFPM_Terminate(h);
}

/* Read and return device info; prints an error and zero-fills on failure. */
static SGDeviceInfoParam read_info(HSGFPM h)
{
    SGDeviceInfoParam di;
    memset(&di, 0, sizeof(di));
    DWORD err = SGFPM_GetDeviceInfo(h, &di);
    if (err != SGFDX_ERROR_NONE)
        fprintf(stderr, "  SGFPM_GetDeviceInfo failed: %lu (%s)\n", err, err_name(err));
    return di;
}

/* Trim trailing spaces/tabs in place (the SDK pads DeviceSN to fixed width). */
static void rstrip(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
}

static void print_info(const SGDeviceInfoParam *di, const char *tag)
{
    char sn[sizeof(di->DeviceSN) + 1];
    snprintf(sn, sizeof sn, "%s", (const char *)di->DeviceSN);
    rstrip(sn);
    printf("[%s] image=%lux%lu dpi=%lu  contrast=%lu brightness=%lu gain=%lu  "
           "fw=%lu sn=%s\n",
           tag, di->ImageWidth, di->ImageHeight, di->ImageDPI,
           di->Contrast, di->Brightness, di->Gain,
           di->FWVersion, sn);
}

static int cmd_info(HSGFPM h)
{
    SGDeviceInfoParam di = read_info(h);
    print_info(&di, "info");
    DWORD maxt = 0;
    SGFPM_GetMaxTemplateSize(h, &maxt);
    printf("  max_template_size=%lu\n", maxt);
    return 0;
}

/* The key test: does GetDeviceInfo report the value we just Set, and does
 * SetBrightness perturb contrast/gain as a side effect? */
static int cmd_sweep(HSGFPM h, int start, int end, int step)
{
    if (step <= 0) step = 10;
    printf("Brightness sweep — does SetBrightness move brightness, "
           "and does it touch contrast/gain?\n");
    printf("  %-5s %-20s %-11s %-10s %-8s\n",
           "set", "SetBrightness rc", "brightness", "contrast", "gain");
    for (int b = start; b <= end; b += step) {
        DWORD err = SGFPM_SetBrightness(h, (DWORD)b);
        SGDeviceInfoParam di = read_info(h);
        printf("  %-5d %lu (%-14s) %-11lu %-10lu %-8lu\n",
               b, err, err_name(err), di.Brightness, di.Contrast, di.Gain);
    }
    return 0;
}

/* Poll GetDeviceInfo repeatedly to see if the firmware drifts any field on
 * its own (e.g. automatic gain) while the device sits idle. */
static int cmd_watch(HSGFPM h, int count, int delay_ms)
{
    if (count < 1) count = 10;
    if (delay_ms < 0) delay_ms = 500;
    printf("Watching device info %d times @ %d ms (looking for autonomous drift):\n",
           count, delay_ms);
    for (int i = 0; i < count; i++) {
        SGDeviceInfoParam di = read_info(h);
        printf("  #%-3d brightness=%-4lu contrast=%-4lu gain=%-4lu\n",
               i, di.Brightness, di.Contrast, di.Gain);
        if (i + 1 < count)
            usleep((useconds_t)delay_ms * 1000);
    }
    return 0;
}

/* Reproduce pam_sgfp's adaptive ladder for N retries from the live floor. */
static int cmd_ladder(HSGFPM h, int retries)
{
    if (retries < 1) retries = 1;
    SGDeviceInfoParam di = read_info(h);
    DWORD floor = (di.Brightness >= 1 && di.Brightness <= BR_MAX) ? di.Brightness : 50;
    printf("Adaptive ladder: readout=%lu -> floor=%lu, retries=%d, ceiling=%d\n",
           di.Brightness, floor, retries, BR_MAX);
    printf("  attempt 1 keeps the sensor default (%lu, not set)\n", floor);
    for (int k = 2; k <= retries; k++) {
        DWORD cur = floor + (DWORD)(BR_MAX - floor) * (DWORD)(k - 1) / (DWORD)(retries - 1);
        if (cur > BR_MAX) cur = BR_MAX;
        DWORD err = SGFPM_SetBrightness(h, cur);
        SGDeviceInfoParam d2 = read_info(h);
        printf("  attempt %d: set=%lu  rc=%lu (%s)  brightness=%lu contrast=%lu gain=%lu\n",
               k, cur, err, err_name(err), d2.Brightness, d2.Contrast, d2.Gain);
    }
    return 0;
}

static int cmd_capture(HSGFPM h, int brightness, int smart)
{
    SGDeviceInfoParam before = read_info(h);
    print_info(&before, "before    ");

    /* Smart Capture toggle — WriteData index 5 (1=enable, 0=disable), exactly
     * as SecuGen's fpmatcher/sgd2 samples do. -1 leaves the device default. */
    if (smart >= 0) {
        DWORD err = SGFPM_WriteData(h, 5, (unsigned char)(smart ? 1 : 0));
        printf("WriteData(5, %d)  [Smart Capture %s]: rc=%lu (%s)\n",
               smart ? 1 : 0, smart ? "ON" : "OFF", err, err_name(err));
    }

    if (brightness >= 0) {
        DWORD err = SGFPM_SetBrightness(h, (DWORD)brightness);
        printf("SetBrightness(%d): rc=%lu (%s)\n", brightness, err, err_name(err));
        SGDeviceInfoParam afterset = read_info(h);
        print_info(&afterset, "after-set ");
    }

    if (before.ImageWidth == 0 || before.ImageHeight == 0) {
        fprintf(stderr, "bad image dimensions %lux%lu\n",
                before.ImageWidth, before.ImageHeight);
        return 1;
    }

    BYTE *img = malloc(before.ImageWidth * before.ImageHeight);
    if (!img) { fprintf(stderr, "malloc failed\n"); return 1; }

    printf("Place finger on the scanner (timeout %d ms, accepting any quality)...\n",
           CAPTURE_TIMEOUT);
    DWORD err = SGFPM_GetImageEx(h, img, CAPTURE_TIMEOUT, NULL, 0);
    printf("SGFPM_GetImageEx: rc=%lu (%s)\n", err, err_name(err));

    if (err == SGFDX_ERROR_NONE) {
        DWORD q = 0;
        SGFPM_GetImageQuality(h, before.ImageWidth, before.ImageHeight, img, &q);
        SGDeviceInfoParam after = read_info(h);
        print_info(&after, "after-cap ");
        printf("  >> image quality=%lu/100  (compare contrast/gain before vs after "
               "to spot firmware AGC)\n", q);
    }
    free(img);
    return 0;
}

/* Quality-vs-brightness with Smart Capture ON, sweeping in ONE finger session
 * (finger held on the platen) so placement variance doesn't swamp the signal. */
static int cmd_qsweep(HSGFPM h, int start, int end, int step)
{
    if (step <= 0) step = 20;
    DWORD werr = SGFPM_WriteData(h, 5, 1);   /* Smart Capture ON */
    printf("Quality sweep, Smart Capture ON (WriteData(5,1) rc=%lu %s).\n"
           "Hold ONE finger steady on the platen for the whole sweep.\n",
           werr, err_name(werr));

    SGDeviceInfoParam di = read_info(h);
    if (di.ImageWidth == 0 || di.ImageHeight == 0) {
        fprintf(stderr, "bad image dimensions\n");
        return 1;
    }
    BYTE *img = malloc(di.ImageWidth * di.ImageHeight);
    if (!img) { fprintf(stderr, "malloc failed\n"); return 1; }

    printf("  %-7s %-20s %-6s %-7s\n", "bright", "GetImageEx rc", "gain", "quality");
    for (int b = start; b <= end; b += step) {
        SGFPM_SetBrightness(h, (DWORD)b);
        DWORD err = SGFPM_GetImageEx(h, img, 4000, NULL, 0);
        DWORD q = 0;
        if (err == SGFDX_ERROR_NONE)
            SGFPM_GetImageQuality(h, di.ImageWidth, di.ImageHeight, img, &q);
        SGDeviceInfoParam after = read_info(h);
        printf("  %-7d %lu (%-14s) %-6lu %-7lu\n",
               b, err, err_name(err), after.Gain, q);
        fflush(stdout);   /* show progress even if the run is interrupted */
    }
    free(img);
    return 0;
}

/* Map an SGFDxDeviceName code to a human model string. */
static const char *model_name(DWORD dev)
{
    switch (dev) {
        case SG_DEV_UNKNOWN:    return "UNKNOWN";
        case SG_DEV_FDP02:      return "FDP02";
        case SG_DEV_FDU02:      return "FDU02";
        case SG_DEV_FDU03:      return "FDU03 (Hamster Plus)";
        case SG_DEV_FDU04:      return "FDU04 (Hamster IV)";
        case SG_DEV_FDU05:      return "FDU05 (HU20 / U20)";
        case SG_DEV_FDU06:      return "FDU06 (UPx)";
        case SG_DEV_FDU07:      return "FDU07 (U10)";
        case SG_DEV_FDU07A:     return "FDU07A (U10-AP)";
        case SG_DEV_FDU08:      return "FDU08 (U20A)";
        case SG_DEV_FDU08P:     return "FDU08P (U20-AP, discontinued)";
        case SG_DEV_FDU06P:     return "FDU06P (UPx-P)";
        case SG_DEV_FDUSDA:     return "FDUSDA (U20-ASF-BT SPP)";
        case SG_DEV_FDUSDA_BLE: return "FDUSDA_BLE (U20-ASF-BT BLE)";
        case SG_DEV_FDU08X:     return "FDU08X (U20-ASFX USB)";
        case SG_DEV_FDU09:      return "FDU09 (U30, discontinued)";
        case SG_DEV_FDU08A:     return "FDU08A (U20-AP)";
        case SG_DEV_FDU09A:     return "FDU09A (U30)";
        case SG_DEV_FDU10A:     return "FDU10A (U-AIR)";
        case SG_DEV_AUTO:       return "AUTO";
        default:                return "?";
    }
}

/* SecuGen USB product-ID -> model, seeded from the vendor's 99SecuGen.rules.
 * The SDK's EnumerateDevice DevName is unreliable (returns UNKNOWN for the U20),
 * so the USB VID:PID is the canonical model identifier. */
static const char *usb_pid_model(unsigned pid)
{
    switch (pid) {
        case 0x0320: return "FDU03 (Hamster Plus)";
        case 0x0322: return "SDU03M";
        case 0x0330: return "FDU04 (Hamster IV)";
        case 0x1000: return "SDU03P";
        case 0x2000: return "SDU04P";
        case 0x2200: return "U20 (FDU05)";
        case 0x2201: return "UPx (FDU06)";
        case 0x2203: return "U10 (FDU07)";
        case 0x2240: return "U20A (FDU08)";
        case 0x2360: return "U20-AP";
        case 0x2410: return "U30A";
        case 0x2500: return "U-AIR (FDU10A)";
        default:     return NULL;
    }
}

/* Read a one-line sysfs attribute, newline-trimmed. Returns 0 on success. */
static int read_sysfs_attr(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char *ok = fgets(buf, (int)len, f);
    fclose(f);
    if (!ok) return -1;
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

/* Find the USB product-id of the attached SecuGen reader by scanning sysfs.
 * If sn is non-NULL, prefer the device whose serial matches; else (or on no
 * match) return the first vendor-1162 device. Returns 0 if none found. */
static unsigned find_secugen_usb_pid(const char *sn)
{
    DIR *d = opendir("/sys/bus/usb/devices");
    if (!d) return 0;

    struct dirent *e;
    unsigned first = 0, matched = 0;
    char path[512], v[32], p[32], s[80];
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idVendor", e->d_name);
        if (read_sysfs_attr(path, v, sizeof v) != 0) continue;
        if ((unsigned)strtoul(v, NULL, 16) != SECUGEN_VID) continue;

        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idProduct", e->d_name);
        if (read_sysfs_attr(path, p, sizeof p) != 0) continue;
        unsigned pid = (unsigned)strtoul(p, NULL, 16);
        if (!first) first = pid;

        if (sn && *sn) {
            snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/serial", e->d_name);
            if (read_sysfs_attr(path, s, sizeof s) == 0) {
                rstrip(s);
                if (strcmp(s, sn) == 0) { matched = pid; break; }
            }
        }
    }
    closedir(d);
    return matched ? matched : first;
}

/* Capabilities enumeration — model-agnostic. Init AUTO, enumerate every
 * attached SecuGen device, then per device report info + Smart Capture support
 * + a recommended config (empirical; only FDU05/U20 validated so far). This is
 * the seed for an installer that auto-tunes per model. */
static int cmd_caps(int json)
{
    HSGFPM h = NULL;
    DWORD err = SGFPM_Create(&h);
    if (err != SGFDX_ERROR_NONE) {
        fprintf(stderr, "SGFPM_Create failed: %lu (%s)\n", err, err_name(err));
        return 1;
    }
    err = SGFPM_Init(h, SG_DEV_AUTO);   /* AUTO so any model is detected */
    if (err != SGFDX_ERROR_NONE) {
        fprintf(stderr, "SGFPM_Init(SG_DEV_AUTO) failed: %lu (%s)\n", err, err_name(err));
        SGFPM_Terminate(h);
        return 1;
    }

    DWORD ndevs = 0;
    SGDeviceList *list = NULL;
    err = SGFPM_EnumerateDevice(h, &ndevs, &list);
    if (err != SGFDX_ERROR_NONE) {
        fprintf(stderr, "SGFPM_EnumerateDevice failed: %lu (%s)\n", err, err_name(err));
        SGFPM_Terminate(h);
        return 1;
    }

    if (json) {
        printf("[");
    } else {
        printf("Enumerated %lu SecuGen device(s):\n", (unsigned long)ndevs);
        for (DWORD i = 0; i < ndevs; i++) {
            char sn[sizeof(list[i].DevSN) + 1];
            snprintf(sn, sizeof sn, "%s", (const char *)list[i].DevSN);
            rstrip(sn);
            unsigned pid = find_secugen_usb_pid(sn);
            const char *m = pid ? usb_pid_model(pid) : NULL;
            printf("  [%lu] model=%-18s usb=%04x:%04x DevID=%lu SN=%s\n",
                   (unsigned long)i, m ? m : "unknown", SECUGEN_VID, pid,
                   (unsigned long)list[i].DevID, sn);
        }
        if (ndevs == 0)
            printf("  (none — is a reader plugged in and accessible?)\n");
        printf("\n");
    }

    int emitted = 0;
    for (DWORD i = 0; i < ndevs; i++) {
        err = SGFPM_OpenDevice(h, i);
        if (err != SGFDX_ERROR_NONE) {
            if (!json)
                printf("=== Device %lu ===\n  OpenDevice(%lu) failed: %lu (%s)\n\n",
                       (unsigned long)i, (unsigned long)i, err, err_name(err));
            continue;
        }

        SGDeviceInfoParam di = read_info(h);
        DWORD maxt = 0;
        SGFPM_GetMaxTemplateSize(h, &maxt);
        DWORD werr = SGFPM_WriteData(h, 5, 1);   /* Smart Capture support probe */

        char sn[sizeof(di.DeviceSN) + 1];
        snprintf(sn, sizeof sn, "%s", (const char *)di.DeviceSN);
        rstrip(sn);

        /* Identify the model by USB VID:PID (canonical), matched by serial —
         * the SDK's DevName returns UNKNOWN for the U20. */
        unsigned pid = find_secugen_usb_pid(sn);
        const char *usbmodel = pid ? usb_pid_model(pid) : NULL;
        int validated = (pid == 0x2200);   /* U20 (FDU05) */

        if (json) {
            printf("%s\n  {\n", emitted ? "," : "");
            printf("    \"index\": %lu,\n", (unsigned long)i);
            printf("    \"serial\": \"%s\",\n", sn);
            printf("    \"usb_vid\": \"%04x\",\n", SECUGEN_VID);
            printf("    \"usb_pid\": \"%04x\",\n", pid);
            printf("    \"model\": \"%s\",\n", usbmodel ? usbmodel : "unknown");
            printf("    \"image_width\": %lu,\n", (unsigned long)di.ImageWidth);
            printf("    \"image_height\": %lu,\n", (unsigned long)di.ImageHeight);
            printf("    \"dpi\": %lu,\n", (unsigned long)di.ImageDPI);
            printf("    \"brightness\": %lu,\n", (unsigned long)di.Brightness);
            printf("    \"contrast\": %lu,\n", (unsigned long)di.Contrast);
            printf("    \"gain\": %lu,\n", (unsigned long)di.Gain);
            printf("    \"fw_version\": %lu,\n", (unsigned long)di.FWVersion);
            printf("    \"max_template_size\": %lu,\n", (unsigned long)maxt);
            printf("    \"smart_capture_supported\": %s,\n",
                   werr == SGFDX_ERROR_NONE ? "true" : "false");
            printf("    \"validated\": %s,\n", validated ? "true" : "false");
            printf("    \"recommended\": { \"smart_capture\": true, "
                   "\"brightness_floor\": %lu, \"retries\": %d }\n",
                   (unsigned long)di.Brightness, validated ? 4 : 3);
            printf("  }");
        } else {
            printf("=== Device %lu: %s ===\n", (unsigned long)i,
                   usbmodel ? usbmodel : "unknown model");
            print_info(&di, "  info");
            printf("  max_template_size=%lu\n", (unsigned long)maxt);
            printf("  Smart Capture (WriteData(5,1)): rc=%lu (%s)%s\n",
                   werr, err_name(werr),
                   werr == SGFDX_ERROR_NONE ? "" : "  <-- not supported on this model");
            printf("  USB id: %04x:%04x -> %s\n", SECUGEN_VID, pid,
                   usbmodel ? usbmodel : (pid ? "unknown SecuGen PID" : "no USB match"));
            printf("  (SDK DevName=0x%02lX %s [unreliable])\n",
                   (unsigned long)list[i].DevName, model_name(list[i].DevName));
            if (validated)
                printf("  recommended: smart_capture=on, brightness floor=readout(%lu), "
                       "retries=4 [VALIDATED]\n", (unsigned long)di.Brightness);
            else
                printf("  recommended: smart_capture=on, retries>=3 "
                       "[UNVALIDATED model — run capture/qsweep to characterize]\n");
            printf("\n");
        }

        emitted++;
        SGFPM_CloseDevice(h);
    }

    if (json)
        printf("%s]\n", emitted ? "\n" : "");

    SGFPM_Terminate(h);
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: sudo %s <command>\n"
        "  info                    one device-info snapshot\n"
        "  sweep [start end step]  set brightness across a range, report read-back (default 0 100 10)\n"
        "  ladder [retries]        reproduce pam_sgfp's adaptive ladder from the live floor (default 3)\n"
        "  capture [brightness] [smart]  optionally set brightness and Smart Capture (0/1),\n"
        "                          capture one image, print quality (-1 = leave default)\n"
        "  watch [count] [ms]      poll device info to spot autonomous drift (default 10 @ 500ms)\n"
        "  qsweep [start end step] Smart Capture ON, sweep brightness over ONE held finger,\n"
        "                          print quality at each level (default 20 100 20)\n"
        "  caps [json]             enumerate all attached readers (model via USB PID, SN),\n"
        "                          per-device info + Smart Capture + recommended config;\n"
        "                          'json' emits a machine-readable array for the installer\n",
        argv0);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    /* caps is model-agnostic: it inits SG_DEV_AUTO and enumerates, so it does
     * not use the FDU05 single-device open path the other commands share. */
    if (strcmp(argv[1], "caps") == 0)
        return cmd_caps(argc > 2 && strcmp(argv[2], "json") == 0);

    HSGFPM h = open_device();
    int rc = 1;

    if (strcmp(argv[1], "info") == 0) {
        rc = cmd_info(h);
    } else if (strcmp(argv[1], "sweep") == 0) {
        int start = (argc > 2) ? atoi(argv[2]) : 0;
        int end   = (argc > 3) ? atoi(argv[3]) : 100;
        int step  = (argc > 4) ? atoi(argv[4]) : 10;
        rc = cmd_sweep(h, start, end, step);
    } else if (strcmp(argv[1], "ladder") == 0) {
        int retries = (argc > 2) ? atoi(argv[2]) : 3;
        rc = cmd_ladder(h, retries);
    } else if (strcmp(argv[1], "capture") == 0) {
        int brightness = (argc > 2) ? atoi(argv[2]) : -1;
        int smart      = (argc > 3) ? atoi(argv[3]) : -1;
        rc = cmd_capture(h, brightness, smart);
    } else if (strcmp(argv[1], "watch") == 0) {
        int count    = (argc > 2) ? atoi(argv[2]) : 10;
        int delay_ms = (argc > 3) ? atoi(argv[3]) : 500;
        rc = cmd_watch(h, count, delay_ms);
    } else if (strcmp(argv[1], "qsweep") == 0) {
        int start = (argc > 2) ? atoi(argv[2]) : 20;
        int end   = (argc > 3) ? atoi(argv[3]) : 100;
        int step  = (argc > 4) ? atoi(argv[4]) : 20;
        rc = cmd_qsweep(h, start, end, step);
    } else {
        usage(argv[0]);
    }

    close_device(h);
    return rc;
}
