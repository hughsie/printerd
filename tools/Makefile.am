EXTRA_DIST =

AM_CPPFLAGS = \
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
	$(GLIB_CFLAGS) 						\
	$(GIO_CFLAGS)						\
	$(WARN_CFLAGS)						\
	$(NULL)

# ----------------------------------------------------------------------

bin_PROGRAMS = pd-cli

pd_cli_SOURCES =						\
	pd-cli.c						\
	$(NULL)

pd_cli_CFLAGS =							\
	-DG_LOG_DOMAIN=\"printerd\"				\
	$(NULL)

pd_cli_LDADD =							\
	$(GLIB_LIBS)						\
	$(top_builddir)/src/libprinterddaemon.la		\
	$(NULL)



-include $(top_srcdir)/git.mk
