#!/bin/sh
#
# Print the last 15 lines of log on screen, with FBInk
# This is a pretty terrible workaround, until I manage to get linefeed handled sanely directly in FBInk ;).
# In which case, fbink -y 1 "$(tail -n 15 /usr/local/kfmon/kfmon.log)" will instead do the job!
#
##

# Pickup the FBInk binary we're shipping
FBINK_BIN="/usr/local/kfmon/bin/fbink"
# Where's our log?
KFMON_LOG="/usr/local/kfmon/kfmon.log"

# And here comes Joe's crappy workaround...
LOGLINE_1="$(tail -n 15 "${KFMON_LOG}" | head -n 1)"
LOGLINE_2="$(tail -n 14 "${KFMON_LOG}" | head -n 1)"
LOGLINE_3="$(tail -n 13 "${KFMON_LOG}" | head -n 1)"
LOGLINE_4="$(tail -n 12 "${KFMON_LOG}" | head -n 1)"
LOGLINE_5="$(tail -n 11 "${KFMON_LOG}" | head -n 1)"
LOGLINE_6="$(tail -n 10 "${KFMON_LOG}" | head -n 1)"
LOGLINE_7="$(tail -n 9 "${KFMON_LOG}" | head -n 1)"
LOGLINE_8="$(tail -n 8 "${KFMON_LOG}" | head -n 1)"
LOGLINE_9="$(tail -n 7 "${KFMON_LOG}" | head -n 1)"
LOGLINE_10="$(tail -n 6 "${KFMON_LOG}" | head -n 1)"
LOGLINE_11="$(tail -n 5 "${KFMON_LOG}" | head -n 1)"
LOGLINE_12="$(tail -n 4 "${KFMON_LOG}" | head -n 1)"
LOGLINE_13="$(tail -n 3 "${KFMON_LOG}" | head -n 1)"
LOGLINE_14="$(tail -n 2 "${KFMON_LOG}" | head -n 1)"
LOGLINE_15="$(tail -n 1 "${KFMON_LOG}")"

# And finally feed it to FBInk... (avoiding the first row because it's behind the bezel on my H2O ;p)
${FBINK_BIN} -y 1       \
	"${LOGLINE_1}"  \
	"${LOGLINE_2}"  \
	"${LOGLINE_3}"  \
	"${LOGLINE_4}"  \
	"${LOGLINE_5}"  \
	"${LOGLINE_6}"  \
	"${LOGLINE_7}"  \
	"${LOGLINE_8}"  \
	"${LOGLINE_9}"  \
	"${LOGLINE_10}" \
	"${LOGLINE_11}" \
	"${LOGLINE_12}" \
	"${LOGLINE_13}" \
	"${LOGLINE_14}" \
	"${LOGLINE_15}"

return 0
