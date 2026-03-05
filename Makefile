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

# X64 SDK layout
SGDK_LIB ?= $(SGDK)/lib/linux4X64
SGDK_INC ?= $(SGDK_LIB)

# ── Compiler flags ────────────────────────────────────────────────────────────
CC     = gcc
CFLAGS = -O2 -fPIC -Wall -Wextra -std=gnu11 \
         -I$(SGDK_INC) \
         -D__LINUX4

LDFLAGS_COMMON ?= -L$(SGDK_LIB) -Wl,-rpath,/usr/local/lib
LIBS_PAM       = -lsgfplib -lstdc++ -lpam
LIBS_ENROLL    = -lsgfplib -lstdc++

# ── DRM blanking tool (no SGDK dependency) ───────────────────────────────────
DRM_CFLAGS = $(shell pkg-config --cflags libdrm)
DRM_LIBS   = $(shell pkg-config --libs libdrm)

# ── Install paths ─────────────────────────────────────────────────────────────
PAM_MODULE_DIR = /usr/lib/security
ENROLL_BIN_DIR = /usr/local/bin

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all clean install install-sdk check-arch test test-verbose test-clean test-vkms

all: pam_sgfp.so sg_enroll sg-drm-blank

pam_sgfp.so: pam_sgfp.c sg_fingers.h
	$(CC) $(CFLAGS) -c -o pam_sgfp.o pam_sgfp.c
	$(CC) -shared -fPIC -o pam_sgfp.so pam_sgfp.o $(LDFLAGS_COMMON) $(LIBS_PAM)

sg_enroll: sg_enroll.c sg_fingers.h
	$(CC) $(CFLAGS) -o sg_enroll sg_enroll.c $(LDFLAGS_COMMON) $(LIBS_ENROLL)

sg-drm-blank: sg-drm-blank.c
	$(CC) -O2 -Wall -Wextra -std=gnu11 $(DRM_CFLAGS) -o sg-drm-blank sg-drm-blank.c $(DRM_LIBS)

# Install SDK shared libraries to /usr/local/lib
install-sdk:
	cd $(SGDK_LIB) && sudo make uninstall install

# Install our binaries
install: all
	sudo install -m 755 pam_sgfp.so $(PAM_MODULE_DIR)/pam_sgfp.so
	sudo install -m 755 sg_enroll   $(ENROLL_BIN_DIR)/sg_enroll
	sudo install -m 755 sg-drm-blank $(ENROLL_BIN_DIR)/sg-drm-blank

# Sanity check — confirm SDK and system PAM are both 64-bit
check-arch:
	@echo "=== SDK library ==="
	@file $(SGDK_LIB)/libsgfplib.so.4.0.1
	@echo ""
	@echo "=== System PAM ==="
	@file /usr/lib/security/pam_unix.so

clean:
	rm -f pam_sgfp.o pam_sgfp.so sg_enroll sg-drm-blank
	rm -f tests/*.o tests/test_valid_username tests/test_load_template \
	      tests/test_pam_authenticate tests/test_sg_enroll \
	      tests/test_finger_names tests/test_load_templates \
	      tests/test_drm_blank tests/test_drm_blank_vkms

# ── Test framework (Criterion + --wrap mocking) ──────────────────────────────

TEST_DIR     = tests
TEST_CFLAGS  = -g -O0 -fPIC -w -std=gnu11 '-I$(SGDK_INC)' -I$(TEST_DIR) -D__LINUX4

# --wrap flags for SDK functions used by pam_sgfp.c and sg_enroll.c
WRAP_SDK = -Wl,--wrap=SGFPM_Create \
           -Wl,--wrap=SGFPM_Terminate \
           -Wl,--wrap=SGFPM_Init \
           -Wl,--wrap=SGFPM_SetTemplateFormat \
           -Wl,--wrap=SGFPM_OpenDevice \
           -Wl,--wrap=SGFPM_CloseDevice \
           -Wl,--wrap=SGFPM_GetDeviceInfo \
           -Wl,--wrap=SGFPM_GetMaxTemplateSize \
           -Wl,--wrap=SGFPM_GetImageEx \
           -Wl,--wrap=SGFPM_GetImageQuality \
           -Wl,--wrap=SGFPM_CreateTemplate \
           -Wl,--wrap=SGFPM_GetTemplateSize \
           -Wl,--wrap=SGFPM_MatchTemplate \
           -Wl,--wrap=SGFPM_GetMatchingScore

# --wrap flags for PAM functions
WRAP_PAM = -Wl,--wrap=pam_get_user \
           -Wl,--wrap=pam_prompt

# --wrap flags for sg_enroll extras
WRAP_ENROLL = -Wl,--wrap=geteuid \
              -Wl,--wrap=sleep

# --wrap flags for DRM system calls (pass-through by default, intercept DRM paths/fds)
WRAP_DRM_SYS = -Wl,--wrap=open -Wl,--wrap=close \
               -Wl,--wrap=mmap -Wl,--wrap=munmap -Wl,--wrap=usleep

# --wrap flags for DRM library functions (always active)
WRAP_DRM_LIB = -Wl,--wrap=drmIoctl \
               -Wl,--wrap=drmSetMaster -Wl,--wrap=drmDropMaster \
               -Wl,--wrap=drmModeGetResources -Wl,--wrap=drmModeFreeResources \
               -Wl,--wrap=drmModeGetCrtc -Wl,--wrap=drmModeFreeCrtc \
               -Wl,--wrap=drmModeSetCrtc \
               -Wl,--wrap=drmModeAddFB -Wl,--wrap=drmModeRmFB

DRM_TEST_CFLAGS = -g -O0 -fPIC -w -std=gnu11 $(DRM_CFLAGS) -I$(TEST_DIR)

# Mock object files
MOCK_SDK_OBJ = $(TEST_DIR)/mock_sdk.o
MOCK_PAM_OBJ = $(TEST_DIR)/mock_pam.o
MOCK_DRM_OBJ = $(TEST_DIR)/mock_drm.o

$(MOCK_SDK_OBJ): $(TEST_DIR)/mock_sdk.c $(TEST_DIR)/mock_state.h
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

$(MOCK_PAM_OBJ): $(TEST_DIR)/mock_pam.c $(TEST_DIR)/mock_state.h
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

$(MOCK_DRM_OBJ): $(TEST_DIR)/mock_drm.c $(TEST_DIR)/mock_drm_state.h
	$(CC) $(DRM_TEST_CFLAGS) -c -o $@ $<

# ── Test binaries ─────────────────────────────────────────────────────────────

# 1. Pure logic tests — includes pam_sgfp.c which references SDK/PAM symbols
$(TEST_DIR)/test_valid_username: $(TEST_DIR)/test_valid_username.c pam_sgfp.c \
                                  $(MOCK_SDK_OBJ) $(MOCK_PAM_OBJ) $(TEST_DIR)/mock_state.h
	$(CC) $(TEST_CFLAGS) -o $@ $< $(MOCK_SDK_OBJ) $(MOCK_PAM_OBJ) \
	    $(WRAP_SDK) $(WRAP_PAM) -lcriterion

# 2. File I/O tests — includes pam_sgfp.c which references SDK/PAM symbols
$(TEST_DIR)/test_load_template: $(TEST_DIR)/test_load_template.c pam_sgfp.c \
                                 $(MOCK_SDK_OBJ) $(MOCK_PAM_OBJ) $(TEST_DIR)/mock_state.h
	$(CC) $(TEST_CFLAGS) -o $@ $< $(MOCK_SDK_OBJ) $(MOCK_PAM_OBJ) \
	    $(WRAP_SDK) $(WRAP_PAM) -lcriterion

# 3. PAM auth flow — needs SDK + PAM mocks
$(TEST_DIR)/test_pam_authenticate: $(TEST_DIR)/test_pam_authenticate.c pam_sgfp.c \
                                    $(MOCK_SDK_OBJ) $(MOCK_PAM_OBJ) $(TEST_DIR)/mock_state.h
	$(CC) $(TEST_CFLAGS) -o $@ $< $(MOCK_SDK_OBJ) $(MOCK_PAM_OBJ) \
	    $(WRAP_SDK) $(WRAP_PAM) -lcriterion

# 4. Enrollment flow — needs SDK mocks + extra wraps
$(TEST_DIR)/test_sg_enroll: $(TEST_DIR)/test_sg_enroll.c sg_enroll.c sg_fingers.h \
                             $(MOCK_SDK_OBJ) $(TEST_DIR)/mock_state.h
	$(CC) $(TEST_CFLAGS) -Dmain=sg_enroll_main -o $@ $< $(MOCK_SDK_OBJ) \
	    $(WRAP_SDK) $(WRAP_ENROLL) -lcriterion

# 5. Finger name validation (pure logic, no wraps)
$(TEST_DIR)/test_finger_names: $(TEST_DIR)/test_finger_names.c sg_fingers.h
	$(CC) $(TEST_CFLAGS) -o $@ $< -lcriterion

# 6. Multi-template loading
$(TEST_DIR)/test_load_templates: $(TEST_DIR)/test_load_templates.c pam_sgfp.c sg_fingers.h \
                                  $(MOCK_SDK_OBJ) $(MOCK_PAM_OBJ) $(TEST_DIR)/mock_state.h
	$(CC) $(TEST_CFLAGS) -o $@ $< $(MOCK_SDK_OBJ) $(MOCK_PAM_OBJ) \
	    $(WRAP_SDK) $(WRAP_PAM) -lcriterion

# 7. DRM screen blanking
$(TEST_DIR)/test_drm_blank: $(TEST_DIR)/test_drm_blank.c sg-drm-blank.c \
                             $(MOCK_DRM_OBJ) $(TEST_DIR)/mock_drm_state.h
	$(CC) $(DRM_TEST_CFLAGS) -Dmain=sg_drm_blank_main -o $@ $< $(MOCK_DRM_OBJ) \
	    $(WRAP_DRM_SYS) $(WRAP_DRM_LIB) $(DRM_LIBS) -lcriterion

# 8. VKMS integration test (requires: sudo modprobe vkms + root)
$(TEST_DIR)/test_drm_blank_vkms: $(TEST_DIR)/test_drm_blank_vkms.c sg-drm-blank.c
	$(CC) $(DRM_TEST_CFLAGS) -Dmain=sg_drm_blank_main -o $@ $< $(DRM_LIBS) -lcriterion

# ── Test runners ──────────────────────────────────────────────────────────────

test: $(TEST_DIR)/test_valid_username $(TEST_DIR)/test_load_template \
      $(TEST_DIR)/test_pam_authenticate $(TEST_DIR)/test_sg_enroll \
      $(TEST_DIR)/test_finger_names $(TEST_DIR)/test_load_templates \
      $(TEST_DIR)/test_drm_blank
	@echo ""; echo "=== Running test suite ==="; echo ""
	@$(TEST_DIR)/test_valid_username && \
	 $(TEST_DIR)/test_load_template && \
	 $(TEST_DIR)/test_pam_authenticate && \
	 $(TEST_DIR)/test_sg_enroll && \
	 $(TEST_DIR)/test_finger_names && \
	 $(TEST_DIR)/test_load_templates && \
	 $(TEST_DIR)/test_drm_blank && \
	 echo "" && echo "=== All tests passed ==="

test-verbose: $(TEST_DIR)/test_valid_username $(TEST_DIR)/test_load_template \
              $(TEST_DIR)/test_pam_authenticate $(TEST_DIR)/test_sg_enroll \
              $(TEST_DIR)/test_finger_names $(TEST_DIR)/test_load_templates \
              $(TEST_DIR)/test_drm_blank
	@echo ""; echo "=== Running test suite (verbose) ==="; echo ""
	$(TEST_DIR)/test_valid_username --verbose
	$(TEST_DIR)/test_load_template --verbose
	$(TEST_DIR)/test_pam_authenticate --verbose
	$(TEST_DIR)/test_sg_enroll --verbose
	$(TEST_DIR)/test_finger_names --verbose
	$(TEST_DIR)/test_load_templates --verbose
	$(TEST_DIR)/test_drm_blank --verbose
	@echo "" && echo "=== All tests passed ==="

test-vkms: $(TEST_DIR)/test_drm_blank_vkms
	@echo "=== VKMS integration test (requires: sudo modprobe vkms + root) ==="
	$(TEST_DIR)/test_drm_blank_vkms -j1 --verbose

test-clean:
	rm -f $(TEST_DIR)/*.o $(TEST_DIR)/test_valid_username \
	      $(TEST_DIR)/test_load_template $(TEST_DIR)/test_pam_authenticate \
	      $(TEST_DIR)/test_sg_enroll $(TEST_DIR)/test_finger_names \
	      $(TEST_DIR)/test_load_templates $(TEST_DIR)/test_drm_blank \
	      $(TEST_DIR)/test_drm_blank_vkms
