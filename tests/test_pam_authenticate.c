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

/* ── Brightness floor comes from the device, not an argument ── */

Test(pam_authenticate, first_attempt_keeps_sensor_default, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = TRUE;

    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.set_brightness_count, 0,
                 "first attempt must keep the sensor's own exposure (no set)");
}

Test(pam_authenticate, brightness_arg_no_longer_recognized, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.match_result = TRUE;

    /* brightness=N was removed: the floor is taken from the device readout, so
       the argument is unknown and must not cause an attempt-1 SetBrightness. */
    const char *args[] = {"brightness=70"};
    int rc = pam_sm_authenticate(NULL, 0, 1, args);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.set_brightness_count, 0,
                 "brightness= must be ignored; first attempt stays at sensor default");
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

Test(pam_authenticate, brightness_step_arg_no_longer_recognized, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.devinfo_brightness = 40;       /* readout -> escalation floor 40 */
    g_mock.match_result = FALSE;          /* every attempt fails */

    /* brightness_step was removed: escalation is always the adaptive ladder
       derived from retries, so this arg is unknown and ignored. Floor 40 over
       the default 3 attempts scales 40,70,100 (attempt 1 untouched) — NOT the
       40,60,80 a fixed step of 20 would have produced. */
    const char *args[] = {"brightness_step=20"};
    int rc = pam_sm_authenticate(NULL, 0, 1, args);
    cr_assert_eq(rc, PAM_AUTH_ERR);
    cr_assert_eq(g_mock.set_brightness_count, 2,
                 "first attempt untouched; 2 later attempts set, got %d",
                 g_mock.set_brightness_count);
    cr_assert_eq(g_mock.last_brightness, 100,
                 "adaptive ladder ignores the step arg and reaches 100, got %lu",
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

Test(pam_authenticate, escalation_stops_on_match, .init = setup,
     .fini = teardown)
{
    write_template("testuser", 400);
    g_mock.devinfo_brightness = 40;       /* readout -> floor 40 */
    BOOL results[] = {FALSE, TRUE};       /* match on 2nd attempt */
    g_mock.match_results = results;
    g_mock.match_results_len = 2;

    /* Adaptive ladder from floor 40 over the default 3 attempts: 40,70,100.
       Attempt 1 keeps the sensor default (40) and fails; attempt 2 sets 70 and
       matches, so escalation stops there (the 100 rung is never reached). */
    int rc = pam_sm_authenticate(NULL, 0, 0, NULL);
    cr_assert_eq(rc, PAM_SUCCESS);
    cr_assert_eq(g_mock.set_brightness_count, 1,
                 "only the matching attempt 2 sets brightness, got %d",
                 g_mock.set_brightness_count);
    cr_assert_eq(g_mock.last_brightness, 70,
                 "matched on attempt 2 at brightness 70, got %lu",
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
