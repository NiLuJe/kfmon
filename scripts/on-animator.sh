#!/bin/sh

PRODUCT=`/bin/sh /bin/kobo_config.sh`;
[ $PRODUCT != trilogy ] && PREFIX=$PRODUCT-

# Launch KFMon if it isn't already running...
if ! pkill -0 kfmon ; then
        KFMON_BIN="/usr/local/kfmon/bin/kfmon"
        [ -x "${KFMON_BIN}" ] && LIBC_FATAL_STDERR_=1 "${KFMON_BIN}" &
fi

i=0;
while true; do
        i=$((((i + 1)) % 11));
        zcat /etc/images/$PREFIX\on-$i.raw.gz | /usr/local/Kobo/pickel showpic 1;
        usleep 250000;
done
