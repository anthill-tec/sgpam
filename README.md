# SecuGen U20 PAM Integration — Installation Guide

**Version**: [0.2.0](https://github.com/4property/sgpam/releases/tag/0.2.0)

Target: CachyOS (Arch Linux x86_64), SDK X64 build

## Files

| File           | Purpose                                                     |
|----------------|-------------------------------------------------------------|
| `pam_sgfp.c`   | PAM module — fingerprint auth for sudo/login/SDDM           |
| `sg_enroll.c`  | Enrollment CLI — captures and saves a fingerprint template  |
| `Makefile`     | Builds both, installs SDK libs and binaries                 |

Templates stored at: `/etc/security/sg_fingerprints/<username>_<finger>.tpl`
Format: SG400 (~400 bytes, AES-encrypted by SDK), root-only (mode 0600).

---

## Prerequisites

```fish
sudo pacman -S pam gcc make
```

---

## Step 1 — Set your SDK path

```fish
set -x SGDK ~/Documents/device_projects/"FDx SDK Pro for Linux v4.0c"/FDx_SDK_PRO_LINUX4_X64_4_0_0
set -x SGDK_LIB $SGDK/lib/linux4X64
```

To persist across sessions, add those two lines to `~/.config/fish/config.fish`.

---

## Step 2 — Verify architecture

```fish
make check-arch SGDK=$SGDK
```

Both SDK and system PAM should report `ELF 64-bit x86-64`.

---

## Step 3 — Install SDK libraries

```fish
cd $SGDK_LIB
sudo make uninstall install
sudo ldconfig
```

This copies all `.so` files and `.dat`/`.lic` calibration files to `/usr/local/lib`.

---

## Step 4 — Build

```fish
cd ~/Documents/device_projects/sgpam
make SGDK=$SGDK
```

---

## Step 5 — Install

```fish
sudo make install SGDK=$SGDK
```

---

## Step 6 — Enroll your fingerprint

```fish
sudo sg_enroll antonyj right-index
```

You will be prompted to scan the same finger twice. The tool reports the quality
score of each capture and the match score between samples. A score ≥ 80/199
(SL_NORMAL) is required for the enrollment to be saved.

Multiple fingers can be enrolled per user. List and manage them with:

```fish
sudo sg_enroll --list antonyj
sudo sg_enroll --remove antonyj right-index
```

---

## Step 7 — Configure PAM

### sudo (test here first)

Edit `/etc/pam.d/sudo`:
```
#%PAM-1.0
auth    sufficient  pam_sgfp.so
auth    include     system-auth
```

`sufficient` means: fingerprint success → grant immediately.
Scanner absent / timeout / no template → fall through to password.

### SDDM (graphical login)

Edit `/etc/pam.d/sddm`:
```
auth    sufficient  pam_sgfp.so
auth    include     system-login
```

### TTY login

Edit `/etc/pam.d/system-local-login`:
```
auth    sufficient  pam_sgfp.so
auth    include     system-auth
```

---

## Testing

```fish
# Force re-authentication then test sudo
sudo -k && sudo whoami

# Watch auth log in real time
sudo journalctl -f _COMM=sudo | grep pam_sgfp
```

---

## Security notes

- Security level is `SL_ABOVE_NORMAL` (match score ≥ 90). Lower to `SL_NORMAL`
  (≥ 80) in `pam_sgfp.c` if you experience too many false rejections.
- Users without an enrolled template receive `PAM_USER_UNKNOWN`, causing PAM to
  silently skip fingerprint and fall through to the next auth rule (password).
  This means non-enrolled users are never locked out.
- Fake finger detection (liveness) is active by default in the U20 driver.
- Templates are encrypted by the SDK and readable only by root.

---

## Re-enroll / remove

```fish
# Re-enroll (overwrites existing template)
sudo sg_enroll antonyj right-index

# Remove a specific finger
sudo sg_enroll --remove antonyj right-index

# Remove interactively (choose from menu)
sudo sg_enroll --remove antonyj

# List enrolled fingers
sudo sg_enroll --list antonyj
```
