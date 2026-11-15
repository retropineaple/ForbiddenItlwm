#!/bin/bash
# Validate an OpenCore config.plist with the ocvalidate saved next to this script.
#   bash tools/oc/ocvalidate.sh [path/to/config.plist]     (default: /Volumes/EFI/EFI/OC/config.plist)
# ocvalidate is from OpenCorePkg 1.0.8 (OpenCore-1.0.8-RELEASE.zip, Utilities/ocvalidate/ocvalidate, macOS x86_64).
# Use the version that matches the OpenCore.efi on the EFI; the check is only meaningful when they match.
# Release zip sha256: 2011e8b7216ecb2645d97ea710df965947edd406846e92050b9bc4190be6d27b
set -u
here="$(cd "$(dirname "$0")" && pwd)"
want="c76f907d5caad299889a2314ae68f03cd8538ad53f65a3c2c7c95ae0d0bb0b38"
cfg="${1:-/Volumes/EFI/EFI/OC/config.plist}"
[ -f "$cfg" ] || { echo "no config at $cfg (is the EFI mounted?)"; exit 2; }
got="$(shasum -a 256 "$here/ocvalidate" | cut -d' ' -f1)"
[ "$got" = "$want" ] || { echo "tools/oc/ocvalidate does not match the saved checksum"; exit 2; }
exec "$here/ocvalidate" "$cfg"
