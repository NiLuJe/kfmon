#!/bin/sh
# Kobo v5 update script, gets called by OTA update script.

set -e
# full path to update.tar
ARCHIVE="$1"

tar -C /tmp/updater -xf "${ARCHIVE}" "kfmon.tgz"
tar -C / -xf /tmp/updater/kfmon.tgz
