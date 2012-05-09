EXTRA_DIST =

INCLUDES = \
	-I$(top_builddir) -I$(top_srcdir)	 		\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT			\
	-DPRINTERD_COMPILATION					\
	$(POLKIT_GOBJECT_1_CFLAGS) 				\
	$(GUDEV_CFLAGS) 					\
	$(GLIB_CFLAGS) 						\
	$(GIO_CFLAGS)						\
	$(WARN_CFLAGS)						\
	$(NULL)

# ----------------------------------------------------------------------

#BUILT_SOURCES = 						\
#	pd-daemon-marshal.h					\
#	pd-daemon-marshal.c					\
#	$(NULL)

#pd-daemon-marshal.h: pd-daemon-marshal.list
#	glib-genmarshal $< --prefix=pd_daemon_marshal --header > $@

#pd-daemon-marshal.c: pd-daemon-marshal.list
#	echo "#include \"pd-daemon-marshal.h\"" > $@ && glib-genmarshal $< --prefix=pd_daemon_marshal --body >> $@

#EXTRA_DIST += pd-daemon-marshal.list

# ----------------------------------------------------------------------

noinst_LTLIBRARIES = libprinterddaemon.la

libprinterddaemon_la_SOURCES =					\
	pd-common.h						\
	pd-common.c						\
	pd-daemontypes.h					\
	pd-daemon.h						\
	pd-daemon.c						\
	pd-engine.h						\
	pd-engine.c						\
	pd-manager-impl.h					\
	pd-manager-impl.c					\
	pd-device-impl.h					\
	pd-device-impl.c					\
	pd-printer-impl.h					\
	pd-printer-impl.c					\
	pd-job-impl.h						\
	pd-job-impl.c						\
	$(BUILT_SOURCES)

libprinterddaemon_la_CFLAGS =					\
	-I$(top_srcdir)						\
	-DG_LOG_DOMAIN=\"printerd\"				\
	$(GLIB_CFLAGS)						\
	$(GIO_CFLAGS)						\
	$(GUDEV_CFLAGS)						\
	$(POLKIT_GOBJECT_1_CFLAGS) 				\
	$(NULL)

libprinterddaemon_la_LIBADD =					\
	$(GLIB_LIBS)						\
	$(GIO_LIBS)						\
	$(GUDEV_LIBS)						\
	$(POLKIT_GOBJECT_1_LIBS) 				\
	$(top_builddir)/printerd/libprinterd.la			\
	$(NULL)

# ----------------------------------------------------------------------

libexec_PROGRAMS = printerd

printerd_SOURCES = 						\
	main.c							\
	$(NULL)

printerd_CFLAGS = 						\
	-DG_LOG_DOMAIN=\"printerd\"				\
	$(NULL)

printerd_LDADD = 						\
	$(GLIB_LIBS)						\
	$(GIO_LIBS)						\
	libprinterddaemon.la					\
	$(NULL)

# ----------------------------------------------------------------------

-include $(top_srcdir)/git.mk
