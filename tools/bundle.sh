#!/usr/bin/env bash
# Package the three built plugins for one platform into a release bundle.
# Usage: tools/bundle.sh <tag> <rack-arch: lin-x64|win-x64|mac-arm64|mac-x64>
# Run after `make dist` in vcv/otislab, vcv/vastish and vcv/spoonbender.
set -euo pipefail
tag=$1; arch=$2
case "$arch" in
  lin-x64)   label=Linux-x64;          folder=plugins-lin-x64 ;;
  win-x64)   label=Windows-x64;        folder=plugins-win-x64 ;;
  mac-arm64) label=macOS-AppleSilicon; folder=plugins-mac-arm64 ;;
  mac-x64)   label=macOS-Intel;        folder=plugins-mac-x64 ;;
  *) echo "unknown arch $arch" >&2; exit 1 ;;
esac
name="DSP_Emulation-$tag-$label"
out="bundle/$name"; rm -rf bundle; mkdir -p "$out"
for p in otislab vastish spoonbender; do
  set -- vcv/$p/dist/*-"$arch".vcvplugin
  [ $# -eq 1 ] && [ -f "$1" ] || { echo "::error::expected one $arch package in vcv/$p/dist, found: $*" >&2; exit 1; }
  cp "$1" "$out/"
done
files=$(cd "$out" && ls *.vcvplugin)
{
  echo "DSP_Emulation $tag for VCV Rack 2 ($label)"
  echo
  echo "Files:"
  for f in $files; do echo "  $f"; done
  echo
  echo "Install:"
  echo "  1. In Rack choose Help > Open user folder, then quit Rack."
  echo "  2. Copy the three .vcvplugin files above, unopened, into the $folder folder there."
  echo "  3. Start Rack. The modules are OTIS Sampler (OtisLab), Vastish (VastishLab) and"
  echo "     Spoon Bender (SpoonBenderLab)."
  echo
  echo "Manuals: https://github.com/JosiahAlanLee/DSP_Emulation#readme"
  [ "$arch" = lin-x64 ] || { echo; echo "This platform's build has been compiled and unit-tested in CI but not yet run inside Rack. Reports welcome:"; echo "https://github.com/JosiahAlanLee/DSP_Emulation/issues"; }
} > "$out/INSTALL.txt"
(cd bundle && zip -qr "$name.zip" "$name")
echo "bundle/$name.zip"; ls -l "$out"
