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
PD_DEST=org.freedesktop.printerd
PD_PATH=/org/freedesktop/printerd
PD_IFACE=org.freedesktop.printerd

BOOKMARK="${BOOKMARK-"${top_builddir}/printerd-session.lines-before-test"}"
SESSION_LOG="${SESSION_LOG-"${top_builddir}"/printerd-session.log}"
# Call this function to report test results
result_is () {
  if [ "$1" -eq 0 ]; then
    n=$(cat "${BOOKMARK}")
    if grep -q '\*\*' \
      <(sed -e "1,${n}d" "${SESSION_LOG}"); then
      printf "Warnings/errors in output:\n"
      sed -e "1,${n}d" "${SESSION_LOG}"
      exit 1
    fi
  fi
  exit "$1"
}

# Remember where we're up to in printerd's output
wc -l < "${SESSION_LOG}" > "${BOOKMARK}"
