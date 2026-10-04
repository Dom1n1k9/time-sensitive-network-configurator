#!/usr/bin/env bash
# HTSN edge auto-update: git pull -> rebuild C core -> sync python services
# -> restart only when something actually changed.
# Runs unattended from htsn-update.timer. Needs HTSN_GH_TOKEN (in
# /etc/htsn/env) for the pull; without it the pull is skipped and the
# build/sync still runs.
set -u
REPO=/home/htsn/htsn-configurator
AI=/home/htsn/htsn-ai
LOG="$AI/update.log"
TS() { date +'%F %T'; }
say() { echo "[$(TS)] $*" | tee -a "$LOG"; }
sudo() { /usr/bin/sudo -n "$@"; }

cd "$REPO" || { say "ERROR: $REPO missing"; exit 1; }

CHANGED=0
OLD_HEAD=$(git rev-parse HEAD 2>/dev/null || echo none)

TOKEN="${HTSN_GH_TOKEN:-}"
if [ -n "$TOKEN" ]; then
    PULL_GIT=(-c credential.helper='!f() { echo username=x-access-token; echo password="$HTSN_GH_TOKEN"; }; f')
else
    PULL_GIT=()
fi
if git "${PULL_GIT[@]}" pull --ff-only origin main >>"$LOG" 2>&1; then
    NEW_HEAD=$(git rev-parse HEAD)
    if [ "$NEW_HEAD" != "$OLD_HEAD" ]; then
        say "git: $OLD_HEAD -> $NEW_HEAD"
        CHANGED=1
    else
        say "git: up to date"
    fi
else
    say "git: PULL FAILED - keeping current code (see $LOG)"
fi

if [ "$CHANGED" = "1" ]; then
    if cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >>"$LOG" 2>&1 \
        && cmake --build build -j4 >>"$LOG" 2>&1; then
        say "build: ok"
    else
        say "build: FAILED - services keep the old binary (see $LOG)"
    fi
    for f in vision_service.py policy_engine.py llm_bridge.py update.sh backup.sh; do
        [ -f "$REPO/rpi-ai/$f" ] && cp -f "$REPO/rpi-ai/$f" "$AI/$f"
    done
    say "synced rpi-ai/*.py -> $AI"
    FAIL=0
    for svc in htsn-cli htsn-webgui htsn-ai htsn-policy htsn-llm; do
        sudo systemctl restart "$svc" || FAIL=1
    done
    [ "$FAIL" = "0" ] && say "services restarted" || say "ERROR: some service restarts failed"
else
    say "no changes - nothing to do"
fi
