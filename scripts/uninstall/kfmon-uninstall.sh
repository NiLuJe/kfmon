#!/bin/sh

# v0.9.5: We're using an on-animator patch, instead of an udev rule (see #2).
rm -f "/etc/udev/rules.d/99-kfmon.rules"

# NOTE: Make sure we're not running, first...
pkill -TERM kfmon

# Uninstall: blow the full directroy tree away
rm -rf "/usr/local/kfmon"

# Also remove the kfmon-ipc symlink
rm -f "/usr/bin/kfmon-ipc"

# Kobo v5 startscript
rm -rf "/etc/rcS.d/S99kfmon"
rm -rf "/etc/init.d/kfmon"

# NOTE: Delete our own icons & configs, but leave whatever else the user might have created alone.
#       List based on what the full OCP deploys.
for my_file in ".adds/kfmon/config/kfmon.ini" ".adds/kfmon/config/kfmon-log.ini" ".adds/kfmon/config/koreader.ini" ".adds/kfmon/config/plato.ini" ".adds/kfmon/bin/kfmon-printlog.sh" ".adds/kfmon/log/kfmon_dump.log" ".adds/nm/kfmon" ".adds/nm/koreader" ".adds/nm/plato" ".adds/nm/doc" "icons/plato.png" "koreader.png" "kfmon.png" ; do
	my_file="/mnt/onboard/${my_file}"
	[ -f "${my_file}" ] && rm -f "${my_file}"
done

# Remove the directory if it's empty.
# NOTE: This'll trip NickelMenu's self-uninstall ;).
for my_dir in ".adds/kfmon/log" ".adds/kfmon/config" ".adds/kfmon/bin" ".adds/kfmon" ".adds/nm" "icons" ; do
	my_dir="/mnt/onboard/${my_dir}"
	# Because we can't have nice things, the stock busybox is too old to support -empty in find...
	[ -d "${my_dir}" ] && [ -z "$(ls -A -- "${my_dir}")" ] && rm -rf "${my_dir}"
done

exit 0

