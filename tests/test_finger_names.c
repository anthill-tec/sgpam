/*
 * test_finger_names.c — Tests for finger name validation and security level parsing
 *
 * Pure logic tests — no SDK wraps needed, just links against Criterion.
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

#include <criterion/criterion.h>
#include "../sg_fingers.h"

/* ── Finger name validation ──────────────────────────────── */

Test(finger_names, table_has_ten_entries)
{
    cr_assert_eq(NUM_FINGERS, 10);
}

Test(finger_names, accepts_all_valid_names)
{
    const char *names[] = {
        "right-thumb", "right-index", "right-middle", "right-ring", "right-little",
        "left-thumb",  "left-index",  "left-middle",  "left-ring",  "left-little",
    };
    for (int i = 0; i < 10; i++) {
        cr_assert(valid_finger_name(names[i]),
                  "expected '%s' to be valid", names[i]);
    }
}

Test(finger_names, rejects_null)
{
    cr_assert_not(valid_finger_name(NULL));
}

Test(finger_names, rejects_empty)
{
    cr_assert_not(valid_finger_name(""));
}

Test(finger_names, rejects_unknown)
{
    cr_assert_not(valid_finger_name("pinky"));
    cr_assert_not(valid_finger_name("thumb"));
    cr_assert_not(valid_finger_name("right"));
}

Test(finger_names, rejects_wrong_separator)
{
    cr_assert_not(valid_finger_name("right_index"));
    cr_assert_not(valid_finger_name("right index"));
    cr_assert_not(valid_finger_name("rightindex"));
}

Test(finger_names, rejects_wrong_case)
{
    cr_assert_not(valid_finger_name("Right-Index"));
    cr_assert_not(valid_finger_name("RIGHT-INDEX"));
}

/* ── Security level parsing ──────────────────────────────── */

Test(security_level, parses_all_valid_levels)
{
    struct { const char *name; DWORD level; DWORD score; } cases[] = {
        {"none",         0,   0},
        {"lowest",       1,  30},
        {"lower",        2,  50},
        {"low",          3,  60},
        {"below_normal", 4,  70},
        {"normal",       5,  80},
        {"above_normal", 6,  90},
        {"high",         7, 100},
        {"higher",       8, 120},
        {"highest",      9, 140},
    };
    for (int i = 0; i < 10; i++) {
        DWORD level = 99, score = 99;
        int rc = parse_security_level(cases[i].name, &level, &score);
        cr_assert_eq(rc, 0, "parse_security_level('%s') should succeed", cases[i].name);
        cr_assert_eq(level, cases[i].level,
                     "'%s' level: expected %lu, got %lu", cases[i].name, cases[i].level, level);
        cr_assert_eq(score, cases[i].score,
                     "'%s' score: expected %lu, got %lu", cases[i].name, cases[i].score, score);
    }
}

Test(security_level, case_insensitive)
{
    DWORD level, score;
    cr_assert_eq(parse_security_level("NORMAL", &level, &score), 0);
    cr_assert_eq(level, 5);
    cr_assert_eq(score, 80);

    cr_assert_eq(parse_security_level("High", &level, &score), 0);
    cr_assert_eq(level, 7);
    cr_assert_eq(score, 100);
}

Test(security_level, rejects_null)
{
    DWORD level, score;
    cr_assert_eq(parse_security_level(NULL, &level, &score), -1);
}

Test(security_level, rejects_empty)
{
    DWORD level, score;
    cr_assert_eq(parse_security_level("", &level, &score), -1);
}

Test(security_level, rejects_unknown)
{
    DWORD level, score;
    cr_assert_eq(parse_security_level("medium", &level, &score), -1);
    cr_assert_eq(parse_security_level("sl_normal", &level, &score), -1);
}
