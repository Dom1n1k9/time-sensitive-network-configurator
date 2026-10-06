#!/usr/bin/env bash
# Deploy Node-RED HTSN flow to the RPi.
#
# Usage:
#   ./deploy.sh              - deploy base flow (27 nodes, TSN config + telemetry)
#   ./deploy.sh all          - deploy full flow (218 nodes, all 7 phases)
#   ./deploy.sh phase1 ...   - deploy base + selected phases
#
# First-time setup: run setup.sh on the RPi (see setup.sh)
set -euo pipefail

RPI_USER="${RPI_USER:-wtsn}"
RPI_PASS="${RPI_PASS:-wtsn}"
RPI_IP="${RPI_IP:-192.168.0.183}"
RPI_NRED="/home/${RPI_USER}/htsn-nodered"
RPI_FLOW="${RPI_NRED}/flows.json"
BASE_DIR="$(cd "$(dirname "$0")" && pwd)"

SSH_OPTS="-o StrictHostKeyChecking=no -o KexAlgorithms=+diffie-hellman-group14-sha1,diffie-hellman-group1-sha1 -o HostKeyAlgorithms=+ssh-rsa,ssh-dss -o PubkeyAcceptedAlgorithms=+ssh-rsa -o Ciphers=+aes128-cbc -o MACs=+hmac-sha1 -o ConnectTimeout=10"

echo "==> Checking RPi reachability (${RPI_IP})..."
if ! sshpass -p "$RPI_PASS" ssh $SSH_OPTS "${RPI_USER}@${RPI_IP}" "echo ok" >/dev/null 2>&1; then
    echo "ERROR: RPi ${RPI_IP} unreachable. Try: RPI_IP=<tailscale-ip> $0 $*"
    exit 1
fi
echo "    OK"

PHASES=("$@")
USE_PRODUCTION=false
[[ " ${PHASES[*]:-} " =~ \ all\  ]] && USE_PRODUCTION=true

WORKDIR=$(mktemp -d)
trap "rm -rf $WORKDIR" EXIT

echo "==> Building flow..."
if $USE_PRODUCTION; then
    cp "$BASE_DIR/flow-production.json" "$WORKDIR/flow.json"
    echo "    Using pre-built production flow ($(python3 -c "import json;print(len(json.load(open('$WORKDIR/flow.json'))))" 2>/dev/null || echo '?') nodes)"
else
    cp "$BASE_DIR/flows.json" "$WORKDIR/flow.json"
    merge_phase() {
        local pf="$1" pl="$2"
        [ -f "$BASE_DIR/$pf" ] || return
        echo "    Merging ${pl}..."
        python3 -c "
import json
b=json.load(open('$WORKDIR/flow.json')); e=json.load(open('$BASE_DIR/$pf'))
ids={n['id'] for n in b}; a=0
for n in e:
    if n['id'] not in ids: b.append(n); a+=1
json.dump(b,open('$WORKDIR/flow.json','w'),indent=4)
print(f'    +{a} nodes (total {len(b)})')
"
    }
    for p in "${PHASES[@]}"; do
        case "$p" in
            phase1) merge_phase "phase1-devices.json" "Phase 1 (devices)" ;;
            phase2) merge_phase "phase2-tsn-config.json" "Phase 2 (TSN config)" ;;
            phase3) merge_phase "phase3-monitoring.json" "Phase 3 (monitoring)" ;;
            phase4) merge_phase "phase4-endpoint-arch.json" "Phase 4 (endpoint+arch)" ;;
            phase5) merge_phase "phase5-firmware.json" "Phase 5 (firmware)" ;;
            phase6) merge_phase "phase6-ai-settings.json" "Phase 6 (AI+settings)" ;;
            phase7) merge_phase "phase7-mqtt-backend.json" "Phase 7 (MQTT backend)" ;;
            all) ;;
            *) echo "    Unknown: $p" ;;
        esac
    done
fi

echo "==> Uploading flow + helpers..."
sshpass -p "$RPI_PASS" scp $SSH_OPTS "$WORKDIR/flow.json" "${RPI_USER}@${RPI_IP}:${RPI_FLOW}"
if [ -f "$BASE_DIR/snapshot.py" ]; then
    sshpass -p "$RPI_PASS" scp $SSH_OPTS "$BASE_DIR/snapshot.py" "${RPI_USER}@${RPI_IP}:${RPI_NRED}/snapshot.py"
fi

echo "==> Restarting Node-RED..."
sshpass -p "$RPI_PASS" ssh $SSH_OPTS "${RPI_USER}@${RPI_IP}" "
    sudo systemctl restart htsn-nodered 2>/dev/null || echo '    (htsn-nodered service not running - start manually)'
    sleep 2
    if systemctl is-active htsn-nodered &>/dev/null; then
        echo '    Service: active'
    else
        echo '    Service: NOT active (run setup.sh first)'
    fi
    journalctl -u htsn-nodered --since '5 sec ago' --no-pager 2>/dev/null | tail -5
"

echo "==> Done. Dashboard: http://${RPI_IP}:1880/"
