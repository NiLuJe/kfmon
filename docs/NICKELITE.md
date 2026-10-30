# Nickelite KFMon package

`make nickelite CROSS_TC=/path/to/arm-kobo-linux-gnueabihf` builds a separate
firmware 5.x/6.x OTA package. Initialize the existing submodules and configure the
upstream Kobo cross-toolchain first. `kobo` and `kobov5` are unchanged.

Outputs are under `Release/Nickelite/`:

- `KFMon-Nickelite-Install.zip`: only `.kobo/update.tar`.
- `KFMon-Nickelite-Uninstall.zip`: only `.kobo/update.tar`.
- `KFMon-Nickelite-Defaults.zip`: an optional main config and absent-only copier.

Extract the install ZIP into the reader's userstore root, then disconnect safely
and let the OTA updater process `.kobo/update.tar`. The outer archive has exactly
`driver.sh`, `nickelite-manage.sh`, and `nickelite-kfmon.tgz`; the uninstaller omits
the last member. Both driver and manager validate exact member lists, including
order and duplicates, and reject links/special files before extraction into a
private temporary directory. The flat payload contains only `kfmon`, `shim`,
`kfmon-ipc`, `fbink`, `kfmon.init`, `nickelite-manage.sh`, `README.md`, `LICENSE`, and
`CREDITS`.

The binaries and service are upstream builds with unchanged ABI and IPC. The
installer writes only:

- `/usr/local/kfmon/bin/{kfmon,shim,kfmon-ipc,fbink,nickelite-manage.sh}`
- `/usr/local/kfmon/{README.md,LICENSE,CREDITS,.nickelite-owned-v1}`
- `/etc/init.d/kfmon`, the existing upstream service
- `/etc/rcS.d/S99kfmon -> ../init.d/kfmon`
- `/usr/bin/kfmon-ipc -> /usr/local/kfmon/bin/kfmon-ipc`

The installed `README.md` is this variant's documentation, including a build
source receipt; it does not direct users to the upstream uninstaller.

The ownership marker records checksums of the exact installed regular files.
Reinstallation verifies those checksums and both link targets first. Unmarked
existing paths, changed owned files, and symlink directories cause refusal before
system files are changed. An existing upstream installation must be handled
separately; this package will not adopt or uninstall it. No animator scripts,
upstream uninstaller, NickelMenu library, `.adds/nm/doc`, icons, or user configs
are included in the system package.

Before changing system files, installation creates a collision-safe recovery
journal under `/usr/local/.nickelite-kfmon-recovery.XXXXXX`. Prior files and their
ownership marker are copied, hash-verified and synced there before replacement.
Ordinary failures restore those backups; successful installation or rollback
removes its journal. If restoring or removing an owned file fails, both the
persistent journal and temporary staging are retained, and stderr prints their
exact paths. The rootfs journal survives the OTA reboot; its `README.txt`,
`written-paths` and `checksums` support manual recovery without guessing paths.
Uninstall accepts missing owned files and links, so an interrupted removal can
be retried; every remaining owned file must still match its stored checksum.

The system installer and uninstaller never write to `.adds`. To add the optional
main config, extract the defaults ZIP somewhere convenient on the userstore and
run `sh /mnt/onboard/Nickelite-KFMon-defaults/nickelite-defaults.sh` on the reader.
It creates `.adds/kfmon/config/kfmon.ini` only when absent; existing files and
symlinks are preserved. Launcher-specific watch configs remain the user's choice.

To uninstall, use the uninstall ZIP through the same OTA route, or run
`sh /usr/local/kfmon/bin/nickelite-manage.sh --uninstall` as root. It checks the
ownership marker, stops the service, and removes only the exact installed files
and links. It retains every `.adds` file, runtime log, and additional rootfs file;
only empty KFMon directories are removed. Keep this uninstaller instead of using
the upstream uninstaller, which has a broader deletion scope.

## OTA evidence and validation limits

Read-only inspection of the available firmware 5 and firmware 6 QEMU rootfs images
found the same `/etc/init.d/ota` (SHA256
`60ce6320db4a065be8f37e5c3fa3bccb5a54948c261dd980fde7aba0611a8448`). It invokes
`driver.sh <archive> stage1 "$PRODUCT"`; zero selects recovery, while nonzero
removes the archive and reboots normally. This variant's driver returns **1 on
success, validation failure, shell error, and handled signal**, and performs no
stage2 work. The nonzero exit is intentional; it is not an installation success
signal. After the manager completes, the driver prints a stage1 receipt before
the firmware's generic `Update failed.` message and normal reboot. Errors are
printed to stderr. A power failure or forced SIGKILL can still interrupt a rootfs
update and require manual repair.

This evidence describes those QEMU images, not a physical reader qualification.
Firmware 6 installation/runtime compatibility has not been tested. Do not infer
support for arbitrary future OTA implementations from the version range.
