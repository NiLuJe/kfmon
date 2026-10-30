#!/bin/sh
# Nickelite owns only these fixed system files; userstore is never touched.
set -eu
marker=/usr/local/kfmon/.nickelite-owned-v1
files='/etc/init.d/kfmon
/usr/local/kfmon/bin/kfmon
/usr/local/kfmon/bin/shim
/usr/local/kfmon/bin/kfmon-ipc
/usr/local/kfmon/bin/fbink
/usr/local/kfmon/bin/nickelite-manage.sh
/usr/local/kfmon/README.md
/usr/local/kfmon/LICENSE
/usr/local/kfmon/CREDITS'
fail() { echo "Nickelite KFMon: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || fail 'root is required'
case ${1-} in --install|--uninstall) ;; *) fail 'use --install PAYLOAD or --uninstall' ;; esac
work=$(mktemp -d /tmp/nickelite-kfmon.XXXXXX)
owned=false
rollback=false
touched=
service_link=false
ipc_link=false
marker_temp=
recovery=
cleanup() {
    status=$?
    trap - 0
    set +e
    restore_failed=false
    if [ -n "$marker_temp" ]; then rm -f "$marker_temp" || restore_failed=true; fi
    if [ "$rollback" = true ]; then
        for file in $touched; do
            if [ "$owned" = true ]; then
                cp -p "$recovery/backup$file" "$file" || restore_failed=true
            else
                rm -f "$file" || restore_failed=true
            fi
        done
        if [ "$service_link" = true ]; then rm -f /etc/rcS.d/S99kfmon || restore_failed=true; fi
        if [ "$ipc_link" = true ]; then rm -f /usr/bin/kfmon-ipc || restore_failed=true; fi
        if [ "$owned" = true ]; then
            cp -p "$recovery/backup-marker" "$marker" || restore_failed=true
        else
            rm -f "$marker" || restore_failed=true
            rmdir /usr/local/kfmon/bin /usr/local/kfmon 2>/dev/null
        fi
        sync
    fi
    if [ "$restore_failed" = true ]; then
        echo "Nickelite KFMon: rollback incomplete; persistent recovery journal: $recovery" >&2
        echo "Nickelite KFMon: temporary staging retained: $work" >&2
        status=1
    else
        if [ -n "$recovery" ]; then
            rm -rf "$recovery" || echo "Nickelite KFMon: recovery journal retained: $recovery" >&2
        fi
        rm -rf "$work"
    fi
    exit "$status"
}
trap cleanup 0
trap 'exit 1' 1 2 3 15

# Do not follow an existing system-path symlink, even on a marked installation.
for dir in /etc /etc/init.d /etc/rcS.d /usr /usr/bin /usr/local; do
    [ -d "$dir" ] && [ ! -L "$dir" ] || fail "invalid system directory: $dir"
done
for dir in /usr/local/kfmon /usr/local/kfmon/bin; do
    [ ! -L "$dir" ] || fail "symlink directory: $dir"
    if [ -e "$dir" ]; then [ -d "$dir" ] || fail "not a directory: $dir"; fi
done
manifest() {
    printf '%s\n' 'Nickelite-KFMon ownership v1'
    for file in $files; do sha256sum "$file" || return 1; done
}
if [ -e "$marker" ] || [ -L "$marker" ]; then
    [ -f "$marker" ] && [ ! -L "$marker" ] || fail 'invalid ownership marker'
    [ "$(head -n 1 "$marker")" = 'Nickelite-KFMon ownership v1' ] || fail 'unknown ownership marker'
    # Validate the stored path set before using its checksums; never execute it.
    awk 'NR > 1 { if (NF != 2 || length($1) != 64 || $1 ~ /[^0-9a-f]/) exit 1; print $2 }' "$marker" > "$work/paths" || fail 'invalid ownership checksums'
    printf '%s\n' "$files" > "$work/expected-paths"
    cmp -s "$work/paths" "$work/expected-paths" || fail 'invalid ownership paths'
    for file in $files; do
        if [ -e "$file" ] || [ -L "$file" ]; then
            [ -f "$file" ] && [ ! -L "$file" ] || fail "owned path replaced: $file"
            expected=$(awk -v path="$file" '$2 == path { print $1 }' "$marker")
            actual=$(sha256sum "$file")
            [ "${actual%% *}" = "$expected" ] || fail "owned file changed: $file"
        else
            [ "$1" = --uninstall ] || fail "owned file missing: $file"
        fi
    done
    for link in /etc/rcS.d/S99kfmon /usr/bin/kfmon-ipc; do
        case "$link" in /etc/*) target=../init.d/kfmon ;; *) target=/usr/local/kfmon/bin/kfmon-ipc ;; esac
        if [ -e "$link" ] || [ -L "$link" ]; then
            [ -L "$link" ] && [ "$(readlink "$link")" = "$target" ] || fail "owned link changed: $link"
        else
            [ "$1" = --uninstall ] || fail "owned link missing: $link"
        fi
    done
    owned=true
else
    [ "$1" = --install ] || fail 'no Nickelite ownership marker; nothing removed'
    for file in /usr/local/kfmon /etc/init.d/kfmon /etc/rcS.d/S99kfmon /usr/bin/kfmon-ipc; do
        if [ -e "$file" ] || [ -L "$file" ]; then fail "unowned path: $file"; fi
    done
fi

if [ "$1" = --uninstall ]; then
    if [ -f /etc/init.d/kfmon ]; then sh /etc/init.d/kfmon stop; fi
    # Missing owned files are allowed so an interrupted uninstall can be retried.
    for file in $files; do rm -f "$file"; done
    rm -f /etc/rcS.d/S99kfmon /usr/bin/kfmon-ipc "$marker"
    # Runtime logs and any additional files are retained.
    rmdir /usr/local/kfmon/bin /usr/local/kfmon 2>/dev/null || :
    sync
    exit 0
fi

[ "$#" = 2 ] && [ -f "$2" ] || fail 'missing payload archive'
printf '%s\n' kfmon shim kfmon-ipc fbink kfmon.init nickelite-manage.sh README.md LICENSE CREDITS > "$work/expected"
tar -tf "$2" > "$work/members"
cmp -s "$work/expected" "$work/members" || fail 'unexpected payload paths or duplicates'
tar -tvf "$2" > "$work/types"
awk 'substr($0, 1, 1) != "-" { bad = 1 } END { exit bad }' "$work/types" || fail 'payload must contain only regular files'
mkdir "$work/payload"
tar -xf "$2" -C "$work/payload"
for file in kfmon shim kfmon-ipc fbink kfmon.init nickelite-manage.sh README.md LICENSE CREDITS; do
    [ -s "$work/payload/$file" ] && [ ! -L "$work/payload/$file" ] || fail "invalid payload: $file"
done
[ -f /usr/libexec/platform/nickel-env.sh ] || fail 'firmware platform environment not found'
# A verified rootfs journal survives the OTA reboot if rollback itself fails.
recovery=$(mktemp -d /usr/local/.nickelite-kfmon-recovery.XXXXXX)
printf '%s\n' "$files" /etc/rcS.d/S99kfmon /usr/bin/kfmon-ipc "$marker" > "$recovery/written-paths"
cat > "$recovery/README.txt" <<'RECOVERY'
Nickelite KFMon interrupted installation recovery journal.
Do not use the upstream uninstaller. No userstore configs were modified.
backup/ contains prior rootfs files, with their original modes and absolute
paths beneath that prefix; backup-marker is the prior ownership marker.
Verify the saved files with: sha256sum -c checksums
Only paths in written-paths were eligible for installation. For a fresh install
there are no prior files to restore; payload-checksums identifies new contents.
Retain this directory until recovery has been completed and verified.
RECOVERY
(cd "$work/payload" && sha256sum kfmon shim kfmon-ipc fbink kfmon.init nickelite-manage.sh README.md LICENSE CREDITS) > "$recovery/payload-checksums"
if [ "$owned" = true ]; then
    mkdir -p "$recovery/backup/etc/init.d" "$recovery/backup/usr/local/kfmon/bin"
    for file in $files; do
        cp -p "$file" "$recovery/backup$file"
        original=$(sha256sum "$file")
        saved=$(sha256sum "$recovery/backup$file")
        [ "${original%% *}" = "${saved%% *}" ] || fail "backup verification failed: $file"
        printf '%s\n' "$saved" >> "$recovery/checksums"
    done
    cp -p "$marker" "$recovery/backup-marker"
    cmp -s "$marker" "$recovery/backup-marker" || fail 'marker backup verification failed'
    sha256sum "$recovery/backup-marker" >> "$recovery/checksums"
    sha256sum -c "$recovery/checksums" > /dev/null
fi
sync
if [ "$owned" = true ]; then sh /etc/init.d/kfmon stop; fi
rollback=true
mkdir -p /usr/local/kfmon/bin
for file in $files; do
    case "$file" in
        /etc/init.d/kfmon) source=kfmon.init; mode=755 ;;
        /usr/local/kfmon/bin/*) source=${file##*/}; mode=755 ;;
        *) source=${file##*/}; mode=644 ;;
    esac
    touched="$touched $file"
    cp "$work/payload/$source" "$file"
    chmod "$mode" "$file"
done
if [ "$owned" = false ]; then
    ln -s ../init.d/kfmon /etc/rcS.d/S99kfmon
    service_link=true
    ln -s /usr/local/kfmon/bin/kfmon-ipc /usr/bin/kfmon-ipc
    ipc_link=true
fi
manifest > "$work/marker"
marker_temp=$(mktemp /usr/local/kfmon/.nickelite-marker.XXXXXX)
cp "$work/marker" "$marker_temp"
chmod 644 "$marker_temp"
mv "$marker_temp" "$marker"
marker_temp=
rollback=false
sync
# OTA reboots after stage1; leave startup to the existing S99kfmon service.
