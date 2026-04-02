#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
EDK2_DIR="$ROOT_DIR/edk2"
DIST_DIR="$ROOT_DIR/dist"

TOOLCHAIN="${TOOLCHAIN:-CLANG35}"
BUILD_TYPE="${BUILD_TYPE:-RELEASE}"
ARCH="${ARCH:-AARCH64}"

if [[ ! -d "$EDK2_DIR" ]]; then
  echo "[ERR] edk2 directory not found: $EDK2_DIR"
  exit 1
fi

mkdir -p "$DIST_DIR"

pushd "$EDK2_DIR" >/dev/null

# Reuse repository Conf templates when available.
if [[ -d "$ROOT_DIR/Conf" ]]; then
  mkdir -p "$EDK2_DIR/Conf"
  cp -f "$ROOT_DIR/Conf"/* "$EDK2_DIR/Conf/" || true
fi

# edksetup.sh probes several variables directly; avoid nounset breakage.
export WORKSPACE="$EDK2_DIR"

# shellcheck disable=SC1091
set +u
source ./edksetup.sh
set -u

echo "[INFO] Building BaseTools GenFw..."
make -C BaseTools/Source/C/GenFw

build \
  -p TetrisPkg/TetrisPkg.dsc \
  -a "$ARCH" \
  -b "$BUILD_TYPE" \
  -t "$TOOLCHAIN"

EFI_OUT="Build/TetrisPkg/${BUILD_TYPE}_${TOOLCHAIN}/${ARCH}/Tetris.efi"
if [[ ! -f "$EFI_OUT" ]]; then
  echo "[ERR] Build finished but EFI not found: $EFI_OUT"
  exit 1
fi

cp -f "$EFI_OUT" "$DIST_DIR/Tetris.efi"

echo "[OK] Built: $EDK2_DIR/$EFI_OUT"
echo "[OK] Copied: $DIST_DIR/Tetris.efi"

popd >/dev/null
