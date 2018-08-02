#!/bin/sh
#
# Print the last ${LOG_LINES} lines of log on screen, with FBInk
#
##

# Pickup the FBInk binary we're shipping
FBINK_BIN="/usr/local/kfmon/bin/fbink"
# Where's our log?
KFMON_LOG="/usr/local/kfmon/kfmon.log"
# How many lines do we want to print?
# NOTE: Hard to pinpoint the right amount of stuff to print,
#       since it depends both on the device and the actual content of the log...
LOG_LINES="15"

# Check if we're logging to syslog instead...
KFMON_CFG="/mnt/onboard/.adds/kfmon/config/kfmon.ini"
if grep use_syslog "${KFMON_CFG}" | grep -q -i -e 1 -e "on" -e "true" -e "yes" ; then
	KFMON_USE_SYSLOG="true"
else
	KFMON_USE_SYSLOG="false"
fi

# Sleep for a bit, so we don't race with Nickel opening the "book"...
sleep 2

# And feed it to FBInk... (avoiding the first row because it's behind the bezel on my H2O ;p)
FBINK_ROW="1"
if [ "${KFMON_USE_SYSLOG}" == "true" ] ; then
	${FBINK_BIN} -q -y ${FBINK_ROW} "$(logread | grep -e KFMon -e FBInk | tail -n ${LOG_LINES})"
else
	${FBINK_BIN} -q -y ${FBINK_ROW} "$(tail -n ${LOG_LINES} "${KFMON_LOG}")"
fi

return 0
