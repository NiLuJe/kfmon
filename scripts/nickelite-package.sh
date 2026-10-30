#!/bin/sh
# Host-side packaging; the cross-built upstream binaries are reused unchanged.
set -eu
cd "$(dirname "$0")/.."
for binary in Release/kfmon Release/shim Release/kfmon-ipc FBInk/Release/fbink; do
    [ -s "$binary" ] || { echo "Missing cross-built binary: $binary" >&2; exit 1; }
    readelf -h "$binary" | grep -q 'Machine:.*ARM' || { echo "Not an ARM binary: $binary" >&2; exit 1; }
done
mkdir -p Release/Nickelite
work=$(mktemp -d "$PWD/Release/Nickelite/.stage.XXXXXX")
trap 'rm -rf "$work"' 0
trap 'exit 1' 1 2 3 15
mkdir "$work/payload" "$work/install" "$work/uninstall" "$work/defaults"
cp Release/kfmon Release/shim Release/kfmon-ipc FBInk/Release/fbink "$work/payload/"
cp scripts/kfmon "$work/payload/kfmon.init"
cp scripts/nickelite-manage.sh LICENSE CREDITS "$work/payload/"
cp docs/NICKELITE.md "$work/payload/README.md"
printf '\nUpstream build source: %s (%s)\n' "$(git describe --always)" "$(git rev-parse HEAD)" >> "$work/payload/README.md"
tar --owner=root --group=root -czf "$work/nickelite-kfmon.tgz" -C "$work/payload" kfmon shim kfmon-ipc fbink kfmon.init nickelite-manage.sh README.md LICENSE CREDITS
for kind in install uninstall; do
    cp scripts/nickelite-driver.sh "$work/$kind/driver.sh"
    cp scripts/nickelite-manage.sh "$work/$kind/"
    mkdir "$work/$kind/.kobo"
done
cp "$work/nickelite-kfmon.tgz" "$work/install/"
tar --owner=root --group=root -cf "$work/install/.kobo/update.tar" -C "$work/install" driver.sh nickelite-manage.sh nickelite-kfmon.tgz
tar --owner=root --group=root -cf "$work/uninstall/.kobo/update.tar" -C "$work/uninstall" driver.sh nickelite-manage.sh
mkdir -p "$work/defaults/Nickelite-KFMon-defaults/config"
cp config/kfmon.ini "$work/defaults/Nickelite-KFMon-defaults/config/"
cp scripts/nickelite-defaults.sh "$work/defaults/Nickelite-KFMon-defaults/"
# Fresh ZIPs avoid retaining stale entries from an earlier build.
(cd "$work/install" && zip -q "$work/KFMon-Nickelite-Install.zip" .kobo/update.tar)
(cd "$work/uninstall" && zip -q "$work/KFMon-Nickelite-Uninstall.zip" .kobo/update.tar)
(cd "$work/defaults" && zip -qr "$work/KFMon-Nickelite-Defaults.zip" Nickelite-KFMon-defaults)
for kind in Install Uninstall Defaults; do
    mv "$work/KFMon-Nickelite-$kind.zip" "Release/Nickelite/KFMon-Nickelite-$kind.zip"
done
