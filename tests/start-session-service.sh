#!/bin/bash

export top_builddir="${top_builddir-.}"
if [ -x /usr/bin/dbus-run-session ]; then
	dbus-run-session -- bash <<"EOF" &
printf "%s" "$DBUS_SESSION_BUS_ADDRESS" > \
	"${top_builddir}"/printerd-session.bus
printf "New D-Bus session bus at %s\n" "$DBUS_SESSION_BUS_ADDRESS"
"${top_builddir}"/src/printerd --session -r -v \
	&> "${top_builddir}"/printerd-session.log &
jobs -p > "${top_builddir}"/printerd-session.pid
printf "printerd started on session bus as PID %s\n" \
	"$(cat "${top_builddir}"/printerd-session.pid)"
wait
EOF
else
	"${top_builddir}"/src/printerd --session -r -v \
		&> "${top_builddir}"/printerd-session.log &
	jobs -p > "${top_builddir}"/printerd-session.pid
	printf "printerd started on session bus as PID %s\n" \
		"$(cat "${top_builddir}"/printerd-session.pid)"
fi

disown %-
sleep 1
kill -0 "$(cat "${top_builddir}"/printerd-session.pid)"
RET="$?"
if [ "${RET}" -ne 0 ]; then
	cat "${top_builddir}"/printerd-session.log
	exit "${RET}"
fi
exit 0
