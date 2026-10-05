#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage: tests/send_slcan.sh UART SCENARIO

UART is a serial device such as /dev/ttyUSB0, or - to write frames to stdout.
SCENARIO is one of: pdc-on, pdc-near, pdc-off, pdc-demo, next, previous,
play, stop.
The serial port defaults to 750000 baud, 8N1 (override with SLCAN_BAUD).
picocom is needed to configure nonstandard baud rates such as 750000.
Connect the sender's TX to the device's UART3 RX and share ground.
EOF
	exit 2
}

[[ $# == 2 ]] || usage
uart=$1
scenario=$2
case $scenario in
	pdc-on|pdc-near|pdc-off|pdc-demo|next|previous|play|stop) ;;
	*) usage ;;
esac

if [[ $uart == - ]]; then
	exec 3>&1
else
	[[ -c $uart && -w $uart ]] || { printf 'Cannot write to UART: %s\n' "$uart" >&2; exit 1; }
	command -v picocom >/dev/null || { printf 'picocom is required to configure the UART\n' >&2; exit 1; }
	picocom --baud "${SLCAN_BAUD:-750000}" --flow n --parity n \
		--databits 8 --stopbits 1 --noreset --quiet --exit "$uart" >/dev/null
	exec 3>"$uart"
fi

send_frame() {
	printf '%s\r' "$1" >&3
}

case $scenario in
	pdc-on)
		# CAN 0x1C2: eight distances, then 0x24A: show PDC.
		send_frame t1C28C8966432326496C8
		send_frame t24A105
		;;
	pdc-near)
		send_frame t1C280A3232646432320A
		;;
	pdc-off)
		send_frame t24A106
		;;
	pdc-demo)
		send_frame t1C28C8966432326496C8
		send_frame t24A105
		sleep 1
		send_frame t1C280A3232646432320A
		sleep 1
		send_frame t24A106
		;;
	next)
		# CAN 0x1D6: Up press, then release so the next press works.
		send_frame t1D62E00C
		send_frame t1D62C00C
		;;
	previous)
		send_frame t1D62D00C
		send_frame t1D62C00C
		;;
	play|stop)
		if [[ $scenario == play ]]; then press=t1D62E00C; else press=t1D62D00C; fi
		# Repeat the held state long enough to cross the one-second threshold.
		send_frame "$press"
		for ((i = 0; i < 12; ++i)); do
			sleep 0.1
			send_frame "$press"
		done
		send_frame t1D62C00C
		;;
esac
