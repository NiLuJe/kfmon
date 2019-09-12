#!/bin/sh

PRODUCT="$(/bin/sh /bin/kobo_config.sh)";
[ "${PRODUCT}" != "trilogy" ] && PREFIX="${PRODUCT}-"

i=0;
while true; do
        i=$((((i + 1)) % 11));
        image="/etc/images/${PREFIX}on-${i}.raw.gz"
        if [ -s "${image}" ] ; then
            zcat "${image}" | /usr/local/Kobo/pickel showpic 1
            usleep 250000
        fi
done
