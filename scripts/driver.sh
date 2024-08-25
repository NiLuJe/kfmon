# Kobo v5 update script, gets called by OTA update script.

set -e
# full path to update.tar
ARCHIVE="$1"
# called with stage1 first. If the script returns zero, reboot to recovery and called with stage2.
# to avoid reboot to recovery, return non-zero (reboot will happen anyway)
STAGE="$2"

mkdir -p /tmp/updater

case $STAGE in
    stage1)
    # use run actual installer from script, so packaging multiple tools in one updater is easy
    tar -C /tmp/updater -xf "${ARCHIVE}" "kfmon-install.sh"
    /tmp/updater/kfmon-install.sh "${ARCHIVE}"
    ;;
    stage2)
    ;;
esac
sync

exit 1