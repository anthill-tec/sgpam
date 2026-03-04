/*
 * mock_pam.c — __wrap implementations for PAM functions
 *
 * Intercepts pam_get_user and pam_prompt (used by pam_info macro).
 */

#include <security/pam_modules.h>
#include <stdarg.h>
#include "mock_state.h"

int __wrap_pam_get_user(pam_handle_t *pamh, const char **user, const char *prompt)
{
    (void)pamh; (void)prompt;
    if (user)
        *user = g_mock.pam_username;
    return g_mock.pam_get_user_rv;
}

/*
 * pam_info() expands to pam_prompt(pamh, PAM_TEXT_INFO, NULL, fmt, ...).
 * We just swallow it — no console output during tests.
 */
int __wrap_pam_prompt(pam_handle_t *pamh, int style,
                      char **response, const char *fmt, ...)
{
    (void)pamh; (void)style; (void)response; (void)fmt;
    return 0; /* PAM_SUCCESS */
}
