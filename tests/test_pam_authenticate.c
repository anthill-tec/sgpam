/*
 * test_pam_authenticate.c — Tests for pam_sm_authenticate()
 *
 * Full PAM authentication flow tests using --wrap mocked SDK/PAM calls.
 * Includes pam_sgfp.c directly; mock state controls all SDK/PAM behavior.
 */

#include <stddef.h>
#include <string.h>
#include "sgfplib.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Use a temp directory for template files */
static char test_template_dir[256];
#define TEMPLATE_DIR test_template_dir

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mock_state.h"

/* Pull in the full pam_sgfp.c source */
#include "../pam_sgfp.c"

/* ── Helpers ──────────────────────────────────────────────── */

static void setup(void)
{
    mock_state_reset();
    snprintf(test_template_dir, sizeof(test_template_dir),
             "/tmp/sgpam_auth_test_%d", getpid());
    mkdir(test_template_dir, 0700);
}

static void teardown(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_template_dir);
    system(cmd);
}

/* Write a fake template file for the given username (legacy format) */
static void write_template(const char *username, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.tpl", test_template_dir, username);
    FILE *f = fopen(path, "wb");
    BYTE *data = calloc(1, size);
    fwrite(data, 1, size, f);
    free(data);
    fclose(f);
}

/* Write a finger-specific template file */
static void write_finger_template(const char *username, const char *finger,
                                   size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_%s.tpl", test_template_dir,
             username, finger);
    FILE *f = fopen(path, "wb");
    BYTE *data = calloc(1, size);
    fwrite(data, 1, size, f);
    free(data);
    fclose(f);
}

/* ── Happy path ───────────────────────────────────────────── */

Test(pam_authenticate, success_match, .init = setup, .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = TRUE;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS, "expected PAM_SUCCESS, got %d", rc);
}

Test(pam_authenticate, success_cleanup_runs, .init = setup, .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = TRUE;

    pam_sm_authenticate(NULL, 0, 0, NULL);

    cr_assert_eq(g_mock.close_device_count, 1, "device should be closed");
    cr_assert_eq(g_mock.terminate_count, 1, "SDK should be terminated");
}

/* ── PAM failures ─────────────────────────────────────────── */

Test(pam_authenticate, get_user_fails, .init = setup, .fini = teardown)
{
    g_mock.pam_get_user_rv = PAM_AUTH_ERR;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

Test(pam_authenticate, invalid_username, .init = setup, .fini = teardown)
{
    g_mock.pam_username = "user/evil";

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

Test(pam_authenticate, no_template_returns_user_unknown, .init = setup,
     .fini = teardown)
{
    /* No template file written — user not enrolled */
    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_USER_UNKNOWN);
}

/* ── SDK failures ─────────────────────────────────────────── */

Test(pam_authenticate, create_fails, .init = setup, .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.create_rv = SGFDX_ERROR_CREATION_FAILED;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

Test(pam_authenticate, init_fails, .init = setup, .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.init_rv = SGFDX_ERROR_INITIALIZE_FAILED;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

Test(pam_authenticate, open_device_fails, .init = setup, .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.open_device_rv = SGFDX_ERROR_DEVICE_NOT_FOUND;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.close_device_count, 0,
                 "CloseDevice must NOT be called if device never opened");
}

Test(pam_authenticate, get_device_info_fails, .init = setup, .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.get_device_info_rv = SGFDX_ERROR_FUNCTION_FAILED;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

Test(pam_authenticate, capture_timeout, .init = setup, .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.get_image_ex_rv = SGFDX_ERROR_TIME_OUT;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

Test(pam_authenticate, template_extraction_fails, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.create_template_rv = SGFDX_ERROR_EXTRACT_FAIL;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

/* ── Match failures ───────────────────────────────────────── */

Test(pam_authenticate, no_match, .init = setup, .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = FALSE;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

/* ── Edge cases ───────────────────────────────────────────── */

Test(pam_authenticate, zero_dimensions_rejected, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.devinfo_width  = 0;
    g_mock.devinfo_height = 0;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

Test(pam_authenticate, oversized_dimensions_rejected, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.devinfo_width  = 8192;
    g_mock.devinfo_height = 8192;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
}

/* ── Multi-template tests ────────────────────────────────── */

Test(pam_authenticate, multi_template_first_matches, .init = setup,
     .fini = teardown)
{
    write_finger_template("testuser", "right-index", 400);
    write_finger_template("testuser", "left-thumb", 400);
    g_mock.match_result = TRUE;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.match_template_count, 1,
                 "should stop after first match");
}

Test(pam_authenticate, multi_template_second_matches, .init = setup,
     .fini = teardown)
{
    write_finger_template("testuser", "right-index", 400);
    write_finger_template("testuser", "left-thumb", 400);

    /* First template doesn't match, second does */
    BOOL results[] = {FALSE, TRUE};
    g_mock.match_results = results;
    g_mock.match_results_len = 2;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.match_template_count, 2,
                 "should try both templates");
}

Test(pam_authenticate, multi_template_none_match, .init = setup,
     .fini = teardown)
{
    write_finger_template("testuser", "right-index", 400);
    write_finger_template("testuser", "left-thumb", 400);
    g_mock.match_result = FALSE;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.match_template_count, 2 * 3,
                 "should try both templates on each of the 3 attempts");
}

/* ── Brightness module argument ───────────────────────────── */

Test(pam_authenticate, brightness_arg_applied, .init = setup, .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = TRUE;

    const char *args[] = {"brightness=70"};
    int rc = pam_sm_authenticate(NULL, 0, 1, args);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.set_brightness_count, 1,
                 "SetBrightness should be called once");
    cr_assert_eq(g_mock.last_brightness, 70,
                 "brightness should be 70, got %lu", g_mock.last_brightness);
}

Test(pam_authenticate, no_brightness_arg_skips_setbrightness, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = TRUE;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.set_brightness_count, 0,
                 "SetBrightness must not be called without brightness= arg");
}

Test(pam_authenticate, out_of_range_brightness_ignored, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = TRUE;

    const char *args[] = {"brightness=200"};
    int rc = pam_sm_authenticate(NULL, 0, 1, args);
    cr_assert_eq(rc, PAM_SUCCESS, "auth should still succeed");
    cr_assert_eq(g_mock.set_brightness_count, 0,
                 "out-of-range brightness must be ignored");
}

/* ── Quiet multi-sample retry (default 3 attempts) ────────── */

Test(pam_authenticate, success_first_attempt_no_extra_captures, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = TRUE;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.get_image_ex_count, 1,
                 "must stop on first valid match, got %d captures",
                 g_mock.get_image_ex_count);
}

Test(pam_authenticate, retry_succeeds_on_later_sample, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    /* First sample's match fails, second sample matches */
    BOOL results[] = {FALSE, TRUE};
    g_mock.match_results = results;
    g_mock.match_results_len = 2;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS, "should accept on the second silent sample");
    cr_assert_eq(g_mock.get_image_ex_count, 2,
                 "should have re-captured once, got %d captures",
                 g_mock.get_image_ex_count);
}

Test(pam_authenticate, rejects_only_after_all_attempts, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = FALSE;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.get_image_ex_count, 3,
                 "default should sample 3 times before rejecting, got %d",
                 g_mock.get_image_ex_count);
}

Test(pam_authenticate, capture_timeout_retries_before_reject, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.get_image_ex_rv = SGFDX_ERROR_TIME_OUT;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.get_image_ex_count, 3,
                 "capture timeout should retry up to 3 times, got %d",
                 g_mock.get_image_ex_count);
}

Test(pam_authenticate, retries_arg_limits_attempts, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = FALSE;

    const char *args[] = {"retries=2"};
    int rc = pam_sm_authenticate(NULL, 0, 1, args);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.get_image_ex_count, 2,
                 "retries=2 should sample exactly twice, got %d",
                 g_mock.get_image_ex_count);
}

/* ── Brightness escalation lock-step with retries ─────────── */

Test(pam_authenticate, brightness_escalates_each_failed_attempt, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = FALSE;          /* every attempt fails */

    const char *args[] = {"brightness=40", "brightness_step=20"};
    int rc = pam_sm_authenticate(NULL, 0, 2, args);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.set_brightness_count, 3,
                 "brightness should be set once per attempt, got %d",
                 g_mock.set_brightness_count);
    cr_assert_eq(g_mock.last_brightness, 80,
                 "3 attempts from 40 step 20 -> 40,60,80; last should be 80, got %lu",
                 g_mock.last_brightness);
}

Test(pam_authenticate, brightness_escalation_caps_at_100, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = FALSE;

    const char *args[] = {"brightness=80", "brightness_step=30"};
    int rc = pam_sm_authenticate(NULL, 0, 2, args);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.last_brightness, 100,
                 "80,110->100,140->100; should cap at 100, got %lu",
                 g_mock.last_brightness);
}

Test(pam_authenticate, brightness_escalates_from_default_base, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = FALSE;

    /* Explicit fixed step overrides the adaptive ladder. No base, readout 0
       -> floor 50. Fixed step 20 over 3 attempts gives rungs 50,70,90; attempt
       1 keeps the sensor default, so attempts 2 (70) and 3 (90) are set. */
    const char *args[] = {"brightness_step=20"};
    int rc = pam_sm_authenticate(NULL, 0, 1, args);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.set_brightness_count, 2,
                 "first attempt keeps sensor default; only 2 later sets, got %d",
                 g_mock.set_brightness_count);
    cr_assert_eq(g_mock.last_brightness, 90,
                 "fixed step 20 from floor 50: 50,70,90; last set should be 90, got %lu",
                 g_mock.last_brightness);
}

Test(pam_authenticate, adaptive_ladder_starts_at_sensor_readout, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.devinfo_brightness = 70;       /* sensor reports 70 -> ladder floor */
    BOOL results[] = {FALSE, TRUE};       /* match on 2nd attempt */
    g_mock.match_results = results;
    g_mock.match_results_len = 2;

    /* retries=4, adaptive: floor 70 -> 100 over 4 rungs = 70,80,90,100.
       Attempt 1 keeps the sensor default (70); attempt 2 sets 80 and matches.
       last==80 proves the floor is the readout (70), not the 50 fallback. */
    const char *args[] = {"retries=4"};
    int rc = pam_sm_authenticate(NULL, 0, 1, args);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.set_brightness_count, 1,
                 "matched on attempt 2, so exactly one set, got %d",
                 g_mock.set_brightness_count);
    cr_assert_eq(g_mock.last_brightness, 80,
                 "floor 70 step (100-70)/3=10 -> attempt 2 = 80, got %lu",
                 g_mock.last_brightness);
}

Test(pam_authenticate, explicit_brightness_overrides_readout_floor, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.devinfo_brightness = 70;       /* readout would be 70 ... */
    BOOL results[] = {FALSE, TRUE};
    g_mock.match_results = results;
    g_mock.match_results_len = 2;

    /* brightness=40 overrides the readout as the floor. Adaptive over 3
       attempts: 40,70,100. With an explicit base, attempt 1 is set (40),
       attempt 2 (70) matches. last==70 proves floor=40, not the readout 70. */
    const char *args[] = {"brightness=40"};
    int rc = pam_sm_authenticate(NULL, 0, 1, args);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.set_brightness_count, 2,
                 "explicit base sets attempt 1 too, got %d",
                 g_mock.set_brightness_count);
    cr_assert_eq(g_mock.last_brightness, 70,
                 "floor 40 over 3 rungs: 40,70,100; matched at 70, got %lu",
                 g_mock.last_brightness);
}

Test(pam_authenticate, brightness_step_zero_disables_escalation, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.devinfo_brightness = 70;
    g_mock.match_result = FALSE;          /* all attempts fail */

    /* brightness_step=0 with no explicit base: never touch brightness */
    const char *args[] = {"brightness_step=0"};
    int rc = pam_sm_authenticate(NULL, 0, 1, args);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.set_brightness_count, 0,
                 "step 0 must disable escalation entirely, got %d",
                 g_mock.set_brightness_count);
}

Test(pam_authenticate, default_escalates_on_failure_only, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = FALSE;          /* all attempts fail */

    /* No args: adaptive ladder scales floor->100 across retries. Sensor
       readout is 0 here (mock default) so floor falls back to 50; with 3
       attempts the rungs are 50,75,100. Attempt 1 keeps the sensor default,
       so only attempts 2 (75) and 3 (100) are set. */
    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.set_brightness_count, 2,
                 "first attempt untouched; 2 later attempts set, got %d",
                 g_mock.set_brightness_count);
    cr_assert_eq(g_mock.last_brightness, 100,
                 "adaptive ladder must reach 100 on the last attempt, got %lu",
                 g_mock.last_brightness);
}

Test(pam_authenticate, brightness_escalation_stops_on_match, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    BOOL results[] = {FALSE, TRUE};       /* match on 2nd attempt */
    g_mock.match_results = results;
    g_mock.match_results_len = 2;

    const char *args[] = {"brightness=40", "brightness_step=20"};
    int rc = pam_sm_authenticate(NULL, 0, 2, args);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.set_brightness_count, 2,
                 "should stop escalating after the matching attempt, got %d",
                 g_mock.set_brightness_count);
    cr_assert_eq(g_mock.last_brightness, 60,
                 "matched on attempt 2 at brightness 60, got %lu",
                 g_mock.last_brightness);
}

Test(pam_authenticate, legacy_template_still_works, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = TRUE;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS);
}

Test(pam_authenticate, mixed_legacy_and_finger, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    write_finger_template("testuser", "right-index", 400);

    /* Legacy doesn't match (or finger-specific comes first), second matches */
    BOOL results[] = {FALSE, TRUE};
    g_mock.match_results = results;
    g_mock.match_results_len = 2;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS);
}
