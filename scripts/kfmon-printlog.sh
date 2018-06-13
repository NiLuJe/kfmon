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
# FIXME: May be a bit optimistic?
LOG_LINES="15"

# And feed it to FBInk... (avoiding the first row because it's behind the bezel on my H2O ;p)
${FBINK_BIN} -y 1 "$(tail -n ${LOG_LINES} "${KFMON_LOG}")"

return 0
