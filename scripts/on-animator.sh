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
# (And also because eating > 50% CPU to draw a progress bar is ridiculous).
FBINK_SHIM_BIN="/usr/local/kfmon/bin/shim"

# NOTE: While this works as-is on current FW, this *may* be problematic on older FW,
#       where Nickel *might* have been relying on pickel to setup the fb...
#       This could probably be worked-around by shipping and using fbdepth like we do on KOReader,
#       (except only for the rotation, in order not to break old 16bpp only FW versions).
#       Other custom stuff that relies on the pickel setup will be left in the lurch regardless, though...

# NOTE: There's a bit of trickery involved where we have to launch FBInk under the on-animator.sh process name,
#       just so it gets killed when on-animator gets the axe,
#       because that's done in a way we can't do anything about from here (SIGKILL, which isn't propagated, and isn't trappable).
#       Ideally, we'd use exec -a, but busybox doesn't support that flag, so, instead,
#       we exec a shim binary that just execs FBInk under a different process name,
#       and with the relevant options for what we want to do...
exec ${FBINK_SHIM_BIN}
