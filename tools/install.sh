#!/bin/bash
#
# Quick'n dirty helper to pickup a Kobo's USBMS mountpoint...
#
##

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
if [[ ! -d "${KOBO_MOUNTPOINT}/.kobo" ]] ; then
	echo "Can't find a .kobo directory, ${KOBO_MOUNTPOINT} doesn't appear to point to a Kobo eReader... Is one actually mounted?"
	exit -1
fi

# Ask the user what they want to install...
AVAILABLE_PKGS=()
for file in KOReader-v*.zip Plato-*.zip KFMon-v*.zip ; do
	[[ -f "${file}" ]] && AVAILABLE_PKGS+=("${file}")
done

echo "* Here are the available packages:"
for i in ${!AVAILABLE_PKGS[@]} ; do
	echo "${i}: ${AVAILABLE_PKGS[${i}]}"
done

echo -n "* Enter the number corresponding to the one you want to install: "
read j

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
echo "* Installing ${AVAILABLE_PKGS[${j}]} . . ."
echo unzip "${AVAILABLE_PKGS[${j}]}" -d "${KOBO_MOUNTPOINT}"
