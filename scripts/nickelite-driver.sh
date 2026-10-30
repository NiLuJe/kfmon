#!/bin/sh
# The OTA stage1 contract selects recovery on success, so every path must fail.
set -eu
work=
trap 'if [ -n "$work" ]; then rm -rf "$work"; fi; sync; exit 1' 0
trap 'exit 1' 1 2 3 15
[ "${2-}" = stage1 ] || exit 1
[ -f "${1-}" ] || exit 1
work=$(mktemp -d /tmp/nickelite-kfmon-ota.XXXXXX)
tar -tf "$1" > "$work/members"
tar -tvf "$1" > "$work/types"
# Reject links, directories and special files before extracting anything.
awk 'substr($0, 1, 1) != "-" { bad = 1 } END { exit bad }' "$work/types"
printf '%s\n' driver.sh nickelite-manage.sh nickelite-kfmon.tgz > "$work/expected"
if cmp -s "$work/expected" "$work/members"; then
    mode=--install
else
    printf '%s\n' driver.sh nickelite-manage.sh > "$work/expected"
    cmp -s "$work/expected" "$work/members"
    mode=--uninstall
fi
mkdir "$work/payload"
tar -xf "$1" -C "$work/payload"
if [ "$mode" = --install ]; then
    sh "$work/payload/nickelite-manage.sh" --install "$work/payload/nickelite-kfmon.tgz"
else
    sh "$work/payload/nickelite-manage.sh" --uninstall
fi
echo 'Nickelite KFMon stage1 applied; exiting nonzero to select the normal reboot path.' >&2
