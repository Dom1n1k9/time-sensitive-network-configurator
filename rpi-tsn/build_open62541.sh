#!/usr/bin/env bash
# Build open62541 v1.5.9 (if not already) + the rpi-tsn C binaries, on the RPi.
#
# The rpi-tsn C code (cnc_opcua server, tsn_opcua_link poller, tsn_opcua_bridge,
# htsn_opcua_cli) links against open62541. This script clones a pinned release,
# builds/installs it to a prefix, then builds the binaries against it.
#
# Usage:  bash rpi-tsn/build_open62541.sh [PFX]
#   PFX   install prefix (default: $HOME/o62541-prefix)
#
# The matching run command expects the same prefix:
#   HTSN_O62541_PFX=$PFX   (see run.sh start_tsn_cnc / the "not built" hint)
set -e
PFX="${1:-$HOME/o62541-prefix}"
VER="v1.5.9"
SRC="$HOME/o62541-src"
BUILD="$HOME/o62541-build"
JOBS="$(nproc)"

echo "==> open62541 prefix: $PFX   (release $VER, -j$JOBS)"

# 1) source (shallow, pinned tag)
if [ ! -d "$SRC/.git" ]; then
    echo "==> cloning open62541 $VER -> $SRC"
    git clone --depth 1 --branch "$VER" https://github.com/open62541/open62541 "$SRC"
fi

# 2) configure + build + install (skip if the shared lib is already there)
if [ ! -e "$PFX/lib/libopen62541.so" ]; then
    echo "==> configuring open62541 (Release, subscribers ON, pubsub ON, encryption OFF, ns0 REDUCED)"
    cmake -S "$SRC" -B "$BUILD" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PFX" \
        -DUA_ARCHITECTURE=posix \
        -DUA_ENABLE_ENCRYPTION=OFF \
        -DUA_ENABLE_SUBSCRIBERS=ON \
        -DUA_ENABLE_PUBSUB=ON \
        -DUA_ENABLE_DISCOVERY=OFF \
        -DUA_NAMESPACE_ZERO=REDUCED \
        -DBUILD_SHARED_LIBS=ON
    echo "==> building open62541 (slow on the RPi; this is the long step)"
    cmake --build "$BUILD" -j"$JOBS"
    echo "==> installing to $PFX"
    cmake --install "$BUILD"
else
    echo "==> open62541 already installed at $PFX - skipping"
fi

# 3) build the rpi-tsn binaries against the prefix
echo "==> building rpi-tsn binaries (PFX=$PFX)"
cd "$(dirname "$0")"
make clean >/dev/null 2>&1 || true
make PFX="$PFX"

echo
echo "==> done. Binaries in $(dirname "$0"):"
ls -1 cnc_opcua tsn_opcua_link tsn_opcua_bridge htsn_opcua_cli test_opcua_client
echo
echo "==> start the data plane (server + poller + bridge), e.g.:"
echo "    HTSN_O62541_PFX=$PFX bash run.sh --headless     # or start the 3 binaries directly"
