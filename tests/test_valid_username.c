/*
 * test_valid_username.c — Unit tests for valid_username()
 *
 * Includes pam_sgfp.c directly to access the static function.
 * No SDK/PAM linking needed — pure logic tests.
 */

/* Override TEMPLATE_DIR before including the source */
#define TEMPLATE_DIR "/tmp/sgpam_test_unused"

/*
 * Include SDK header BEFORE Criterion to avoid the stdbool.h conflict:
 * sgfplib.h has "typedef BOOL bool;" which breaks if stdbool.h's
 * "#define bool _Bool" is already active. The include guard prevents
 * re-inclusion when pam_sgfp.c includes it again.
 *
 * stddef.h provides wchar_t needed by sgfplib.h.
 */
#include <stddef.h>
#include "sgfplib.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#include <criterion/criterion.h>
#include <string.h>

/* Pull in the static functions from pam_sgfp.c */
#include "../pam_sgfp.c"

/* ── Accepts ──────────────────────────────────────────────── */

Test(valid_username, accepts_alpha)
{
    cr_assert(valid_username("alice"));
    cr_assert(valid_username("BOB"));
    cr_assert(valid_username("Charlie"));
}

Test(valid_username, accepts_alphanumeric)
{
    cr_assert(valid_username("user42"));
    cr_assert(valid_username("007james"));
}

Test(valid_username, accepts_underscore_hyphen_dot)
{
    cr_assert(valid_username("first_last"));
    cr_assert(valid_username("first-last"));
    cr_assert(valid_username("first.last"));
    cr_assert(valid_username("a_b-c.d"));
}

Test(valid_username, accepts_single_char)
{
    cr_assert(valid_username("a"));
    cr_assert(valid_username("Z"));
    cr_assert(valid_username("5"));
}

Test(valid_username, accepts_max_minus_one_length)
{
    /* USERNAME_MAX is 256, so 255 chars should be accepted */
    char name[256];
    memset(name, 'a', 255);
    name[255] = '\0';
    cr_assert(valid_username(name));
}

/* ── Rejects ──────────────────────────────────────────────── */

Test(valid_username, rejects_null)
{
    cr_assert_not(valid_username(NULL));
}

Test(valid_username, rejects_empty)
{
    cr_assert_not(valid_username(""));
}

Test(valid_username, rejects_path_traversal)
{
    /* "../etc/shadow" and "user/../root" rejected due to '/' */
    cr_assert_not(valid_username("../etc/shadow"));
    cr_assert_not(valid_username("user/../root"));
}

Test(valid_username, accepts_dots_only)
{
    /* ".." is valid chars (dots) — path traversal is blocked by the '/'
       check and by load_template using a fixed directory prefix */
    cr_assert(valid_username(".."));
    cr_assert(valid_username("."));
    cr_assert(valid_username("..."));
}

Test(valid_username, rejects_slash)
{
    cr_assert_not(valid_username("user/name"));
    cr_assert_not(valid_username("/root"));
}

Test(valid_username, rejects_space)
{
    cr_assert_not(valid_username("user name"));
    cr_assert_not(valid_username(" leading"));
    cr_assert_not(valid_username("trailing "));
}

Test(valid_username, rejects_special_chars)
{
    cr_assert_not(valid_username("user@host"));
    cr_assert_not(valid_username("user;cmd"));
    cr_assert_not(valid_username("user$var"));
    cr_assert_not(valid_username("user`cmd`"));
    cr_assert_not(valid_username("user|pipe"));
    cr_assert_not(valid_username("user&bg"));
}

Test(valid_username, rejects_too_long)
{
    /* Exactly USERNAME_MAX (256) chars should be rejected */
    char name[257];
    memset(name, 'a', 256);
    name[256] = '\0';
    cr_assert_not(valid_username(name));
}
