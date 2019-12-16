#!/bin/bash
set -puxo pipefail
#
# Quick'n dirty helper to pickup a Kobo's USBMS mountpoint...
#
##

# Force a cd to the script's directory, because on macOS, a command script has a fixed $PWD set to $HOME...
cd -P -- "$(dirname "${BASH_SOURCE[0]}")" || exit 255

# We're ultimately going to need unzip...
if ! unzip -v &>/dev/null ; then
	echo "This script relies on unzip!"
	exit 255
fi

# Are we on Linux or macOS?
PLATFORM="$(uname -s)"

# Find out where the Kobo is mounted...
KOBO_MOUNTPOINT="/dev/null"

case "${PLATFORM}" in
	"Linux" )
		# Use findmnt, it's in util-linux, which should be present in every sane distro.
		if ! findmnt -V &>/dev/null ; then
			echo "This script relies on findmnt, from util-linux!"
			exit 255
		fi

		# Match on the FS Label, which is common to all models.
		KOBO_MOUNTPOINT="$(findmnt -nlo TARGET LABEL=KOBOeReader)"
	;;
	"Darwin" )
		# Same idea, via diskutil
		KOBO_MOUNTPOINT="$(diskutil info -plist "KOBOeReader" | grep -A1 "MountPoint" | tail -n 1 | cut -d'>' -f2 | cut -d'<' -f1)"
	;;
	* )
		echo "Unsupported OS!"
		exit 255
	;;
esac

# Sanity check...
if [[ -z "${KOBO_MOUNTPOINT}" ]] ; then
	echo "Couldn't find a Kobo eReader volume! Is one actually mounted?"
	exit 255
fi

KOBO_DIR="${KOBO_MOUNTPOINT}/.kobo"
if [[ ! -d "${KOBO_DIR}" ]] ; then
	echo "Can't find a .kobo directory, ${KOBO_MOUNTPOINT} doesn't appear to point to a Kobo eReader... Is one actually mounted?"
	exit 255
fi

# Ask the user what they want to install...
AVAILABLE_PKGS=()
for file in KOReader-v*.zip Plato-*.zip KFMon-v*.zip KFMon-Uninstaller.zip ; do
	[[ -f "${file}" ]] && AVAILABLE_PKGS+=("${file}")
done

# Sanity check...
if [[ ${#AVAILABLE_PKGS[@]} -eq 0 ]] ; then
	echo "No supported packages found in the current directory (${PWD})!"
	exit 255
fi

echo "* Here are the available packages:"
for i in "${!AVAILABLE_PKGS[@]}" ; do
	echo "${i}: ${AVAILABLE_PKGS[${i}]}"
done

read -r -p "* Enter the number corresponding to the one you want to install: " j

# Check if that was a sane reply...
if ! [ "${j}" -eq "${j}" ] 2>/dev/null ; then
	echo "That wasn't a number!"
	exit 255
fi

if [[ ${j} -lt 0 ]] || [[ ${j} -ge ${#AVAILABLE_PKGS[@]} ]] ; then
	echo "That number was out of range!"
	exit 255
fi

# NOTE: Since FW 4.17, Nickel will attempt to index content found in hidden directories.
#       Since all of this stuff lives in *nix hidden directories, this won't do.
#       Thankfully, FW 4.17.13694 introduced a hidden setting to control that behavior.
#       We'll enforce the "legacy" behavior of basically ignoring non-default hidden directories.
#       c.f., https://www.mobileread.com/forums/showpost.php?p=3892463&postcount=10
#          &  https://www.mobileread.com/forums/showpost.php?p=3894033&postcount=70
# NOTE: We can simply push a (potentially) duplicate section + entry at the end of the file,
#       QSettings will do the right thing, that is, pick up this new key, use it,
#       and save everything in the right place without leaving duplicates around.
echo "* Preventing Nickel from scanning hidden directories . . ."
cat >> "${KOBO_DIR}/Kobo/Kobo eReader.conf" <<-\EoM

	[FeatureSettings]
	ExcludeSyncFolders=\\.(?!kobo|adobe).*?
EoM

ret=$?
if [ ${ret} -ne 0 ] ; then
	echo "* Installation FAILED: Failed to update Nickel config!"
	echo "* No permanent changes have been made."
	exit ${ret}
fi

# We've got a Kobo, we've got a package, let's go!
if [[ "${AVAILABLE_PKGS[${j}]}" == "KFMon-Uninstaller.zip" ]] ; then
	echo "* Uninstalling KFMon . . ."
	unzip -o "${AVAILABLE_PKGS[${j}]}" -d "${KOBO_DIR}"
else
	echo "* Installing ${AVAILABLE_PKGS[${j}]} . . ."
	unzip -o "${AVAILABLE_PKGS[${j}]}" -d "${KOBO_MOUNTPOINT}"
fi

ret=$?
if [ ${ret} -eq 0 ] ; then
	echo "* Installation successful!"
else
	echo "* Installation FAILED! No cleanup will be done!"
	exit ${ret}
fi
