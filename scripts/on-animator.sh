#!/bin/sh

# Launch KFMon if it isn't already running...
KFMON_LOG="/usr/local/kfmon/kfmon.log"
if ! pkill -0 kfmon ; then
	echo "[START] [$(date +'%Y-%m-%d @ %H:%M:%S')] [INFO] [PID: $$] Starting KFMon . . ." >> "${KFMON_LOG}"
	KFMON_BIN="/usr/local/kfmon/bin/kfmon"
	if [ -x "${KFMON_BIN}" ] ; then
		LIBC_FATAL_STDERR_=1 "${KFMON_BIN}" &
		# NOTE: The PID shown here is not terribly helpful, since the first thing KFMon will do will be to fork twice to daemonize...
		echo "[START] [$(date +'%Y-%m-%d @ %H:%M:%S')] [INFO] [PID: $$] Launched KFMon! (Initial PID: $!)" >> "${KFMON_LOG}"
	else
		echo "[START] [$(date +'%Y-%m-%d @ %H:%M:%S')] [ERR!] [PID: $$] KFMon binary '${KFMON_BIN}' cannot be executed!" >> "${KFMON_LOG}"
	fi
else
	# NOTE: I'm sometimes seeing wonky behavior after an update, where we trip the "already running" check when we actually *do* need to be launched...
	#       Possibly due to the specific timing at which on-animator runs around updates?
	echo "[START] [$(date +'%Y-%m-%d @ %H:%M:%S')] [WARN] [PID: $$] KFMon is already running (PID: $(pidof kfmon || echo 'N/A'))!" >> "${KFMON_LOG}"
fi

# Ditch the pickel progress bar for FBInk's, for shit'n giggles.
FBINK_BIN="/usr/local/kfmon/bin/fbink"

# NOTE: There's a bit of trickery involved with FBInk having to fake its process name as on-animator.sh,
#       just so it gets killed when on-animator itself gets the axe,
#       because that's done in a way we can't do anything about from here (SIGKILL)...
exec ${FBINK_BIN} -z -A -1
