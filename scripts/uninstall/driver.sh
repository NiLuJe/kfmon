#!/bin/sh
# Kobo v5 update script, gets called by OTA update script.

set -e
# full path to update.tar
ARCHIVE="$1"
# called with stage1 first. If the script returns zero, the device reboots to recovery and this same script is called with stage2
# to avoid a reboot to recovery, we always return non-zero (the reboot will happen anyway)
STAGE="$2"

mkdir -p /tmp/updater

case $STAGE in
	stage1)
		# pull the actual script we want to run from the archive, and run it from here, making packaging multiple tools in one updater easy
		tar -C /tmp/updater -xf "${ARCHIVE}" "kfmon-uninstall.sh" "animator.sh"
		/tmp/updater/kfmon-uninstall.sh
		# restore the stock boot anim
		cp -f "/tmp/updater/animator.sh" "/usr/bin/animator.sh"
		chmod a+x "/usr/bin/animator.sh"
	;;
	stage2)
	;;
esac

sync

exit 1
