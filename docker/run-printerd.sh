#!/bin/sh

# Start our own system D-Bus
dbus-daemon --system --fork

# Start up ippd
/usr/libexec/ippd &

# Start up printerd
exec /usr/libexec/printerd -v
