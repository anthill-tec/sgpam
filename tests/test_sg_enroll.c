/*
 * test_sg_enroll.c — Tests for sg_enroll enrollment flow
 *
 * sg_enroll's main() is renamed to sg_enroll_main via -Dmain=sg_enroll_main
 * to avoid collision with Criterion's main. Additional wraps for geteuid
 * (pretend root) and sleep (no-op for speed).
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

/* ── Tests ────────────────────────────────────────────────── */

Test(sg_enroll, missing_args, .init = setup, .fini = teardown)
{
    char *argv[] = {"sg_enroll", NULL};
    int rc = sg_enroll_main(1, argv);
    cr_assert_eq(rc, 1, "should return 1 for missing args");
}

Test(sg_enroll, invalid_username, .init = setup, .fini = teardown)
{
    char *argv[] = {"sg_enroll", "user/evil", NULL};
    int rc = sg_enroll_main(2, argv);
    cr_assert_eq(rc, 1, "should return 1 for invalid username");
}

Test(sg_enroll, successful_enrollment, .init = setup, .fini = teardown)
{
    g_mock.match_result = TRUE;
    g_mock.matching_score = 150;
    g_mock.template_size = 400;

    char *argv[] = {"sg_enroll", "gooduser", NULL};
    int rc = sg_enroll_main(2, argv);
    cr_assert_eq(rc, 0, "expected success, got %d", rc);

    /* Verify the template file was created */
    char path[512];
    snprintf(path, sizeof(path), "%s/gooduser.tpl", test_template_dir);
    FILE *f = fopen(path, "rb");
    cr_assert_not_null(f, "template file should exist");
    fclose(f);
}

Test(sg_enroll, sdk_create_failure, .init = setup, .fini = teardown,
     .exit_code = 1)
{
    g_mock.create_rv = SGFDX_ERROR_CREATION_FAILED;

    char *argv[] = {"sg_enroll", "testuser", NULL};
    sg_enroll_main(2, argv);
    /* die() calls exit(1) */
}

Test(sg_enroll, samples_dont_match, .init = setup, .fini = teardown)
{
    g_mock.match_result = FALSE;

    char *argv[] = {"sg_enroll", "testuser", NULL};
    int rc = sg_enroll_main(2, argv);
    cr_assert_neq(rc, 0, "should fail when samples don't match");
}

Test(sg_enroll, capture_fails_after_retries, .init = setup, .fini = teardown)
{
    /* All capture attempts fail */
    g_mock.get_image_ex_rv = SGFDX_ERROR_TIME_OUT;

    char *argv[] = {"sg_enroll", "testuser", NULL};
    int rc = sg_enroll_main(2, argv);
    cr_assert_neq(rc, 0, "should fail after 3 capture retries");
}
