AC_INIT([printerd],
	[0.0.1],
	[https://github.com/hughsie/printerd/issues],
	[printerd])

AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-xz tar-ustar foreign])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])

AM_MAINTAINER_MODE

AX_CHECK_ENABLE_DEBUG
AC_PROG_CC
AC_ISC_POSIX
AC_HEADER_STDC
AC_PROG_LIBTOOL

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Initialization
#

GNOME_COMMON_INIT
GNOME_COMPILE_WARNINGS([maximum])
GNOME_MAINTAINER_MODE_DEFINES

AC_PATH_PROG([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc is needed])
fi

GTK_DOC_CHECK([1.3])

GOBJECT_INTROSPECTION_CHECK([0.6.2])

# Libraries
#

PKG_CHECK_MODULES(GUDEV, [gudev-1.0 >= 147])
AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.30.2])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GIO, [gio-unix-2.0 >= 2.30.2])
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(POLKIT_GOBJECT_1, [polkit-gobject-1 >= 0.92])
AC_SUBST(POLKIT_GOBJECT_1_CFLAGS)
AC_SUBST(POLKIT_GOBJECT_1_LIBS)

PKG_CHECK_MODULES(POLKIT_AGENT_1, [polkit-agent-1 >= 0.92])
AC_SUBST(POLKIT_AGENT_1_CFLAGS)
AC_SUBST(POLKIT_AGENT_1_LIBS)

AC_PATH_PROG(CUPS_CONFIG, cups-config, no)
if test "x$CUPS_CONFIG" = xno; then
  AC_MSG_ERROR([Please install cups development package])
fi
CUPS_CFLAGS="`$CUPS_CONFIG --cflags`"
CUPS_LIBS="`$CUPS_CONFIG --libs`"
AC_SUBST(CUPS_CFLAGS)
AC_SUBST(CUPS_LIBS)

# systemd
AC_ARG_ENABLE(systemd,
              AS_HELP_STRING([--enable-systemd],[enable systemd [default=yes]]),
	      enable_systemd=$enableval,enable_systemd=yes)
AM_CONDITIONAL(HAVE_SYSTEMD, test x$enable_systemd = xyes)
if test x$enable_systemd = xyes; then
  PKG_CHECK_MODULES(SYSTEMD,
                    [libsystemd >= 200])
  AC_ARG_WITH([systemdsystemunitdir],
              AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
              [],
              [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
  if test "x$with_systemdsystemunitdir" != "xno"; then
    AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
  fi
  AC_DEFINE(HAVE_SYSTEMD, 1, [if we should use systemd])
else
  SYSTEMD_CFLAGS=""
  SYSTEMD_LIBS=""
fi
AC_SUBST(SYSTEMD_CFLAGS)
AC_SUBST(SYSTEMD_LIBS)

# Internationalization
#

IT_PROG_INTLTOOL([$INTLTOOL_REQUIRED])
GETTEXT_PACKAGE=printerd
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])
ALL_LINGUAS="en_GB en_US es fr id pt_BR pt_PT tr_TR uk vi"

# Generate
#

AC_OUTPUT([
Makefile
data/Makefile
printerd/Makefile
printerd/printerd.pc
src/Makefile
tools/Makefile
doc/Makefile
doc/version.xml
doc/man/Makefile
po/Makefile.in
contrib/printerd.spec
])

dnl ==========================================================================
echo "
               printerd $VERSION
             ================

        prefix:                     ${prefix}
        libdir:                     ${libdir}
        libexecdir:                 ${libexecdir}
        bindir:                     ${bindir}
        sbindir:                    ${sbindir}
        datadir:                    ${datadir}
        sysconfdir:                 ${sysconfdir}
        localstatedir:              ${localstatedir}
        docdir:                     ${docdir}
        introspection:              ${found_introspection}
        systemdsystemunitdir:       ${systemdsystemunitdir}

        compiler:                   ${CC}
        cflags:                     ${CFLAGS}
        cppflags:                   ${CPPFLAGS}

        Maintainer mode:            ${USE_MAINTAINER_MODE}
        Building api docs:          ${enable_gtk_doc}
"
