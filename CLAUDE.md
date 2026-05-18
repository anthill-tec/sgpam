# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**sgpam** — SecuGen U20 fingerprint authentication for Arch Linux (CachyOS target). Provides PAM-based fingerprint auth for `sudo`, TTY login, and graphical login (SDDM, GDM, greetd/ReGreet), plus a DRM screen-blanking helper that hides the desktop during the transition from greeter to Wayland session (Hyprland).

C project, GNU Make build, pacman packaging via PKGBUILD. Git Flow with semver tags; current version is **1.0.0** (see `PKGBUILD` and `README.md` — both must be bumped together on release).

## Build, Install, Test

The build depends on the proprietary **SecuGen FDx SDK Pro for Linux v4.0c** (X64). Either pass `SGDK=…` to `make`, or set `SGDK` once in your shell. On packaged installs the SDK is consumed from `/usr/include` and `/usr/lib` via `SGDK_INC` / `SGDK_LIB` overrides (see PKGBUILD).

```fish
# Local dev (Fish shell, SDK in user dir):
set -x SGDK ~/Documents/device_projects/"FDx SDK Pro for Linux v4.0c"/FDx_SDK_PRO_LINUX4_X64_4_0_0
make check-arch              # verify SDK + system PAM are both 64-bit
make                         # builds pam_sgfp.so, sg_enroll, sg-drm-blank
sudo make install-sdk        # copies SDK shared libs to /usr/local/lib
sudo make install            # installs binaries, man pages, template dir

# Package for Arch:
makepkg -f                   # uses PKGBUILD; runs `make test` in check()
```

### Tests (Criterion + `--wrap` mocking)

```fish
make test                    # all unit tests except VKMS
make test-verbose            # same, with -j1 --verbose per binary
make test-clean              # remove test binaries and .o
make test-vkms               # integration test, requires root + VKMS

# Run a single test binary directly:
./tests/test_pam_authenticate --verbose
./tests/test_pam_authenticate --filter "suite/case"
```

`test-vkms` requires `sudo modprobe vkms` first (or use `scripts/run-vkms-test.sh` which sets it up). It exercises the real libdrm path against the kernel's virtual KMS device — do not run unprivileged.

## Architecture

Three independent C artifacts share `sg_fingers.h` (finger-name table, template path helpers) but otherwise have separate dependency stacks:

| Artifact         | Source            | Links                         | Role                                                                                              |
|------------------|-------------------|-------------------------------|---------------------------------------------------------------------------------------------------|
| `pam_sgfp.so`    | `pam_sgfp.c`      | `libsgfplib`, `libstdc++`, `libpam` | PAM auth module. Loaded by `/etc/pam.d/{sudo,login,sddm,greetd,…}`. Loads all templates for the calling user, captures one fingerprint via the SDK, returns `PAM_SUCCESS` on a match. |
| `sg_enroll`      | `sg_enroll.c`     | `libsgfplib`, `libstdc++`     | Root-only CLI. Captures two samples per finger, validates quality + match score (≥80/199 SL_NORMAL by default), writes `/etc/security/sg_fingerprints/<user>_<finger>.tpl` (SG400 format, ~400 B, AES-encrypted by SDK, mode 0600). Supports `--list` and `--remove`. |
| `sg-drm-blank`   | `sg-drm-blank.c`  | `libdrm` (pkg-config)         | No SDK dependency. Sets every connected CRTC to black via libdrm/KMS. Invoked by the Wayland session wrappers to hide flicker between greeter exit and compositor start. |

### Wayland session wrappers (greetd path)

`sgpam-start-hyprland` and `sgpam-start-hyprland-uwsm` wrap Hyprland startup. They run `sg-drm-blank` before/after the compositor handoff so the greeter→session transition is visually clean. The matching `.desktop` files land in `/usr/share/wayland-sessions/` and appear in the ReGreet session picker.

`hyprland.conf` is installed to `/etc/greetd/` and is the minimal Hyprland config used by ReGreet itself (greetd's `command =` line is patched by the install hook to `start-hyprland -- -c /etc/greetd/hyprland.conf`).

### Install hooks (`sgpam.install`)

`post_install` / `post_upgrade` auto-detect the active display manager (sddm → gdm → greetd) and patch `/etc/pam.d/<dm>` + `sudo` + `login` with `auth sufficient pam_sgfp.so`. For greetd it additionally rewrites `/etc/greetd/config.toml` to use the wrapper and applies a dark-theme block to `regreet.toml`. `pre_remove` undoes all of that; backups are left as `*.sgpam.bak`. **Enrolled templates are never deleted by package removal.**

## Test scaffolding

Tests live in `tests/` and use Criterion. The Makefile composes each test binary out of:

- The real production source compiled with `-include` of a `mock_*_state.h` (so static globals can be reset between tests).
- Linker `--wrap=symbol` flags for SDK / PAM / DRM entry points. The wrap lists are split into four groups in the Makefile: `WRAP_SDK`, `WRAP_PAM`, `WRAP_ENROLL`, `WRAP_DRM_SYS` (passthrough by default, intercepted for DRM paths/fds) and `WRAP_DRM_LIB`.
- Stub implementations in `tests/mock_sdk.c`, `tests/mock_pam.c`, `tests/mock_drm.c`.
- `-Dmain=sg_drm_blank_main` (DRM tests) so the real `main()` can be invoked as a function.

When adding a test that exercises a new SDK/PAM/DRM call, add the symbol to the corresponding `WRAP_*` variable in the Makefile **and** add a stub to the matching `mock_*.c` — linkage will otherwise fail with "undefined reference to __wrap_…".

## Important repo quirks

- **`src/` is a stale build copy** of the root tree (older snapshot, contains compiled artifacts). The canonical sources live at the repo root; the Makefile, PKGBUILD, and tests all operate on root paths. Do not edit files under `src/` — changes there are not picked up by the build and will diverge.
- The PKGBUILD's `prepare()` copies a fixed file list from `$startdir` into `$srcdir`; **any new top-level source or session asset must be added there too**, otherwise it won't make it into the package.
- Template path layout is part of the on-disk contract: `/etc/security/sg_fingerprints/<username>_<finger>.tpl`. Both `pam_sgfp.c` and `sg_enroll.c` derive this independently — keep `sg_fingers.h` (finger-name table) the single source of truth.
- `sg_enroll` requires root for **all** operations (including `--list`). This is intentional; the template directory is mode 0700.
- The CFLAGS define `-D__LINUX4` and the SDK rpath is hardcoded to `/usr/local/lib` for local builds — leave both alone unless you understand the SDK loader.
- **`/usr/local/lib` rpath is `DT_RUNPATH`, not `DT_RPATH`.** Modern `ld` writes `-Wl,-rpath,…` as `DT_RUNPATH`, which is only searched for *direct* dependencies. `libsgfplib.so` pulls in `libsgimage.so` transitively, so the loader falls through to `ldconfig` for those. The source-tree `install-sdk` target therefore drops `/etc/ld.so.conf.d/sgpam-sdk.conf` and runs `ldconfig`; the matching `uninstall-sdk` target removes it. The pacman package is unaffected because PKGBUILD overrides `LDFLAGS_COMMON="$LDFLAGS"` (no embedded rpath) and the SDK comes from `secugen-fdx-sdk` in `/usr/lib`, which ldconfig already knows.

## Release process

Git Flow. Tags are at `0.1.0`, `0.2.0`, `0.3.0`, `1.0.0`. A release bumps:

1. `pkgver` in `PKGBUILD`
2. The version + release-tag link in `README.md`
3. Single commit "Bump version to X.Y.Z" on `release/X.Y.Z`, then `git flow release finish`

After merging to `master`, rebuild the pacman package with `makepkg -f` from `master` and verify `make test` passes inside the package check phase.
