#!/bin/sh
#
# Print the last ${LOG_LINES} lines of log on screen, with FBInk
#
##

# As D.Va would say, NERF THIS!
# NOTE: There's a very good chance chrt won't actually be bundled in the Kobo's busybox, but, meh.
chrt -b -p 0 $$
renice -n 5 -p $$
ionice -c 3 -p $$

# Pickup the FBInk binary we're shipping
FBINK_BIN="/usr/local/kfmon/bin/fbink"
# Where's our log?
KFMON_LOG="/usr/local/kfmon/kfmon.log"
# Where's our log dump on the userstore?
KFMON_USER_LOG="/mnt/onboard/.adds/kfmon/log/kfmon_dump.log"
# How many lines do we want to print?
# NOTE: Start high, we'll try to adjust it down to fit both the screen & the content later...
LOG_LINES="25"

# See how many lines we can actually print...
eval $(${FBINK_BIN} -qe)
# Try to account for linebreaks...
MAXCHARS="$(awk -v LOG_LINES=${LOG_LINES} -v MAXCOLS=${MAXCOLS} -v MAXROWS=${MAXROWS} 'BEGIN { print int(MAXCOLS * (MAXROWS - (LOG_LINES / 2))) }')"

# Check if we're logging to syslog instead...
KFMON_CFG="/mnt/onboard/.adds/kfmon/config/kfmon.ini"
if grep use_syslog "${KFMON_CFG}" | grep -q -i -e 1 -e "on" -e "true" -e "yes" ; then
	KFMON_USE_SYSLOG="true"
	# And see how many lines of that we can (roughly) print at most...
	while [ "$(logread | grep -e KFMon -e FBInk | tail -n ${LOG_LINES} | wc -c)" -gt "${MAXCHARS}" ] ; do
		let "LOG_LINES = ${LOG_LINES} - 1"
		# Amount of lines changed, update that!
		MAXCHARS="$(awk -v LOG_LINES=${LOG_LINES} -v MAXCOLS=${MAXCOLS} -v MAXROWS=${MAXROWS} 'BEGIN { print int(MAXCOLS * (MAXROWS - (LOG_LINES / 2))) }')"
	done
else
	KFMON_USE_SYSLOG="false"
	# And see how many lines of that we can (roughly) print at most...
	while [ "$(tail -n ${LOG_LINES} "${KFMON_LOG}" | wc -c)" -gt "${MAXCHARS}" ] ; do
		let "LOG_LINES = ${LOG_LINES} - 1"
		# Amount of lines changed, update that!
		MAXCHARS="$(awk -v LOG_LINES=${LOG_LINES} -v MAXCOLS=${MAXCOLS} -v MAXROWS=${MAXROWS} 'BEGIN { print int(MAXCOLS * (MAXROWS - (LOG_LINES / 2))) }')"
	done
fi

# Sleep for a bit, so we don't race with Nickel opening the "book"...
sleep 2

# Check if something wonky happened with our LOG_LINES trickery...
if [ "${KFMON_USE_SYSLOG}" == "true" ] ; then
	if [ "${LOG_LINES}" -eq "0" ] || [ "$(logread | grep -e KFMon -e FBInk | tail -n ${LOG_LINES} | wc -c)" -eq "0" ] ; then
		${FBINK_BIN} -q -Mmph "Nothing to print?!"
		exit 1
	fi
else
	if [ "${LOG_LINES}" -eq "0" ] || [ "$(tail -n ${LOG_LINES} "${KFMON_LOG}" | wc -c)" -eq "0" ] ; then
		${FBINK_BIN} -q -Mmph "Nothing to print?!"
		exit 1
	fi
fi

# Everything's okay, feed it to FBInk...
if [ "${KFMON_USE_SYSLOG}" == "true" ] ; then
	logread | grep -e KFMon -e FBInk | tail -n ${LOG_LINES} | ${FBINK_BIN} -q
else
	tail -n ${LOG_LINES} "${KFMON_LOG}" | ${FBINK_BIN} -q
fi

# Dump it in the userstore, to make it easily accessible to users without shell access
if [ "${KFMON_USE_SYSLOG}" == "true" ] ; then
	logread | grep -e KFMon -e FBInk > "${KFMON_USER_LOG}" 2>&1
else
	cp -f "${KFMON_LOG}" "${KFMON_USER_LOG}"
fi
# Add a timestamp, and a dump of Nickel's version tag
echo "**** Log dumped on $(date +'%Y-%m-%d @ %H:%M:%S') ****" >> "${KFMON_USER_LOG}"
echo "**** FW $(awk '{print $3}' FS=',' /mnt/onboard/.kobo/version) on Linux $(uname -r) ($(uname -v)) ****" >> "${KFMON_USER_LOG}"

return 0
