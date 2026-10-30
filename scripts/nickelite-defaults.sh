#!/bin/sh
# Run on the reader only when a default main config is wanted.
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for dir in /mnt /mnt/onboard /mnt/onboard/.adds /mnt/onboard/.adds/kfmon /mnt/onboard/.adds/kfmon/config; do
    [ ! -L "$dir" ] || { echo "Refusing symlink directory: $dir" >&2; exit 1; }
    if [ -e "$dir" ]; then [ -d "$dir" ] || exit 1; else mkdir "$dir"; fi
done
config=/mnt/onboard/.adds/kfmon/config/kfmon.ini
if [ -e "$config" ] || [ -L "$config" ]; then
    echo 'Existing kfmon.ini preserved.'
else
    # Noclobber also prevents overwriting a file created since the check.
    (set -C; cat "$source_dir/config/kfmon.ini" > "$config")
fi
