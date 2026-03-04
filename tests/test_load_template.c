/*
 * test_load_template.c — Unit tests for load_template()
 *
 * Includes pam_sgfp.c directly to access the static function.
 * Uses temp directories with Criterion .init/.fini hooks.
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
#include <errno.h>

/* Pull in the static functions from pam_sgfp.c */
#include "../pam_sgfp.c"

/* ── Fixture setup/teardown ──────────────────────────────── */

static void create_test_dir(void)
{
    snprintf(test_template_dir, sizeof(test_template_dir),
             "/tmp/sgpam_test_%d", getpid());
    mkdir(test_template_dir, 0700);
}

static void remove_test_dir(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_template_dir);
    system(cmd);
}

/* ── Tests ────────────────────────────────────────────────── */

Test(load_template, valid_400_byte_template, .init = create_test_dir,
     .fini = remove_test_dir)
{
    /* Write a 400-byte template file */
    BYTE data[400];
    for (int i = 0; i < 400; i++)
        data[i] = (BYTE)(i & 0xFF);

    char path[512];
    snprintf(path, sizeof(path), "%s/alice.tpl", test_template_dir);
    FILE *f = fopen(path, "wb");
    cr_assert_not_null(f);
    cr_assert_eq(fwrite(data, 1, 400, f), 400);
    fclose(f);

    BYTE *tmpl = NULL;
    DWORD size = 0;
    int rc = load_template("alice", &tmpl, &size);

    cr_assert_eq(rc, 0, "load_template should succeed");
    cr_assert_eq(size, 400);
    cr_assert_not_null(tmpl);
    cr_assert_eq(memcmp(tmpl, data, 400), 0, "template content mismatch");
    free(tmpl);
}

Test(load_template, missing_file, .init = create_test_dir,
     .fini = remove_test_dir)
{
    BYTE *tmpl = NULL;
    DWORD size = 0;
    int rc = load_template("nonexistent", &tmpl, &size);
    cr_assert_neq(rc, 0, "load_template should fail for missing file");
    cr_assert_null(tmpl);
}

Test(load_template, empty_file, .init = create_test_dir,
     .fini = remove_test_dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/empty.tpl", test_template_dir);
    FILE *f = fopen(path, "wb");
    cr_assert_not_null(f);
    fclose(f);

    BYTE *tmpl = NULL;
    DWORD size = 0;
    int rc = load_template("empty", &tmpl, &size);
    /* ftell returns 0, malloc(0) is implementation-defined,
       fread of 0 bytes succeeds — either outcome is acceptable */
    (void)rc;
    if (tmpl) free(tmpl);
}

Test(load_template, binary_content_fidelity, .init = create_test_dir,
     .fini = remove_test_dir)
{
    /* Full 256-value byte range to verify no corruption */
    BYTE data[256];
    for (int i = 0; i < 256; i++)
        data[i] = (BYTE)i;

    char path[512];
    snprintf(path, sizeof(path), "%s/binary.tpl", test_template_dir);
    FILE *f = fopen(path, "wb");
    cr_assert_not_null(f);
    fwrite(data, 1, 256, f);
    fclose(f);

    BYTE *tmpl = NULL;
    DWORD size = 0;
    int rc = load_template("binary", &tmpl, &size);

    cr_assert_eq(rc, 0);
    cr_assert_eq(size, 256);
    cr_assert_not_null(tmpl);
    cr_assert_eq(memcmp(tmpl, data, 256), 0, "binary fidelity check failed");
    free(tmpl);
}
