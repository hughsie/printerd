printerd
========

printerd is a daemon to manage local and remote printers on Linux.

It is a polkit-aware system activatable D-Bus service.

To play around with it, after running 'make install' you can run the
daemon as root in verbose mode from the command line:

```
su -c '/usr/local/libexec/printerd -v'
```

Now you can interact with it using the supplied `pd-client` and
`pd-view` tools, or with [d-feet](http://live.gnome.org/DFeet/),
or with the "gdbus" tool:

```
  gdbus monitor --system --dest org.freedesktop.printerd
```

Note that currently there is no SELinux policy for printerd so when
submitting files for printing you will need to enable permissive mode
using `setenforce 0`.

The D-Bus objects are:

*  Manager: for creating print queues with a specific device URI
*  Device: a detected printer device
* Printer: a print queue
* Job: a print job

The Device object provides a method for creating a Printer object for
it.  The Printer object provides a method for creating a job.

To print a file, the Printer object's CreateJob method is called.
This returns an object path for the new job.  The new Job object's
AddDocument method is used to add a file to the job, and finally the
Job object's Start method is used to allow the job to be processed.

The core IPP concepts are used for job and printer states, state
reasons, and so on.  The plan is that an IPP server could be built
out-of-process, which would be a client for the printerd D-Bus
interface.

Translations can be submitted at [Transifex](https://www.transifex.com/projects/p/printerd/).

Structure
---------

The `data/` directory contains:
* the D-Bus interface, configuration, and service definition,
* the polkit configuration, and
* the systemd service file

in the `printerd/` directory is client code for communicating with
printerd from C and using GObject introspection, as well as code
generated from the interface definition.

The service implementation lives in the `src/` directory. The service
is implemented using GDBusObjectManager to export the various objects
to D-Bus. This involves having skeleton objects such as PdJob, which
hold the properties defined in the interface, and implementation
objects such as PdJobImpl (inheriting from PdJob) which provide the
method implementations.

The Printer and Device objects set a flag telling the
GDBusObjectManager to handle each D-Bus method invocation in its own
thread. The Job object handles all method invocations in the main
context.

Orchestrating the Device, Printer, and Job objects is the
PdEngine. The engine decides on actions, for example when to start
jobs.
