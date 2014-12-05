dbusservicedir       = $(datadir)/dbus-1/system-services
dbusservice_in_files = org.freedesktop.printerd.service.in
dbusservice_DATA     = $(dbusservice_in_files:.service.in=.service)

$(dbusservice_DATA): $(dbusservice_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dbusconf_in_files = org.freedesktop.printerd.conf.in
dbusconf_DATA = $(dbusconf_in_files:.conf.in=.conf)

$(dbusconf_DATA): $(dbusconf_in_files) Makefile
	cp $< $@

systemdservice_in_files = printerd.service.in ippd.service.in
systemdsocket_files = ippd.socket

if HAVE_SYSTEMD
systemdservicedir       = $(DESTDIR)$(systemdsystemunitdir)
systemdservice_DATA     = \
	$(systemdservice_in_files:.service.in=.service) \
	$(systemdsocket_files)

%.service: %.service.in Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@
endif

polkitdir        = $(datadir)/polkit-1/actions
polkit_in_files  = org.freedesktop.printerd.policy.in
polkit_DATA      = $(polkit_in_files:.policy.in=.policy)

@INTLTOOL_POLICY_RULE@

EXTRA_DIST = 						\
	org.freedesktop.printerd.xml			\
	$(systemdservice_in_files)			\
	$(systemdsocket_files)				\
	$(dbusservice_in_files)				\
	$(dbusconf_in_files)				\
	$(polkit_in_files)				\
	$(NULL)

DISTCLEANFILES =					\
	$(dbusservice_DATA)				\
	$(dbusconf_DATA)				\
	$(polkit_DATA)					\
	$(systemdservice_DATA)

clean-local :
	rm -f *~ $(dbusservice_DATA) $(dbusconf_DATA) $(systemdservice_DATA) $(polkit_DATA)

-include $(top_srcdir)/git.mk
