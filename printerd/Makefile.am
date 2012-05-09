
INCLUDES = \
	-I$(top_builddir) -I$(top_srcdir)				\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\"			\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"				\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\"				\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\"				\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\"				\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT				\
	-DPRINTERD_COMPILATION						\
	$(GLIB_CFLAGS)							\
	$(GIO_CFLAGS)							\
	$(WARN_CFLAGS)							\
	$(NULL)

$(dbus_built_sources) : Makefile.am $(top_srcdir)/data/org.freedesktop.printerd.xml
	gdbus-codegen							\
		--interface-prefix org.freedesktop.printerd.		\
		--c-namespace Pd					\
		--c-generate-object-manager				\
		--generate-c-code pd-generated				\
		--generate-docbook pd-generated-doc			\
		$(top_srcdir)/data/org.freedesktop.printerd.xml		\
		$(NULL)

pd-enumtypes.h: pd-enums.h pd-enumtypes.h.template
	( top_builddir=`cd $(top_builddir) && pwd`;					\
	 cd $(srcdir) && glib-mkenums --template pd-enumtypes.h.template pd-enums.h ) >			\
	   pd-enumtypes.h.tmp && mv pd-enumtypes.h.tmp pd-enumtypes.h

pd-enumtypes.c: pd-enums.h pd-enumtypes.c.template
	( top_builddir=`cd $(top_builddir) && pwd`;					\
	 cd $(srcdir) && glib-mkenums --template pd-enumtypes.c.template pd-enums.h ) >			\
	   pd-enumtypes.c.tmp && mv pd-enumtypes.c.tmp pd-enumtypes.c

enum_built_sources =							\
	pd-enumtypes.h							\
	pd-enumtypes.c							\
	$(NULL)

dbus_built_sources =							\
	pd-generated.h							\
	pd-generated.c							\
	$(NULL)

BUILT_SOURCES =								\
	$(enum_built_sources)						\
	$(dbus_built_sources)						\
	$(NULL)

lib_LTLIBRARIES = libprinterd.la

libprinterdincludedir=$(includedir)/printerd/printerd

libprinterdinclude_HEADERS=						\
	printerd.h							\
	pd-client.h							\
	pd-enums.h							\
	pd-enumtypes.h							\
	pd-error.h							\
	pd-types.h							\
	pd-generated.h							\
	$(NULL)

libprinterd_la_SOURCES =						\
	$(BUILT_SOURCES)						\
	pd-client.h							\
	pd-client.c							\
	pd-enums.h							\
	pd-error.h							\
	pd-error.c							\
	pd-types.h							\
	$(NULL)

libprinterd_la_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"libprinterd\"					\
	$(NULL)

libprinterd_la_CFLAGS =							\
	$(GLIB_CFLAGS)							\
	$(GIO_CFLAGS)							\
	$(NULL)

libprinterd_la_LIBADD =							\
	$(GLIB_LIBS)							\
	$(GIO_LIBS)							\
	$(NULL)

# ----------------------------------------------------------------------

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = printerd.pc

# ----------------------------------------------------------------------

if HAVE_INTROSPECTION

INTROSPECTION_GIRS = printerd-1.0.gir

girdir = $(INTROSPECTION_GIRDIR)
gir_DATA = printerd-1.0.gir

typelibsdir = $(INTROSPECTION_TYPELIBDIR)
typelibs_DATA = printerd-1.0.typelib

printerd_1_0_gir_SCANNERFLAGS =			\
	--c-include='printerd/printerd.h'	\
	--warn-all				\
	--namespace printerd			\
	--identifier-prefix Pd			\
	--symbol-prefix pd			\
	--warn-all				\
	$(NULL)

printerd_1_0_gir_CFLAGS =			\
	$(libprinterd_la_CFLAGS)		\
	-DPRINTERD_COMPILATION			\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	$(NULL)

printerd-1.0.gir: libprinterd.la
printerd_1_0_gir_INCLUDES = Gio-2.0
printerd_1_0_gir_LIBS = libprinterd.la
printerd_1_0_gir_FILES = $(libprinterd_la_SOURCES)
printerd_1_0_gir_EXPORT_PACKAGES = printerd

include $(INTROSPECTION_MAKEFILE)

endif # HAVE_INTROSPECTION

# ----------------------------------------------------------------------

CLEANFILES = pd-generated-doc-*.xml pd-generated.[ch] $(gir_DATA) $(typelibs_DATA)

EXTRA_DIST =								\
	printerd.pc.in							\
	pd-enumtypes.h.template						\
	pd-enumtypes.c.template						\
	$(NULL)

dist-hook :
	(for i in $(BUILT_SOURCES) ; do rm -f $(distdir)/$$i ; done)

clean-local :
	rm -f *~ $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
