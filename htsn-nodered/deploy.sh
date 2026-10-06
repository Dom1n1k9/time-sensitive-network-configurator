#!/usr/bin/env bash
# Deploy Node-RED flows to the RPi.
# Usage: ./deploy.sh [phases...]
#   (no args)    - deploy the base flow (27 nodes)
#   phase1       - also merge the Phase 1 device management nodes
#   phase2       - also merge the Phase 2 TSN config pages
#   phase1 phase2 - merge both
set -euo pipefail

RPI_USER="${RPI_USER:-wtsn}"
RPI_PASS="${RPI_PASS:-wtsn}"
RPI_IP="${RPI_IP:-192.168.0.183}"
RPI_FLOW="/home/${RPI_USER}/htsn-nodered/flows.json"
BASE_DIR="$(cd "$(dirname "$0")" && pwd)"

SSH_OPTS="-o StrictHostKeyChecking=no -o KexAlgorithms=+diffie-hellman-group14-sha1,diffie-hellman-group1-sha1 -o HostKeyAlgorithms=+ssh-rsa,ssh-dss -o PubkeyAcceptedAlgorithms=+ssh-rsa -o Ciphers=+aes128-cbc -o MACs=+hmac-sha1 -o ConnectTimeout=10"

echo "==> Checking RPi reachability (${RPI_IP})..."
if ! sshpass -p "$RPI_PASS" ssh $SSH_OPTS "${RPI_USER}@${RPI_IP}" "echo ok" >/dev/null 2>&1; then
    echo "ERROR: RPi ${RPI_IP} is unreachable. Try Tailscale IP."
    exit 1
fi
echo "    OK"

echo "==> Installing node-red-contrib-sqlite (if not present)..."
sshpass -p "$RPI_PASS" ssh $SSH_OPTS "${RPI_USER}@${RPI_IP}" "
    cd /home/${RPI_USER}/htsn-nodered
    if [ ! -d node_modules/node-red-contrib-sqlite ]; then
        echo '    Installing node-red-contrib-sqlite...'
        npm install node-red-contrib-sqlite --no-audit --no-fund 2>&1 | tail -3
    else
        echo '    Already installed'
    fi
"

PHASES=("$@")

echo "==> Building flow..."
WORKDIR=$(mktemp -d)
trap "rm -rf $WORKDIR" EXIT

cp "$BASE_DIR/flows.json" "$WORKDIR/flow.json"

merge_phase() {
    local phase_file="$1"
    local phase_label="$2"
    if [ -f "$BASE_DIR/$phase_file" ]; then
        echo "    Merging ${phase_label} nodes..."
        python3 -c "
import json
base = json.load(open('$WORKDIR/flow.json'))
extra = json.load(open('$BASE_DIR/$phase_file'))
existing_ids = {n['id'] for n in base}
added = 0
for n in extra:
    if n['id'] not in existing_ids:
        base.append(n)
        added += 1
json.dump(base, open('$WORKDIR/flow.json', 'w'), indent=4)
print(f'    Added {added} nodes (total: {len(base)})')
"
    fi
}

for p in "${PHASES[@]}"; do
    case "$p" in
        phase1) merge_phase "phase1-devices.json" "Phase 1 (devices)" ;;
        phase2) merge_phase "phase2-tsn-config.json" "Phase 2 (TSN config)" ;;
        phase3) merge_phase "phase3-monitoring.json" "Phase 3 (monitoring)" ;;
        phase7) merge_phase "phase7-mqtt-backend.json" "Phase 7 (MQTT backend)" ;;
        *) echo "    Unknown phase: $p" ;;
    esac
done

echo "==> Uploading flow to RPi..."
sshpass -p "$RPI_PASS" scp $SSH_OPTS "$WORKDIR/flow.json" "${RPI_USER}@${RPI_IP}:${RPI_FLOW}"

echo "==> Restarting Node-RED..."
sshpass -p "$RPI_PASS" ssh $SSH_OPTS "${RPI_USER}@${RPI_IP}" "
    sudo systemctl restart htsn-nodered
    sleep 2
    systemctl is-active htsn-nodered
    journalctl -u htsn-nodered --since '5 sec ago' --no-pager | tail -5
"

echo "==> Done. Dashboard: http://${RPI_IP}:1880/"
