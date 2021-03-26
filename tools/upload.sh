#!/bin/bash -e
#
# Quick'n dirty script to upload packages to my PCS
#
##

#
## Helper functions
#

# Check if arg is an int
is_integer() {
	# Cheap trick ;)
	[ "${1}" -eq "${1}" ] 2>/dev/null
	return $?
}

#
## Load my PCS credentials, and script settings
#

## Remember where we are...
SCRIPTS_BASE_DIR="$(readlink -f "${BASH_SOURCE%/*}")"

## This config file sources my PCS credentials...
# shellcheck disable=SC1090
source ~/.mr_settings

## And also sets these constants, specific to this script.
# Which container are we using?
#ST_CONTAINER=""
# What's the StorageURL?
#ST_STORAGEURL=""
# Which makes our base URL...
#BASE_URL=""

# Override the container name
KFM_ST_CONTAINER="kfmon-pub"
# Which means we need to refresh the base URL...
KFM_ST_STORAGEURL="${ST_STORAGEURL/${ST_CONTAINER}/${KFM_ST_CONTAINER}}"
KFM_BASE_URL="${BASE_URL/${ST_CONTAINER}/${KFM_ST_CONTAINER}}"
# And now that's done, override the usual variables...
ST_CONTAINER="${KFM_ST_CONTAINER}"
ST_STORAGEURL="${KFM_ST_STORAGEURL}"
BASE_URL="${KFM_BASE_URL}"


#
## Upload stuff.
#

# Empty it first...
echo "* Cleaning up container . . ."
# Start by making it private for the duration of our update...
swift post -r '' ${ST_CONTAINER}
# Delete the container's contents (and possibly the container itself)
until swift delete --retries=5 --object-threads=2 --container-threads=2 ${ST_CONTAINER} ; do
	sleep 15
	# Sigh... May help with "Container not found" errors after a failed delete (HTTP 409)
	swift post --verbose -r '' ${ST_CONTAINER}
done
# Recreate the container if needed, and keep it private during the upload...
swift post -r '' ${ST_CONTAINER}

# Swift pushes relative paths as is, so move stuff around first...

# Go to the staging directory...
echo "* Setting up staging directory . . ."
cd /tmp/KFMon
# Move our stuff...
cp -pv "${SCRIPTS_BASE_DIR}/KFMON_PUB_BB" ./
cp -pv "${SCRIPTS_BASE_DIR}/install.sh" ./
cp -pv "${SCRIPTS_BASE_DIR}/install.ps1" ./
cp -pv "${SCRIPTS_BASE_DIR}"/../Kobo/KFMon-v*.zip ./

# Upload!
echo "* Uploading . . ."
# shellcheck disable=SC2035
swift upload --retries=5 --object-threads=2 ${ST_CONTAINER} *.zip

# Make it public straightaway so that the URL check will work...
# Make sure our container is readable by anonymous users...
echo "* Make it public! . . ."
swift post -r '.r:*' ${ST_CONTAINER}
# Enable web-listings, even if we're using our own...
swift post -m 'web-listings: true' ${ST_CONTAINER}

#
## Build the index
#

echo "* Building index . . ."
# Header
cat > kfmon.html << EOF
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head>
	<title>One-Click Packages</title>
	<meta name="robots" content="nofollow"/>
	<!--
		Shamelessy ripped from Lighttpd's mod_dirlisting
		(http://redmine.lighttpd.net/projects/lighttpd/repository/entry/branches/lighttpd-1.4.x/src/mod_dirlisting.c)
	-->
	<style type="text/css">
		a, a:active {text-decoration: none; color: blue;}
		a:visited {color: #48468F;}
		a:hover, a:focus {text-decoration: underline; color: red;}
		body {background-color: #F5F5F5;}
		h2 {margin-bottom: 12px;}
		table {margin-left: 12px;}
		th, td {
			font: 90% monospace;
			text-align: left;
		}
		th {
			font-weight: bold;
			padding-right: 14px;
			padding-bottom: 3px;
		}
		td {padding-right: 14px;}
		td.s, th.s {text-align: right;}
		div.list {
			background-color: white;
			border-top: 1px solid #646464;
			border-bottom: 1px solid #646464;
			padding-top: 10px;
			padding-bottom: 14px;
		}
		div.foot {
			font: 90% monospace;
			color: #787878;
			padding-top: 4px;
		}
	</style>
</head>
<body>
	<h2>Index of One-Click Packages</h2>

	<div class="list">
		<table summary="Directory Listing" cellpadding="0" cellspacing="0">
			<thead>
				<tr>
					<th class="n">Name</th>
					<th class="m">Last Modified</th>
					<th class="s">Size</th>
					<th class="t">Type</th>
					<th class="c">MD5 Checksum</th>
				</tr>
			</thead>
			<tbody>
				<tr>
					<td class="n"><a href="https://www.mobileread.com/forums/forumdisplay.php?f=247">Kobo Developer's Corner Forum</a></td>
					<td class="m">- &nbsp;</td>
					<td class="s">- &nbsp;</td>
					<td class="t">Link</td>
					<td class="c">- &nbsp;</td>
				</tr>
			</tbody>

			<!-- Some more links... -->
			<tr><td class="n"><a href="https://www.mobileread.com/forums/showthread.php?t=314220">Matching Thread @ MR</a></td><td class="m">- &nbsp;</td><td class="s">- &nbsp;</td><td class="t">Link</td><td class="c">- &nbsp;</td></tr>
			<tr><td class="n"><a href="https://github.com/koreader/koreader">KOReader @ GitHub</a></td><td class="m">- &nbsp;</td><td class="s">- &nbsp;</td><td class="t">Link</td><td class="c">- &nbsp;</td></tr>
			<tr><td class="n"><a href="https://github.com/baskerville/plato">Plato @ GitHub</a></td><td class="m">- &nbsp;</td><td class="s">- &nbsp;</td><td class="t">Link</td><td class="c">- &nbsp;</td></tr>
			<tr><td class="n"><a href="https://github.com/NiLuJe/kfmon">KFMon @ GitHub</a></td><td class="m">- &nbsp;</td><td class="s">- &nbsp;</td><td class="t">Link</td><td class="c">- &nbsp;</td></tr>

			<!-- And a dummy entry to act as a spacer -->
			<tr><td class="n">&nbsp;</td><td class="m">&nbsp;</td><td class="s">&nbsp;</td><td class="t">&nbsp;</td><td class="c">&nbsp;</td></tr>
EOF

# Handle each of our files...
echo "* Parsing current folder . . ."
# Directory (we've only got one)
cat >> kfmon.html << EOF
		<tr><td class="n">Kobo One-Click Install Packages</td><td class="m">- &nbsp;</td><td class="s">- &nbsp;</td><td class="t">Directory</td><td class="c">- &nbsp;</td></tr>
EOF

# And a JSON manifest...
cat > manifest.json << EOF
{
	"Storage_URL": "${BASE_URL}",
	"OCP": [
EOF

for file in *.zip ; do
	# Check if the upload was successful...
	until curl --output /dev/null --silent --head --fail "${BASE_URL}/${file}" ; do
		# Try to re-upload it...
		echo "*!!* Hu ho, ${file} wasn't uploaded successfully, trying again... *!!*"
		swift upload --retries=5 --object-threads=2 ${ST_CONTAINER} "${file}"
		# Wait a bit...
		sleep 5
	done

	echo "* Parsing file ${file} . . ."
	# Get the mimetype
	mimetype="$(mimetype -b "${file}")"
	# Get the modification date
	rawmoddate="$(stat -c "%Y" "${file}")"
	moddate="$(date -d "@${rawmoddate}" +"%Y-%b-%d %H:%M:%S")"
	# Get the file size
	rawsize="$(stat -c "%s" "${file}")"
	if [[ ${rawsize} -ge 1048576 ]] ; then
		size="$(echo "scale=1;${rawsize}/1048576" | bc)M"
	elif [[ ${rawsize} -ge 1024 ]] ; then
		size="$(echo "scale=1;${rawsize}/1024" | bc)K"
	else
		size="${rawsize}.0B"
	fi
	# Get the MD5 checksum
	checksum="$(md5sum "${file}" | cut -f1 -d ' ')"
	# And the BLAKE2B checksum
	b2bchecksum="$(b2sum "${file}" | cut -f1 -d ' ')"
	# Pull the package name out of the filename
	name="$(echo "${file##*/}" | sed -re 's/^(OCP-)?([[:alpha:]\-]*?)-([[:digit:]v\.]*?)\..*?$/\2/')"
	# Pull the version out of the filename
	version="$(echo "${file##*/}" | sed -re 's/^(OCP-)?([[:alpha:]\-]*?)-([[:digit:]v\.]*?)\..*?$/\3/')"

	# Stupid exceptions...
	case "${file##*/}" in
		OCP-Plato-*_KOReader-v*.zip )
			name="Both"
			version="N/A"
		;;
	esac

	# File
	cat >> kfmon.html << EOF
		<tr><td class="n"><a href="${BASE_URL}/${file}" rel="nofollow">${file##*/}</a></td><td class="m">${moddate}</td><td class="s">${size}</td><td class="t">${mimetype}</td><td class="c">${checksum}</td></tr>
EOF
	cat >> manifest.json << EOF
		{
			"name": "${name}",
			"version": "${version}",
			"filename": "${file##*/}",
			"path": "${file}",
			"mimetype": "${mimetype}",
			"date": ${rawmoddate},
			"revision": -1,
			"size": ${rawsize},
			"MD5": "${checksum}",
			"BLAKE2B": "${b2bchecksum}"
		},
EOF

	# And then the MR thread...
	case "${file##*/}" in
		OCP-Plato-*_KOReader-v*.zip )
			mr_file="BOTH"
		;;
		OCP-KOReader-v*.zip )
			mr_file="KOREADER"
		;;
		OCP-Plato-*.zip )
			mr_file="PLATO"
		;;
		KFMon-*.zip )
			mr_file="KFMON"
		;;
		* )
			echo "Unknown file for ${file##*/} !!"
			exit 1
		;;
	esac

	# Do the actual substitution...
	if [[ "${mr_file}" != "KFMON" ]] ; then
		sed -e "s~%${mr_file}%~[url=${BASE_URL}/${file}]${file##*/}[/url]  [B]|[/B]  [I]${moddate}[/I]  [B]|[/B]  ${size}  [B]|[/B]  [COLOR=\"DimGray\"]${checksum}[/COLOR]~" -i KFMON_PUB_BB
	fi
done

cat >> kfmon.html << EOF
		</table>

	</div>

	<div class="foot">
		<span>Don't Panic! -- Last updated on $(date -R)</span>
	</div>

</body>
</html>
EOF

# Can't have trailing commas in JSON...
sed -i '$ d' manifest.json
cat >> manifest.json << EOF
		}
	]
}
EOF

# The MR thread, too...
sed -e "s~%LASTUPDATED%~Last updated on $(date -R)~" -i KFMON_PUB_BB

# Upload it!
echo "* Uploading index . . ."
# Push it to the cloud...
swift upload --retries=5 --object-threads=2 ${ST_CONTAINER} kfmon.html manifest.json

# Handle the *nix install helper script, which we zip up to preserve the exec bit...
echo "* Uploading install scripts . . ."
zip kfm_nix_install.zip install.sh
swift upload --retries=5 --object-threads=2 ${ST_CONTAINER} kfm_nix_install.zip

# macOS is a special snowflake...
cp -pv install.sh install.command
zip kfm_mac_install.zip install.command
swift upload --retries=5 --object-threads=2 ${ST_CONTAINER} kfm_mac_install.zip

# Mirror Windows script, too
swift upload --retries=5 --object-threads=2 ${ST_CONTAINER} install.ps1

# Clean it up...
echo "* Cleanup . . ."
rm -rfv ./kfmon.html ./manifest.json ./kfm_nix_install.zip ./install.sh ./kfm_mac_install.zip ./install.command ./install.ps1

# Go back
# shellcheck disable=SC2103
#cd -

# And we're done :)
exit 0
