#!/usr/bin/env bash
# One-shot deploy on the RPi. Preferred (non-interactive) invocation:
#   echo wtsn | sudo -S bash /tmp/htsn-deploy/htsn_deploy.sh
# (scp this dir to /tmp/htsn-deploy first). Works as root or wtsn.
#
# Deploys: (1) asyncua, (2) OPC UA subscription client + 802.1Q TSN config,
# (3) htsn-opcua-sub service (drives PublishRequest/Response), (4) 802.1Q
# persistence drop-in (re-applies TSN config on every cnc_opcua start/restart),
# (5) re-apply TSN config now, (6) verify.
set -uo pipefail

STAGE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI=/home/wtsn/wtsn-configurator/rpi-tsn/htsn_opcua_cli
DEST=/home/wtsn/htsn-opcua-sub
TSN=/etc/htsn/tsn_cfg.json
SUDO="$( [ "$(id -u)" = 0 ] && echo "" || echo sudo )"

step(){ printf '\n== %s ==\n' "$*"; }

step "1/6 asyncua (Python OPC UA client lib)"
if python3 -c 'import asyncua' 2>/dev/null; then
  echo "already present"
else
  python3 -m pip install --user --break-system-packages asyncua || { echo "pip install failed"; exit 1; }
fi
python3 -c 'import asyncua; print("asyncua", asyncua.__version__)'

step "2/6 client + TSN config files"
$SUDO install -d -o wtsn -g wtsn "$DEST"
$SUDO install -o wtsn -g wtsn -m 0644 "$STAGE/htsn_sub_client.py" "$DEST/htsn_sub_client.py"
$SUDO install -d /etc/htsn
$SUDO install -m 0644 "$STAGE/tsn_cfg.json" "$TSN"

step "3/6 systemd service (htsn-opcua-sub -> drives publish exchange)"
$SUDO install -m 0644 "$STAGE/htsn-opcua-sub.service" /etc/systemd/system/htsn-opcua-sub.service
$SUDO systemctl daemon-reload
$SUDO systemctl enable htsn-opcua-sub.service
$SUDO systemctl restart htsn-opcua-sub.service
sleep 4
echo "  active: $($SUDO systemctl is-active htsn-opcua-sub.service)"
$SUDO journalctl -u htsn-opcua-sub -n 6 --no-pager | sed 's/^/    /'

step "4/6 802.1Q persistence (drop-in on htsn-cnc.service)"
if [ -f /etc/systemd/system/htsn-cnc.service ]; then
  $SUDO install -d /etc/systemd/system/htsn-cnc.service.d
  $SUDO install -m 0644 "$STAGE/htsn-cnc-tsn-apply.conf" /etc/systemd/system/htsn-cnc.service.d/tsn-apply.conf
  if [ -f "$STAGE/htsn-cnc-uadp-vlan.conf" ]; then
    $SUDO install -m 0644 "$STAGE/htsn-cnc-uadp-vlan.conf" /etc/systemd/system/htsn-cnc.service.d/uadp-vlan.conf
  fi
  if [ -f "$STAGE/htsn-cnc-zz-kill.conf" ]; then
    $SUDO install -m 0644 "$STAGE/htsn-cnc-zz-kill.conf" /etc/systemd/system/htsn-cnc.service.d/zz-kill.conf
  fi
  # Stop/restart the data-plane clients together with cnc_opcua, so a
  # `systemctl restart htsn-cnc` brings bridge+poller back automatically.
  for u in htsn-bridge htsn-poller; do
    if [ -f "/etc/systemd/system/$u.service" ] && [ -f "$STAGE/$u-partof.conf" ]; then
      $SUDO install -d "/etc/systemd/system/$u.service.d"
      $SUDO install -m 0644 "$STAGE/$u-partof.conf" "/etc/systemd/system/$u.service.d/partof.conf"
    fi
  done
  $SUDO systemctl daemon-reload
  echo "  installed: re-applies TSN config, routes UADP onto VLAN 100, guards teardown, couples bridge/poller"
else
  echo "  WARN: /etc/systemd/system/htsn-cnc.service not found; skipping drop-in"
fi

step "5/6 re-apply 802.1Q TSN config now (node 51 -> endpoint)"
if [ -x "$CLI" ]; then
  echo "  $CLI write-json cmd_tsn_config $TSN"
  "$CLI" write-json cmd_tsn_config "$TSN"
else
  echo "  WARN: $CLI not found; skipping (endpoint may stay untagged)"
fi

step "6/6 verify (publish exchange lo:4840 + vlan/ptp/uadp)"
if command -v tshark >/dev/null 2>&1; then
  echo "  -- protocol mix (8s) --"
  $SUDO tshark -i any -a duration:8 -Q -Y 'tcp.port==4840 or udp.port==8899 or vlan or ptp' \
    -T fields -e frame.protocols 2>/dev/null | tr ',' '\n' | grep -v '^$' \
    | sort | uniq -c | sort -rn | head -15
  echo "  -- publish exchange on lo:4840 (PublishReq/Resp count) --"
  $SUDO tshark -i any -a duration:8 -Q -Y 'tcp.port==4840 and (opcua.service==10 or opcua.service==11)' \
    2>/dev/null | wc -l
else
  echo "  tshark not installed; verify in the noVNC Wireshark (filter: tcp.port==4840)"
fi

echo
echo "DONE. Open the noVNC Wireshark (Display filter: tcp.port==4840) and look for"
echo "OPC UA 'PublishRequest'/'PublishResponse' on lo, plus vlan/ptp on eth0."
echo "802.1Q will now survive cnc_opcua restarts (drop-in re-applies it)."
