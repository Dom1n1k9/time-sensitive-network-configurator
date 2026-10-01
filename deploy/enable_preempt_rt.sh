#!/usr/bin/env bash
# enable_preempt_rt.sh — provision a PREEMPT_RT kernel + PTP tuning on the RPi/CNC.
#
# Why: the RPi is the PTP v2 grandmaster (ptpd). A PREEMPT_RT kernel gives it
# single-digit-us scheduler latency so the SYNC/FOLLOW_UP + HW/SW timestamps the
# STM32 endpoint locks to have tight, stable jitter.
#
# Usage (run ON the RPi, as root):
#     sudo bash deploy/enable_preempt_rt.sh
#
# From the dev box, once the RPi is reachable (export the RPi user + password
# first — do NOT hard-code the password in the repo):
#     export WTSN_RPI_USER=wtsn WTSN_RPI_PASS='<rpi user password>'
#     RPI_IP=<rpi ip>
#     sshpass -p "$WTSN_RPI_PASS" scp -o StrictHostKeyChecking=no deploy/enable_preempt_rt.sh "$WTSN_RPI_USER@$RPI_IP:/tmp/"
#     sshpass -p "$WTSN_RPI_PASS" ssh -o StrictHostKeyChecking=no "$WTSN_RPI_USER@$RPI_IP" \
#         "echo $WTSN_RPI_PASS | sudo -S bash /tmp/enable_preempt_rt.sh"
#
# Idempotent + safe: it only ADDS the RT kernel (the stock kernel stays for
# rollback), then reboots. It never removes the current kernel.
set -euo pipefail

log()  { printf '\033[1;32m[rt]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[rt]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[rt]\033[0m %s\n' "$*" >&2; }

# ---- 0. root ----
if [[ $EUID -ne 0 ]]; then
    err "run as root (sudo bash $0)"; exit 1
fi

# ---- 1. is this a Raspberry Pi? ----
if ! grep -qiE 'raspberry' /etc/os-release 2>/dev/null; then
    err "does not look like a Raspberry Pi OS host (no 'raspberry' in /etc/os-release)."
    err "Refusing to continue."; exit 1
fi
ARCH="$(uname -m)"
log "Raspberry Pi OS, arch=$ARCH"

# ---- 2. current kernel ----
log "current kernel: $(uname -r)"
if uname -v | grep -qi 'preempt_rt\|preempt-rt'; then
    log "already running a PREEMPT_RT kernel. Applying tuning + done (no reinstall)."
    SKIP_INSTALL=1
fi

# ---- 3. install the RT kernel (adds alongside the stock kernel) ----
if [[ -z "${SKIP_INSTALL:-}" ]]; then
    if dpkg -s rpi-rt-kernel >/dev/null 2>&1; then
        log "rpi-rt-kernel already installed."
    else
        log "installing rpi-rt-kernel (PREEMPT_RT kernel + modules)..."
        # Refresh indexes once; ignore failure (offline still works if cached).
        apt-get update -y || warn "apt-get update failed (offline?); trying install anyway"
        if ! apt-get install -y rpi-rt-kernel; then
            err "could not install 'rpi-rt-kernel'."
            err "  - check the host is Raspberry Pi OS (64-bit Bookworm/Bookworm or 32-bullseye)"
            err "  - ensure the Raspberry Pi apt repo is enabled: 'raspi' component"
            err "The stock kernel is untouched. See rollback at the end of this script."
            exit 1
        fi
    fi
fi

# ---- 4. PTP tuning (safe / idempotent) ----
log "applying PTP tuning (CPU governor + net sysctls)..."

# CPU governor -> performance (if cpufreq is present; guard the busybox-less paths).
if [[ -d /sys/devices/system/cpu/cpu0/cpufreq ]]; then
    for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        echo performance > "$g" 2>/dev/null || true
    done
    log "  cpu governor -> performance"
else
    warn "  no cpufreq sysfs found; skipping governor"
fi

# A small, conservative net tuning set for low-latency UDP/PTP on the wired NIC.
cat > /etc/sysctl.d/99-wtsn-tpo.conf <<'SYSCTL'
# wtsn PTP (ptpd grandmaster) — modest, safe network tuning
net.core.netdev_max_backlog = 16384
net.core.somaxconn = 8192
net.ipv4.udp_rmem_min = 8192
net.ipv4.udp_wmem_min = 8192
# (Optional, manual) to further isolate ptpd, add to /boot/config.txt (firmware boot)
# and reboot:   isolcpus=3   default_halt_task=0
SYSCTL
sysctl --system >/dev/null 2>&1 || true
log "  /etc/sysctl.d/99-wtsn-tpo.conf applied"

# ---- 5. make sure the RT kernel is the default on next boot ----
# The rpi-rt-kernel package's boot hook normally selects the RT kernel
# automatically. If this RPi uses a firmware boot with an explicit 'kernel=',
# confirm what will boot and report it (do not force a wrong value).
if [[ -f /boot/config.txt || -f /boot/firmware/config.txt ]]; then
    CFG=$([[ -f /boot/firmware/config.txt ]] && echo /boot/firmware/config.txt || echo /boot/config.txt)
    if grep -qE '^\s*kernel=' "$CFG"; then
        warn "  /boot/config.txt sets an explicit 'kernel=' — verify it points to the RT kernel."
        grep -E '^\s*kernel=' "$CFG"
    fi
fi

# ---- 6. verify + rollback + reboot ----
cat <<EOF

------------------------------------------------------------------------
 Done. Next steps:
   1. Reboot to load the RT kernel:        sudo reboot
   2. After reboot, verify (expect 'PREEMPT_RT' in the version string):
          uname -v
          grep -i PREEMPT_RT /boot/config-\$(uname -r) | head
   3. Re-run 'sudo ptpd ...' (or 'sudo systemctl restart ptpd') and check the
      endpoint locks: on this box 'watch cat /tmp/wtsn_tsn_opcua.json | jq .ptp'
------------------------------------------------------------------------
 Rollback (if the RT kernel misbehaves) — stock kernel is untouched:
   # firmware boot: edit /boot/config.txt, remove/point 'kernel=' back to stock
   #   e.g. kernel=kernel8.img  (or kernel8-64.img), then reboot.
   # remove the package entirely if desired:  apt-get remove -y rpi-rt-kernel
EOF

log "NOT auto-rebooting (so you can inspect); reboot when ready."
