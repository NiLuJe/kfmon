#!/bin/sh

PRODUCT="$(/bin/sh /bin/kobo_config.sh)"
[ "${PRODUCT}" != "trilogy" ] && PREFIX="${PRODUCT}-"
COLOR="OFF"
if [ -e "/dev/mmcblk0p6" ] ; then
	# We've only had color panels on MTK devices so far...
	COLOR="$(ntx_hwconfig -S 1 -p /dev/mmcblk0p6 EPD_Flags CFA)"
fi

PARTIAL_UPDATE=1
if [ "${COLOR}" == "ON" ] ; then
	# Send the first iteration as a FULL update on color panels
	PARTIAL_UPDATE=0
fi

i=0
while true ; do
	i=$(( (i + 1) % 11 ))
	image="/etc/images/${PREFIX}on-${i}.raw.gz"
	if [ -s "${image}" ] ; then
		zcat "${image}" | /usr/local/Kobo/pickel showpic ${PARTIAL_UPDATE}
		PARTIAL_UPDATE=1
		usleep 250000
	fi
done
