#!/bin/bash
#
# Quick'n dirty helper to pickup a Kobo's USBMS mountpoint...
#
##

# Force a cd to the script's directory, because on macOS, a command script has a fixed $PWD set to $HOME...
cd -- "$(dirname "${BASH_SOURCE[0]}")"

# We're ultimately going to need unzip...
if ! unzip -v &>/dev/null ; then
	echo "This script relies on unzip!"
	exit -1
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
			exit -1
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
		exit -1
	;;
esac

# Sanity check...
if [[ -z "${KOBO_MOUNTPOINT}" ]] ; then
	echo "Couldn't find a Kobo eReader volume! Is one actually mounted?"
	exit -1
fi

KOBO_DIR="${KOBO_MOUNTPOINT}/.kobo"
if [[ ! -d "${KOBO_DIR}" ]] ; then
	echo "Can't find a .kobo directory, ${KOBO_MOUNTPOINT} doesn't appear to point to a Kobo eReader... Is one actually mounted?"
	exit -1
fi

# Ask the user what they want to install...
AVAILABLE_PKGS=()
for file in KOReader-v*.zip Plato-*.zip KFMon-v*.zip KFMon-Uninstaller.zip ; do
	[[ -f "${file}" ]] && AVAILABLE_PKGS+=("${file}")
done

# Sanity check...
if [[ ${#AVAILABLE_PKGS[@]} -eq 0 ]] ; then
	echo "No supported packages found in the current directory (${PWD})!"
	exit -1
fi

echo "* Here are the available packages:"
for i in "${!AVAILABLE_PKGS[@]}" ; do
	echo "${i}: ${AVAILABLE_PKGS[${i}]}"
done

read -r -p "* Enter the number corresponding to the one you want to install: " j

# Check if that was a sane reply...
if ! [ "${j}" -eq "${j}" ] 2>/dev/null ; then
	echo "That wasn't a number!"
	exit -1
fi

if [[ ${j} -lt 0 ]] || [[ ${j} -ge ${#AVAILABLE_PKGS[@]} ]] ; then
	echo "That number was out of range!"
	exit -1
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
