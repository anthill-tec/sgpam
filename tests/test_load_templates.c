/*
 * test_load_templates.c — Tests for load_templates() multi-template loading
 *
 * Includes pam_sgfp.c directly to access the static function.
 * Tests glob-based template discovery for multi-finger enrollment.
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

/* Override TEMPLATE_DIR to a per-test temp directory */
static char test_template_dir[256];
#define TEMPLATE_DIR test_template_dir

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mock_state.h"

/* Pull in the static functions from pam_sgfp.c */
#include "../pam_sgfp.c"

/* ── Fixture setup/teardown ──────────────────────────────── */

static void setup(void)
{
    mock_state_reset();
    snprintf(test_template_dir, sizeof(test_template_dir),
             "/tmp/sgpam_loadtmpls_test_%d", getpid());
    mkdir(test_template_dir, 0700);
}

static void teardown(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_template_dir);
    system(cmd);
}

/* Write a template file with predictable content */
static void write_named_template(const char *filename, BYTE fill, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_template_dir, filename);
    FILE *f = fopen(path, "wb");
    BYTE *data = malloc(size);
    memset(data, fill, size);
    fwrite(data, 1, size, f);
    free(data);
    fclose(f);
}

/* ── Tests ────────────────────────────────────────────────── */

Test(load_templates, finds_finger_specific_templates,
     .init = setup, .fini = teardown)
{
    write_named_template("alice_right-index.tpl", 0xAA, 400);
    write_named_template("alice_left-thumb.tpl", 0xBB, 400);

    BYTE **tmpls = NULL;
    DWORD *sizes = NULL;
    int count = 0;

    int rc = load_templates("alice", &tmpls, &sizes, &count, NULL);
    cr_assert_eq(rc, 0, "load_templates should succeed");
    cr_assert_eq(count, 2, "expected 2 templates, got %d", count);
    cr_assert_not_null(tmpls);
    cr_assert_not_null(sizes);

    for (int i = 0; i < count; i++) {
        cr_assert_eq(sizes[i], 400);
        cr_assert_not_null(tmpls[i]);
        free(tmpls[i]);
    }
    free(tmpls);
    free(sizes);
}

Test(load_templates, finds_legacy_template,
     .init = setup, .fini = teardown)
{
    write_named_template("bob.tpl", 0xCC, 400);

    BYTE **tmpls = NULL;
    DWORD *sizes = NULL;
    int count = 0;

    int rc = load_templates("bob", &tmpls, &sizes, &count, NULL);
    cr_assert_eq(rc, 0, "load_templates should find legacy template");
    cr_assert_eq(count, 1, "expected 1 template, got %d", count);
    cr_assert_eq(sizes[0], 400);

    /* Verify content */
    BYTE expected[400];
    memset(expected, 0xCC, 400);
    cr_assert_eq(memcmp(tmpls[0], expected, 400), 0);

    free(tmpls[0]);
    free(tmpls);
    free(sizes);
}

Test(load_templates, returns_error_for_no_templates,
     .init = setup, .fini = teardown)
{
    BYTE **tmpls = NULL;
    DWORD *sizes = NULL;
    int count = 0;

    int rc = load_templates("nobody", &tmpls, &sizes, &count, NULL);
    cr_assert_neq(rc, 0, "should fail when no templates found");
    cr_assert_eq(count, 0);
}

Test(load_templates, mixed_legacy_and_finger_templates,
     .init = setup, .fini = teardown)
{
    write_named_template("carol.tpl", 0xDD, 400);
    write_named_template("carol_right-index.tpl", 0xEE, 400);

    BYTE **tmpls = NULL;
    DWORD *sizes = NULL;
    int count = 0;

    int rc = load_templates("carol", &tmpls, &sizes, &count, NULL);
    cr_assert_eq(rc, 0);
    cr_assert_eq(count, 2, "expected 2 templates (legacy + finger), got %d", count);

    for (int i = 0; i < count; i++) {
        cr_assert_eq(sizes[i], 400);
        free(tmpls[i]);
    }
    free(tmpls);
    free(sizes);
}

Test(load_templates, binary_content_fidelity,
     .init = setup, .fini = teardown)
{
    /* Write a template with all 256 byte values */
    char path[512];
    snprintf(path, sizeof(path), "%s/dave_left-index.tpl", test_template_dir);
    FILE *f = fopen(path, "wb");
    BYTE data[256];
    for (int i = 0; i < 256; i++)
        data[i] = (BYTE)i;
    fwrite(data, 1, 256, f);
    fclose(f);

    BYTE **tmpls = NULL;
    DWORD *sizes = NULL;
    int count = 0;

    int rc = load_templates("dave", &tmpls, &sizes, &count, NULL);
    cr_assert_eq(rc, 0);
    cr_assert_eq(count, 1);
    cr_assert_eq(sizes[0], 256);
    cr_assert_eq(memcmp(tmpls[0], data, 256), 0, "binary fidelity check failed");

    free(tmpls[0]);
    free(tmpls);
    free(sizes);
}

Test(load_templates, prompt_includes_finger_names,
     .init = setup, .fini = teardown)
{
    write_named_template("eve_right-index.tpl", 0xAA, 400);
    write_named_template("eve_left-thumb.tpl", 0xBB, 400);

    BYTE **tmpls = NULL;
    DWORD *sizes = NULL;
    int count = 0;
    char *prompt = NULL;

    int rc = load_templates("eve", &tmpls, &sizes, &count, &prompt);
    cr_assert_eq(rc, 0);
    cr_assert_not_null(prompt, "prompt should be set");

    /* Prompt should contain both finger names */
    cr_assert(strstr(prompt, "right-index") != NULL,
              "prompt should contain 'right-index': %s", prompt);
    cr_assert(strstr(prompt, "left-thumb") != NULL,
              "prompt should contain 'left-thumb': %s", prompt);
    cr_assert(strstr(prompt, "Place finger on scanner") != NULL,
              "prompt should start with standard text: %s", prompt);

    for (int i = 0; i < count; i++)
        free(tmpls[i]);
    free(tmpls);
    free(sizes);
    free(prompt);
}

Test(load_templates, prompt_fallback_for_legacy_only,
     .init = setup, .fini = teardown)
{
    write_named_template("frank.tpl", 0xCC, 400);

    BYTE **tmpls = NULL;
    DWORD *sizes = NULL;
    int count = 0;
    char *prompt = NULL;

    int rc = load_templates("frank", &tmpls, &sizes, &count, &prompt);
    cr_assert_eq(rc, 0);
    cr_assert_not_null(prompt);

    /* Legacy-only: no finger names in parentheses */
    cr_assert_str_eq(prompt, "Place finger on scanner...");

    for (int i = 0; i < count; i++)
        free(tmpls[i]);
    free(tmpls);
    free(sizes);
    free(prompt);
}
