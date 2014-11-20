[ -n "$VERBOSE" ] && set -x
top_builddir=`cd ${top_builddir-.}; pwd`
PRINTERD="${top_builddir}/src/printerd"
PDCLI="${top_builddir}/tools/pd-cli"
PRINTERD_SESSION_BUS="${top_builddir}"/printerd-session.bus
if [ -r "${PRINTERD_SESSION_BUS}" ]; then
	DBUS_SESSION_BUS_ADDRESS="$(cat "${PRINTERD_SESSION_BUS}")"
	export DBUS_SESSION_BUS_ADDRESS
fi
unset PRINTERD_SESSION_BUS
