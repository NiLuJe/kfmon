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
# NOTE: This requires some extra shenanigans so that FBInk will properly get killed when on-animator gets the axe from Nickel...
#       c.f., https://unix.stackexchange.com/a/444676
prep_term()
{
	unset term_child_pid
	unset term_kill_needed
	trap 'handle_term' TERM INT
}

handle_term()
{
	if [ "${term_child_pid}" ]; then
		kill -TERM "${term_child_pid}" 2>/dev/null
	else
		term_kill_needed="yes"
	fi
}

wait_term()
{
	term_child_pid=$!
	if [ "${term_kill_needed}" ]; then
		kill -TERM "${term_child_pid}" 2>/dev/null
	fi
	wait ${term_child_pid}
	trap - TERM INT
	wait ${term_child_pid}
}

FBINK_BIN="/usr/local/kfmon/bin/fbink"

# shellcheck disable=SC2046
eval $(${FBINK_BIN} -e)

prep_term
# Make it fast, centered, and larger than usual.
${FBINK_BIN} -q -M -W A2 -A -1 -S $(( FONTSIZE_MULT * 2 )) &
wait_term
