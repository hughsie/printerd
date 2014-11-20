ACLOCAL_AMFLAGS = -I m4

SUBDIRS = 						\
	doc						\
	printerd					\
	data						\
	src						\
	tools						\
	po

DISTCLEANFILES = \
	printerd-session.log \
	printerd-session.pid \
	printerd-session.bus

MAINTAINERCLEANFILES =					\
	$(srcdir)/INSTALL				\
	$(srcdir)/ABOUT-NLS				\
	$(srcdir)/aclocal.m4				\
	$(srcdir)/autoscan.log				\
	$(srcdir)/compile				\
	$(srcdir)/config.guess				\
	$(srcdir)/config.rpath				\
	$(srcdir)/config.h.in				\
	$(srcdir)/config.sub				\
	$(srcdir)/configure.scan			\
	$(srcdir)/depcomp				\
	$(srcdir)/install-sh				\
	$(srcdir)/ltmain.sh				\
	$(srcdir)/missing				\
	$(srcdir)/mkinstalldirs				\
	$(srcdir)/omf.make				\
	$(srcdir)/xmldocs.make				\
	$(srcdir)/gtk-doc.make				\
	$(srcdir)/ChangeLog				\
	$(srcdir)/po/Makefile.in.in~			\
	$(srcdir)/printerd-*.tar.*			\
	`find "$(srcdir)" -type f -name Makefile.in -print`

DISTCHECK_CONFIGURE_FLAGS =				\
	--disable-introspection				\
	--with-systemdsystemunitdir=

distclean-local:
	if test $(srdcir) = .; then :; else \
		rm -f ChangeLog; \
	fi

# Regression tests
TESTS_ENVIRONMENT = top_srcdir=$(top_srcdir) top_builddir=$(top_builddir)
TESTS = \
	tests/version/run-test \
	tests/stop-session-service/run-test

XFAIL_TESTS =

printerd-session.pid: all .FORCE
	bash $(top_srcdir)/tests/start-session-service.sh

tests/version/run-test: printerd-session.pid
	for test in $(TESTS); do \
		chmod a+x $(top_srcdir)/$$test; \
	done

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log --stat -M -C --name-status  --date=short --no-color) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog
.FORCE:

EXTRA_DIST = \
	tests/common.sh tests/start-session-service.sh \
	$(TESTS) $(XFAIL_TESTS)

-include $(top_srcdir)/git.mk
