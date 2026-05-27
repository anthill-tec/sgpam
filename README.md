# SecuGen U20 Fingerprint Authentication for Arch Linux

**Version**: [1.1.0](https://github.com/anthill-tec/sgpam/releases/tag/1.1.0)

PAM-based fingerprint authentication for the SecuGen U20 (`fdu05`) on Arch Linux /
CachyOS — for `sudo`, TTY console login, and graphical greeter login
(greetd/ReGreet, SDDM, GDM). Fingerprint is tried first; **password is always the
fallback**, so nobody is ever locked out.

Target: x86_64, SecuGen FDx SDK Pro v4.0c (X64).

## How it works

- **`pam_sgfp.so`** — the PAM `auth` module. Captures a fingerprint and matches it
  against the user's enrolled templates. Configured `sufficient`: a match grants
  access immediately; any failure (no scanner, timeout, no match) falls through to
  the next rule (password). Users with no enrolled template return
  `PAM_USER_UNKNOWN` and pass straight through — never locked out.
- **`sg_enroll`** — root-only CLI to enroll / list / remove templates, stored at
  `/etc/security/sg_fingerprints/<user>_<finger>.tpl` (SG400 format, AES-encrypted
  by the SDK, mode 0600).

## Install (Arch package — recommended)

`sgpam` is a pacman package. It depends on two companion packages built from the
**proprietary SecuGen FDx SDK Pro**, which you must supply (SecuGen does not permit
public redistribution):

| Package | Role | Provides |
|---|---|---|
| `secugen-fdx-driver` | runtime dependency | `libsgfplib.so` + device libs in `/usr/lib`, udev rule, `secugen` group |
| `secugen-fdx-sdk` | build dependency | `/usr/include/sgfplib.h`, `sgfplib.pc` |
| `sgpam` | this package | `pam_sgfp.so`, `sg_enroll`, man pages |

Build and install **in dependency order** — `secugen-fdx-sdk` depends on the
driver, so the driver must be installed first:

```fish
# 1. Driver: runtime libs + udev rule + 'secugen' group
cd path/to/secugen-fdx-driver
makepkg -si

# 2. SDK: build-time headers (depends on the driver)
cd path/to/secugen-fdx-sdk
makepkg -si

# 3. sgpam: builds, runs the test suite (check()), installs, and the
#    post_install hook configures PAM automatically
cd path/to/sgpam
makepkg -si
```

During the `sgpam` install you'll be prompted to confirm PAM setup for
**console + greeter login** and **sudo** — answer `y`. The hook patches
`/etc/pam.d/system-local-login` (covers greetd **and** TTY login, since both
`include` it) and `/etc/pam.d/sudo`, inserting `pam_sgfp.so` **after** the
`nologin`/`securetty` gates and **before** the password include — fingerprint
first, password fallback. **No manual `/etc/pam.d` editing required.**

> **Upgrading from a `make install`?** Remove the unmanaged files first, or
> `pacman -U` will hit a file conflict and `/usr/local/bin` will shadow the
> package:
> ```fish
> sudo rm -f /usr/local/bin/sg_enroll /usr/local/bin/sg-drm-blank /usr/lib/security/pam_sgfp.so
> ```

## Enroll a fingerprint

```fish
sudo sg_enroll <username> <finger-name>     # e.g. sudo sg_enroll alice right-index
sudo sg_enroll --list <username>
sudo sg_enroll --remove <username> <finger-name>
```

You scan the finger twice; the tool reports image quality and the match score and
saves the template once it meets the threshold (≥ 80/199, `SL_NORMAL`). Multiple
fingers per user are supported — the module tries all and accepts the first match.

- **Finger names:** `right-thumb right-index right-middle right-ring right-little`,
  `left-thumb left-index left-middle left-ring left-little`.
- `-s LEVEL` sets the enrollment security level; `-b/--brightness N` (0–100) pins
  the LED exposure (otherwise it auto-tunes).

## Tuning (optional PAM arguments)

Graphical greeters ship with `retries=4`. Tune per service in `/etc/pam.d/<service>`:

```
auth  sufficient  pam_sgfp.so retries=4
```

- **`retries=N`** (1–10, default 3): the sensor is sampled silently up to N times,
  accepting the first match — a hurried finger gets more chances within one prompt.
- **Smart Capture (AGC)** and **brightness escalation** are automatic: the module
  enables the sensor's auto-gain and scales LED exposure from the live readout
  across the retries. No configuration needed.

See `pam_sgfp(8)` and `sg_enroll(1)` for full details.

## Uninstall

```fish
sudo pacman -R sgpam
```

The `pre_remove` hook strips the `pam_sgfp.so` lines from the PAM files (leaving
`*.sgpam.bak` backups). **Enrolled templates are not deleted** — remove
`/etc/security/sg_fingerprints/` manually if desired.

## Building from source (development)

For contributors. Point `SGDK` at the vendor SDK:

```fish
set -x SGDK ~/path/to/FDx_SDK_PRO_LINUX4_X64_4_0_0
make check-arch          # verify SDK + system PAM are both 64-bit
make                     # build pam_sgfp.so, sg_enroll, sg-drm-blank
make test                # run the Criterion test suite
```

`make install` exists but installs **unmanaged** files (`/usr/local/bin`,
`/usr/lib/security`) — prefer the pacman package for real systems; use `make`
only for build/test during development.

## Security notes

- Matching uses `SL_NORMAL` (match score ≥ 80/199). Set a stricter level at
  enrollment with `sg_enroll -s`.
- `sufficient` placement + `PAM_USER_UNKNOWN` passthrough mean a non-enrolled user,
  or a missing/failing scanner, simply falls through to password — no lockout.
- The U20's fake-finger (liveness) detection is active in the driver.
- Templates are AES-encrypted by the SDK and root-only
  (`/etc/security/sg_fingerprints/`, dir 0700, files 0600).

## Not included in 1.1.0

DRM screen-blanking (`sg-drm-blank --hold` and the greetd Hyprland session
wrappers) is deferred pending further testing of the `--hold` variant. The code
remains in the tree and is built/tested, just not shipped.
