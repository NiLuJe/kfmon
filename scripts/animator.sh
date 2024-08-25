#!/bin/sh

# NOTE: Based on the initial FW 5.x animator script.
#       Since the initial lineup was MTK-only, this uses pickel-mtk directly,
#       keep that in mind in the future if a pickel symlink is ever reintroduced for platform selection...

# Like in the v4 script, we want an initial flashing refresh on MTK
zcat "${1}" | /usr/local/Kobo/pickel-mtk showpic

while true ; do
	# Loop over the script's arguments
	for image ; do
		if [ -s "${image}" ] ; then
			zcat "${image}" | /usr/local/Kobo/pickel-mtk showpic 1
			sleep 0.25
		fi
	done
done
