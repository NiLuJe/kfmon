#!/bin/sh

# v0.9.5: We're using an on-animator patch, instead of an udev rule (see #2).
rm -f "/etc/udev/rules.d/99-kfmon.rules"

# NOTE: Make sure we're not running, first...
pkill kfmon

# Uninstall: blow the full directroy tree away
rm -rf "/usr/local/kfmon"

# Also remove the kfmon-ipc symlink
rm -f "/usr/bin/kfmon-ipc"

# NOTE: We're leaving /mnt/onboard/kfmon.png & /mnt/onboard/koreader.png in,
#       as well as basically whatever's in onboard, because that might contain user-created content,
#       so, not our place to delete it.

exit 0

