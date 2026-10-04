#!/usr/bin/env bash
# enable_preempt_rt.sh — provision a PREEMPT_RT kernel + PTP tuning on the RPi/CNC.
#
# Why: the RPi is the PTP v2 grandmaster (ptpd, GM IP 192.168.1.10; the STM32
# endpoint stm32-tsn/ is 192.168.1.20). A PREEMPT_RT kernel gives it tight,
# stable scheduler latency so the SYNC/FOLLOW_UP + timestamps the endpoint locks
# to have low jitter.
#
# Works on Raspberry Pi OS AND plain Debian on the Pi (this project's Pi runs
# Debian 13). The RPi RT kernel is the 'rpi-*-rt' image (e.g.
# linux-image-<ver>+rpt-rpi-v8-rt) — NOT the 'rpi-rt-kernel' package, which only
# exists in the Raspberry Pi OS repo. There is no 'rpi-2712-rt'; the 64-bit RT
# image is the 'rpi-v8-rt' flavor (boots the Pi 5).
#
# Usage (run ON the RPi, as root):
#     sudo bash deploy/enable_preempt_rt.sh
#
# Idempotent + safe: it only ADDS the RT kernel (stock kernel stays for rollback)
# and points /boot/firmware/config.txt at it (a .bak-rt backup is kept). It never
# removes the current kernel and does NOT auto-reboot — reboot when ready.
set -euo pipefail

log()  { printf '\033[1;32m[rt]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[rt]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[rt]\033[0m %s\n' "$*" >&2; }

# ---- 0. root ----
if [[ $EUID -ne 0 ]]; then err "run as root (sudo bash $0)"; exit 1; fi

# ---- 1. is this a Raspberry Pi? ----
# Detect via the device-tree model (works on RPi OS AND plain Debian), with the
# os-release check as a fallback.
MODEL="$(cat /proc/device-tree/model 2>/dev/null | tr -d '\0' || true)"
[[ -z "$MODEL" ]] && MODEL="$(cat /sys/firmware/devicetree/base/model 2>/dev/null | tr -d '\0' || true)"
if ! printf '%s' "$MODEL" | grep -qi 'raspberry' && ! grep -qi 'raspberry' /etc/os-release 2>/dev/null; then
    err "does not look like a Raspberry Pi (model='${MODEL:-?}', no 'raspberry' in os-release)."
    err "Refusing to continue."
    exit 1
fi
log "Raspberry Pi: '${MODEL:-unknown}' ($(uname -m))"

# ---- 2. current kernel ----
CURR="$(uname -r)"
log "current kernel: $CURR"
SKIP_INSTALL=""
if uname -v | grep -qiE 'preempt[-_ ]?rt'; then
    log "already running a PREEMPT_RT kernel; skipping install (tuning + boot only)."
    SKIP_INSTALL=1
fi

# ---- 3. find + install the RPi RT kernel image ----
RT_PKG=""
RT_HDR=""
if [[ -z "$SKIP_INSTALL" ]]; then
    apt-get update -y || warn "apt-get update failed (offline?); trying install anyway"
    VER="${CURR%%+*}"   # e.g. 6.18.50 from 6.18.50+rpt-rpi-2712
    # Prefer an RT image matching the current base version; else the newest one.
    RT_PKG="$(apt-cache search --names-only 'linux-image' 2>/dev/null | awk '{print $1}' \
              | grep -E "^linux-image-${VER}\+rpt-rpi-[^ ]*-rt$" | head -1 || true)"
    if [[ -z "$RT_PKG" ]]; then
        warn "no RT image for base version $VER; using newest available rpi-*-rt"
        RT_PKG="$(apt-cache search --names-only 'linux-image' 2>/dev/null | awk '{print $1}' \
                  | grep -E '^linux-image-[0-9].*\+rpt-rpi-[^ ]*-rt$' | sort -V | tail -1 || true)"
    fi
    if [[ -z "$RT_PKG" ]]; then
        err "could not find an RPi RT kernel image (linux-image-*+rpt-rpi-*-rt)."
        err "  check the RPi apt repo is enabled (/etc/apt/sources.list.d/)."
        exit 1
    fi
    RT_HDR="${RT_PKG/linux-image/linux-headers}"
    log "installing RT kernel: $RT_PKG (+ $RT_HDR)"
    if ! DEBIAN_FRONTEND=noninteractive apt-get install -y "$RT_PKG" "$RT_HDR"; then
        err "could not install '$RT_PKG'."
        exit 1
    fi
else
    # Already on RT: locate the installed RT image (for the rollback note below).
    RT_PKG="$(dpkg-query -W -f='${Package}\n' 'linux-image-*rpi-*rt' 2>/dev/null | head -1 || true)"
fi

# ---- 4. PTP tuning (safe / idempotent) ----
log "applying PTP tuning (CPU governor + net sysctls)..."
if [[ -d /sys/devices/system/cpu/cpu0/cpufreq ]]; then
    for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        echo performance > "$g" 2>/dev/null || true
    done
    log "  cpu governor -> performance"
else
    warn "  no cpufreq sysfs; skipping governor"
fi
cat > /etc/sysctl.d/99-htsn-tpo.conf <<'SYSCTL'
# htsn PTP (ptpd grandmaster) - modest, safe network tuning
net.core.netdev_max_backlog = 16384
net.core.somaxconn = 8192
net.ipv4.udp_rmem_min = 8192
net.ipv4.udp_wmem_min = 8192
SYSCTL
sysctl --system >/dev/null 2>&1 || true
log "  /etc/sysctl.d/99-htsn-tpo.conf applied"

# ---- 5. make the RT kernel the default on next boot (RPi firmware boot) ----
# The RPi firmware reads /boot/firmware/config.txt (or /boot/config.txt) and loads
# the kernel named by 'kernel='. The RT image lands as /boot/firmware/*_rt.img.
CFG=""
[[ -f /boot/firmware/config.txt ]] && CFG=/boot/firmware/config.txt
[[ -z "$CFG" && -f /boot/config.txt ]] && CFG=/boot/config.txt
if [[ -n "$CFG" ]]; then
    RT_IMG="$(ls /boot/firmware/*_rt.img /boot/*_rt.img 2>/dev/null | head -1 | xargs -r basename || true)"
    if [[ -n "$RT_IMG" ]]; then
        cp -n "$CFG" "${CFG}.bak-rt" 2>/dev/null || true      # keep the first backup
        if grep -qE '^[[:space:]]*kernel=' "$CFG"; then
            sed -i -E "s|^[[:space:]]*kernel=.*|kernel=${RT_IMG}|" "$CFG"
        else
            printf '\nkernel=%s\n' "$RT_IMG" >> "$CFG"
        fi
        log "  $CFG -> kernel=$RT_IMG   (backup: ${CFG}.bak-rt)"
    else
        warn "  no *_rt.img found in /boot; set 'kernel=' in $CFG manually."
    fi
else
    warn "  no RPi config.txt found (GRUB boot?); select the -rt kernel in GRUB."
fi

# ---- 6. verify + rollback + next steps ----
cat <<EOF

------------------------------------------------------------------------
  Done. Next steps:
    1. Reboot to load the RT kernel:            sudo reboot
    2. Verify (expect 'PREEMPT_RT' in the version + CONFIG_PREEMPT_RT=y):
           uname -v
           grep -E '^CONFIG_PREEMPT_RT=' /boot/config-\$(uname -r)
    3. Grandmaster: ptpd on the wired NIC (run.sh start_tsn_cnc handles it),
       GM IP 192.168.1.10, endpoint 192.168.1.20.
  Rollback (stock kernel is untouched):
           # restore the original config, then reboot:
           cp ${CFG:-/boot/firmware/config.txt}.bak-rt ${CFG:-/boot/firmware/config.txt}
           # remove the RT image entirely, if desired:
           apt-get remove -y ${RT_PKG:-linux-image-*rpi-*-rt}
------------------------------------------------------------------------
EOF
log "NOT auto-rebooting (so you can inspect); reboot when ready."
