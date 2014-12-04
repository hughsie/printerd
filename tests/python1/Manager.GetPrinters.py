#!/usr/bin/python
from gi.repository import printerd
client = printerd.Client.new_sync ()
objmgr = client.get_object_manager ()
mgr = objmgr.\
      get_object ("/org/freedesktop/printerd/Manager").\
      get_interface ("org.freedesktop.printerd.Manager")
printers = mgr.call_get_printers_sync ()
for printer in printers:
    print (printer)
