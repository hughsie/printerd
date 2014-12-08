Running the tests
=================

As printerd is built using automake, `make check` runs the tests.

The way the tests are organised is that firstly, a new session D-Bus
message bus is started and printerd is started for it.

Next, the "started" and "Manager.GetPrinters" tests are run. These
have to be run first of all.

Once all the other tests have run, a special "stop-session-service"
test runs which stops the running printerd and checks its logs for
errors.

These dependencies between tests are accounted for in `Makefile.am`,
meaning that `make check -j` will do the right thing, running multiple
tests concurrently against the session printerd service.

Tips
----

Some tips for debugging problems found using the test suite:

* To simply start printerd on its own session bus, you can use `make
  printerd-session.pid`.
* To stop a printerd service started that way, you can run
  `make stop-session-service`.
* To run all the tests without stopping the service at the end, run
  `make check STOP_TESTS=`.
* To attach gdb to the running printerd, be aware that it was started
  using libtool and so you'll need to run `gdb src/.libs/lt-printerd
  $(cat printerd-session.pid)`.
* You can run ippd against the session printerd service with `make
  run-ippd`.
* Similarly, you can run pd-view against the session printerd service
  with `make run-pd-view`.
