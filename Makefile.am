ACLOCAL_AMFLAGS = -I m4

SUBDIRS = 						\
	doc						\
	printerd					\
	data						\
	src						\
	tools						\
	po

# Regression tests
PYTHON=python
TEST_BOOKMARK=$(top_builddir)/printerd-session.lines-before-test
TEST_SESSION_LOG=$(top_builddir)/printerd-session.log
TESTS_ENVIRONMENT = \
	top_srcdir=$(top_srcdir) \
	top_builddir=$(top_builddir) \
	PYTHON=$(PYTHON) \
	BOOKMARK=$(TEST_BOOKMARK) \
	SESSION_LOG=$(TEST_SESSION_LOG)

# A rule to start printerd on a session bus of its own
printerd-session.pid:
	bash $(top_srcdir)/tests/start-session-service.sh

# A rule to make the test scripts executable
tests/started/run-test:
	for test in $(TESTS); do \
		chmod a+x $(top_srcdir)/$$test; \
	done

# Here's where most of the tests go.
AFTER_GETPRINTERS_TESTS = \
	tests/createprinter1/run-test \
	tests/createprinter2/run-test \
	tests/createprinter3/run-test \
	tests/createprinter5/run-test \
	tests/updatedriver1/run-test \
	tests/job1/run-test \
	tests/job2/run-test \
	tests/job3/run-test \
	tests/job4/run-test \
	tests/cupsfilter/run-test \
	tests/python1/run-test

# Some tests have to run at the beginning.
ALL_TESTS = \
	tests/started/run-test \
	tests/version/run-test \
	tests/Manager.GetPrinters/run-test \
	$(AFTER_GETPRINTERS_TESTS)

# One test has to run at the end.
STOP_TESTS = \
	tests/stop-session-service/run-test

TESTS = \
	$(ALL_TESTS) \
	$(STOP_TESTS)

# Some tests are known to fail at the moment.
XFAIL_TESTS = \
	tests/createprinter3/run-test \
	tests/python1/run-test

# Start the session daemon before running the tests
$(TESTS:run-test=run-test.log): printerd-session.pid

# Make sure Mananger.GetPrinters is tested before other failed tests
# can leave printers lying around.
$(AFTER_GETPRINTERS_TESTS:run-test=run-test.log): tests/Manager.GetPrinters/run-test.log

# Don't run the stop-session-service test until all others have finished.
$(STOP_TESTS:run-test=run-test.log): $(ALL_TESTS:run-test=run-test.log)

DISTCLEANFILES = \
	$(TEST_SESSION_LOG) \
	$(TEST_BOOKMARK) \
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
	if test $(srcdir) = .; then :; else \
		rm -f ChangeLog; \
	fi

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

EXTRA_DIST = \
	tests/common.sh tests/start-session-service.sh \
	$(TESTS) $(XFAIL_TESTS) \
	README.md TESTS.md \
	contrib/printerd.spec

-include $(top_srcdir)/git.mk
