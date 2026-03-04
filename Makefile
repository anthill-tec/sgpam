# Makefile for SecuGen U20 PAM module + enrollment tool
# Target: x86_64 / CachyOS (Arch Linux)
#
# Usage:
#   make SGDK=~/Documents/device_projects/"FDx SDK Pro for Linux v4.0c"/FDx_SDK_PRO_LINUX4_X64_4_0_0
#
# Or set the Fish variable first and just run make:
#   set -x SGDK ~/Documents/device_projects/"FDx SDK Pro for Linux v4.0c"/FDx_SDK_PRO_LINUX4_X64_4_0_0
#   make

SGDK ?= $(error Set SGDK to the X64 SDK root directory)

# X64 SDK layout — header lives alongside the .so files
SGDK_LIB = $(SGDK)/lib/linux4X64
SGDK_INC = $(SGDK_LIB)

# ── Compiler flags ────────────────────────────────────────────────────────────
CC     = gcc
CFLAGS = -O2 -fPIC -Wall -Wextra \
         -I$(SGDK_INC) \
         -D__LINUX4

LDFLAGS_PAM = -shared -fPIC \
              -L$(SGDK_LIB) \
              -Wl,-rpath,/usr/local/lib \
              -lsgfplib -lpam

LDFLAGS_ENROLL = -L$(SGDK_LIB) \
                 -Wl,-rpath,/usr/local/lib \
                 -lsgfplib

# ── Install paths ─────────────────────────────────────────────────────────────
PAM_MODULE_DIR = /usr/lib/security
ENROLL_BIN_DIR = /usr/local/bin

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all clean install install-sdk check-arch

all: pam_sgfp.so sg_enroll

pam_sgfp.so: pam_sgfp.c
	$(CC) $(CFLAGS) -c -o pam_sgfp.o pam_sgfp.c
	$(CC) $(LDFLAGS_PAM) -o pam_sgfp.so pam_sgfp.o

sg_enroll: sg_enroll.c
	$(CC) $(CFLAGS) -o sg_enroll sg_enroll.c $(LDFLAGS_ENROLL)

# Install SDK shared libraries to /usr/local/lib
install-sdk:
	cd $(SGDK_LIB) && sudo make uninstall install

# Install our binaries
install: all
	sudo install -m 755 pam_sgfp.so $(PAM_MODULE_DIR)/pam_sgfp.so
	sudo install -m 755 sg_enroll   $(ENROLL_BIN_DIR)/sg_enroll

# Sanity check — confirm SDK and system PAM are both 64-bit
check-arch:
	@echo "=== SDK library ==="
	@file $(SGDK_LIB)/libsgfplib.so.4.0.1
	@echo ""
	@echo "=== System PAM ==="
	@file /usr/lib/security/pam_unix.so

clean:
	rm -f pam_sgfp.o pam_sgfp.so sg_enroll
