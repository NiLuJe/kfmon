#!/bin/sh

PRODUCT="$(/bin/sh /bin/kobo_config.sh)"
[ "${PRODUCT}" != "trilogy" ] && PREFIX="${PRODUCT}-"
COLOR="OFF"
if [ -e "/dev/mmcblk0p6" ] ; then
	# We've only had color panels on MTK devices so far...
	COLOR="$(ntx_hwconfig -S 1 -p /dev/mmcblk0p6 EPD_Flags CFA)"
fi

i=0
PARTIAL_UPDATE=0
while true ; do
	i=$((((i + 1)) % 11))
	image="/etc/images/${PREFIX}on-${i}.raw.gz"
	if [ -s "${image}" ] ; then
		if [ "${COLOR}" == "ON" ] ; then
			zcat "${image}" | /usr/local/Kobo/pickel showpic ${PARTIAL_UPDATE}
			PARTIAL_UPDATE=1
		else
			zcat "${image}" | /usr/local/Kobo/pickel showpic 1
		fi
		usleep 250000
	fi
done
