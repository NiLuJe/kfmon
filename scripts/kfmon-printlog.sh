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

# Sleep for a bit, so we don't race with Nickel opening the "book"...
sleep 2

# And feed it to FBInk... (avoiding the first row because it's behind the bezel on my H2O ;p)
${FBINK_BIN} -q -y 1 "$(tail -n ${LOG_LINES} "${KFMON_LOG}")" >/dev/null 2>&1

return 0
