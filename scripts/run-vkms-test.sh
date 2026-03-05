#!/bin/sh
# run-vkms-test.sh — Run VKMS integration tests from a TTY
#
# Usage:
#   1. Close all desktop apps
#   2. Switch to TTY: Ctrl+Alt+F2
#   3. Log in
#   4. Run: sudo ./scripts/run-vkms-test.sh
#
# The script will terminate the Hyprland desktop session, load vkms,
# run tests, unload vkms, and restart greetd so you can log back in.

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_FILE="$PROJECT_DIR/tests/vkms-test.log"

# Tee all output to log file and console
exec > >(tee "$LOG_FILE") 2>&1
echo "==> VKMS test run: $(date)"
echo "==> Log file: $LOG_FILE"

if [ "$(id -u)" -ne 0 ]; then
    echo "error: must run as root (sudo)" >&2
    exit 1
fi

# Stop greetd first — prevents it from respawning a greeter Hyprland
echo "==> Stopping greetd service..."
systemctl stop greetd

# Kill the desktop session process tree: uwsm / start-hyprland / Hyprland
if pgrep -x 'Hyprland|uwsm|start-hyprland' >/dev/null 2>&1; then
    echo "==> Sending SIGTERM to Hyprland session processes..."
    killall -TERM Hyprland start-hyprland uwsm 2>/dev/null
    timeout=5
    while pgrep -x 'Hyprland|uwsm|start-hyprland' >/dev/null 2>&1 && [ $timeout -gt 0 ]; do
        sleep 1
        timeout=$((timeout - 1))
    done
    if pgrep -x 'Hyprland|uwsm|start-hyprland' >/dev/null 2>&1; then
        echo "==> Processes still running, sending SIGKILL..."
        killall -KILL Hyprland start-hyprland uwsm 2>/dev/null
        sleep 1
    fi
fi

if pgrep -x Hyprland >/dev/null 2>&1; then
    echo "error: could not stop Hyprland" >&2
    echo "==> Restarting greetd..."
    systemctl start greetd
    exit 1
fi
echo "==> No compositor running."

echo "==> Loading vkms module..."
modprobe vkms

echo "==> Building and running VKMS integration tests..."
cd "$PROJECT_DIR"
rc=0
make test-vkms SGDK_INC=/usr/include SGDK_LIB=/usr/lib LDFLAGS_COMMON="" || rc=$?

# Teardown: unload vkms BEFORE restarting greetd (critical — if greetd
# starts Hyprland while vkms is loaded, Hyprland grabs it and crashes)

echo "==> Killing any leftover test processes holding vkms..."
pkill -KILL -f test_drm_blank_vkms 2>/dev/null
sleep 1

# Also kill any orphaned children (helper subprocesses from the test)
fuser -k /dev/dri/card* 2>/dev/null
sleep 1

echo "==> Unloading vkms module..."
attempts=3
while [ $attempts -gt 0 ]; do
    modprobe -r vkms 2>/dev/null && break
    echo "  retrying vkms unload ($attempts attempts left)..."
    fuser -k /dev/dri/card* 2>/dev/null
    sleep 2
    attempts=$((attempts - 1))
done

if lsmod | grep -q '^vkms'; then
    echo "  CRITICAL: vkms still loaded! NOT restarting greetd to prevent Hyprland crash."
    echo "  Manual fix: sudo fuser -k /dev/dri/card*; sudo modprobe -r vkms; sudo systemctl start greetd"
    exit 1
fi

echo "==> vkms unloaded successfully."
echo "==> Restarting greetd..."
systemctl restart greetd

if [ $rc -eq 0 ]; then
    echo "==> All VKMS tests passed. Switch to F1 to log back in."
else
    echo "==> VKMS tests FAILED (exit code $rc). Switch to F1 to log back in."
fi

exit $rc
