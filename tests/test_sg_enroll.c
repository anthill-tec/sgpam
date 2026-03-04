/*
 * test_sg_enroll.c — Tests for sg_enroll enrollment flow
 *
 * sg_enroll's main() is renamed to sg_enroll_main via -Dmain=sg_enroll_main
 * to avoid collision with Criterion's main. Additional wraps for geteuid
 * (pretend root) and sleep (no-op for speed).
 *
 * Note: isatty(STDIN_FILENO) returns false in the test harness, so
 * interactive prompts (finger selection, confidence re-capture,
 * verification) are skipped — tests exercise the non-interactive paths.
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

/* Pull in sg_enroll.c — main is renamed to sg_enroll_main via -Dmain */
#include "../sg_enroll.c"

/* ── Extra wraps for sg_enroll ────────────────────────────── */

uid_t __wrap_geteuid(void)
{
    return 0;  /* pretend root */
}

unsigned int __wrap_sleep(unsigned int seconds)
{
    (void)seconds;
    return 0;  /* no-op for speed */
}

/* ── Helpers ──────────────────────────────────────────────── */

static void setup(void)
{
    mock_state_reset();
    snprintf(test_template_dir, sizeof(test_template_dir),
             "/tmp/sgpam_enroll_test_%d", getpid());
    mkdir(test_template_dir, 0700);
}

static void teardown(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_template_dir);
    system(cmd);
}

/* ── Existing tests (updated for new arg format) ─────────── */

Test(sg_enroll, missing_args, .init = setup, .fini = teardown)
{
    char *argv[] = {"sg_enroll", NULL};
    int rc = sg_enroll_main(1, argv);
    cr_assert_eq(rc, 1, "should return 1 for missing args");
}

Test(sg_enroll, invalid_username, .init = setup, .fini = teardown)
{
    char *argv[] = {"sg_enroll", "user/evil", "right-index", NULL};
    int rc = sg_enroll_main(3, argv);
    cr_assert_eq(rc, 1, "should return 1 for invalid username");
}

Test(sg_enroll, successful_enrollment, .init = setup, .fini = teardown)
{
    g_mock.match_result = TRUE;
    g_mock.matching_score = 150;
    g_mock.template_size = 400;

    char *argv[] = {"sg_enroll", "gooduser", "right-index", NULL};
    int rc = sg_enroll_main(3, argv);
    cr_assert_eq(rc, 0, "expected success, got %d", rc);

    /* Verify the template file was created with new naming */
    char path[512];
    snprintf(path, sizeof(path), "%s/gooduser_right-index.tpl", test_template_dir);
    FILE *f = fopen(path, "rb");
    cr_assert_not_null(f, "template file should exist");
    fclose(f);
}

Test(sg_enroll, sdk_create_failure, .init = setup, .fini = teardown,
     .exit_code = 1)
{
    g_mock.create_rv = SGFDX_ERROR_CREATION_FAILED;

    char *argv[] = {"sg_enroll", "testuser", "right-index", NULL};
    sg_enroll_main(3, argv);
    /* die() calls exit(1) */
}

Test(sg_enroll, samples_dont_match, .init = setup, .fini = teardown)
{
    g_mock.match_result = FALSE;

    char *argv[] = {"sg_enroll", "testuser", "right-index", NULL};
    int rc = sg_enroll_main(3, argv);
    cr_assert_neq(rc, 0, "should fail when samples don't match");
}

Test(sg_enroll, capture_fails_after_retries, .init = setup, .fini = teardown)
{
    /* All capture attempts fail */
    g_mock.get_image_ex_rv = SGFDX_ERROR_TIME_OUT;

    char *argv[] = {"sg_enroll", "testuser", "right-index", NULL};
    int rc = sg_enroll_main(3, argv);
    cr_assert_neq(rc, 0, "should fail after 3 capture retries");
}

/* ── New multi-finger tests ──────────────────────────────── */

Test(sg_enroll, enrollment_with_finger_name, .init = setup, .fini = teardown)
{
    g_mock.match_result = TRUE;
    g_mock.matching_score = 150;
    g_mock.template_size = 400;

    char *argv[] = {"sg_enroll", "alice", "left-thumb", NULL};
    int rc = sg_enroll_main(3, argv);
    cr_assert_eq(rc, 0, "expected success, got %d", rc);

    /* Verify correct filename */
    char path[512];
    snprintf(path, sizeof(path), "%s/alice_left-thumb.tpl", test_template_dir);
    cr_assert_eq(access(path, F_OK), 0, "alice_left-thumb.tpl should exist");
}

Test(sg_enroll, invalid_finger_name, .init = setup, .fini = teardown)
{
    char *argv[] = {"sg_enroll", "bob", "pinky", NULL};
    int rc = sg_enroll_main(3, argv);
    cr_assert_eq(rc, 1, "should reject invalid finger name");
}

Test(sg_enroll, list_mode_no_enrollments, .init = setup, .fini = teardown)
{
    char *argv[] = {"sg_enroll", "--list", "nobody", NULL};
    int rc = sg_enroll_main(3, argv);
    cr_assert_eq(rc, 0, "list should return 0 even with no enrollments");
}

Test(sg_enroll, list_mode_with_enrollments, .init = setup, .fini = teardown)
{
    /* Pre-create template files */
    char path[512];
    snprintf(path, sizeof(path), "%s/bob_right-index.tpl", test_template_dir);
    FILE *f = fopen(path, "wb");
    BYTE data[400] = {0};
    fwrite(data, 1, 400, f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/bob_left-thumb.tpl", test_template_dir);
    f = fopen(path, "wb");
    fwrite(data, 1, 400, f);
    fclose(f);

    char *argv[] = {"sg_enroll", "--list", "bob", NULL};
    int rc = sg_enroll_main(3, argv);
    cr_assert_eq(rc, 0, "list should return 0");
}

Test(sg_enroll, security_level_option, .init = setup, .fini = teardown)
{
    g_mock.match_result = TRUE;
    g_mock.matching_score = 150;
    g_mock.template_size = 400;

    char *argv[] = {"sg_enroll", "-s", "high", "carol", "right-index", NULL};
    int rc = sg_enroll_main(5, argv);
    cr_assert_eq(rc, 0, "expected success with -s high, got %d", rc);

    char path[512];
    snprintf(path, sizeof(path), "%s/carol_right-index.tpl", test_template_dir);
    cr_assert_eq(access(path, F_OK), 0, "template should exist");
}

Test(sg_enroll, invalid_security_level, .init = setup, .fini = teardown)
{
    char *argv[] = {"sg_enroll", "-s", "mega", "dave", "right-index", NULL};
    int rc = sg_enroll_main(5, argv);
    cr_assert_eq(rc, 1, "should reject invalid security level");
}
