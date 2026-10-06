#!/usr/bin/env bash
# One-time RPi setup for the Node-RED HTSN dashboard.
# Run this ONCE on the RPi before using deploy.sh.
#   scp setup.sh snapshot.py wtsn@<rpi>:~/htsn-nodered/
#   ssh wtsn@<rpi> "cd ~/htsn-nodered && bash setup.sh"
set -euo pipefail

RPI_USER="${RPI_USER:-wtsn}"
NRED_DIR="/home/${RPI_USER}/htsn-nodered"

echo "==> Setting up Node-RED HTSN dashboard on $(hostname)..."

# 1. Node-RED must be installed
if ! command -v node-red &>/dev/null; then
    echo "    Node-RED not found. Installing globally..."
    sudo npm install -g node-red --no-audit --no-fund
fi
echo "    Node-RED: $(node-red --version 2>/dev/null || echo 'installed')"

# 2. Create the Node-RED user dir if missing
mkdir -p "$NRED_DIR"

# 3. Install the SQLite node
cd "$NRED_DIR"
if [ ! -d node_modules/node-red-contrib-sqlite ]; then
    echo "    Installing node-red-contrib-sqlite..."
    npm init -y &>/dev/null 2>&1 || true
    npm install node-red-contrib-sqlite --no-audit --no-fund 2>&1 | tail -3
else
    echo "    node-red-contrib-sqlite already installed"
fi

# 4. Copy the snapshot helper script
if [ -f "$(dirname "$0")/snapshot.py" ]; then
    cp "$(dirname "$0")/snapshot.py" "$NRED_DIR/snapshot.py"
    chmod +x "$NRED_DIR/snapshot.py"
    echo "    snapshot.py installed"
fi

# 5. Verify the SQLite DB exists (created by the Python GUI)
DB="/home/${RPI_USER}/wtsn-configurator/build/htsn_gui.db"
if [ -f "$DB" ]; then
    echo "    SQLite DB found: $DB ($(du -h "$DB" | cut -f1))"
else
    echo "    WARNING: SQLite DB not found at $DB"
    echo "    The Python GUI must run at least once to create it."
fi

# 6. Verify the Node-RED systemd service exists
if systemctl list-unit-files | grep -q htsn-nodered; then
    echo "    htsn-nodered.service already installed"
else
    echo "    Creating htsn-nodered.service..."
    cat > /tmp/htsn-nodered.service <<EOF
[Unit]
Description=Node-RED HTSN Dashboard
After=network.target

[Service]
Type=simple
User=${RPI_USER}
WorkingDirectory=${NRED_DIR}
ExecStart=$(command -v node-red) --userDir ${NRED_DIR}
Restart=on-failure
RestartSec=5
Environment=NODE_ENV=production

[Install]
WantedBy=multi-user.target
EOF
    sudo cp /tmp/htsn-nodered.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable htsn-nodered
    echo "    htsn-nodered.service created + enabled"
fi

# 7. Verify the OPC UA server + poller services
for svc in htsn-cnc htsn-poller wtsn-webgui; do
    if systemctl list-unit-files | grep -q "$svc"; then
        echo "    $svc.service: installed"
    else
        echo "    WARNING: $svc.service not installed"
    fi
done

echo ""
echo "==> Setup complete."
echo "    Next: bash deploy.sh all   (to deploy the full flow)"
echo "    Dashboard: http://<rpi-ip>:1880/"
