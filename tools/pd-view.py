#!/usr/bin/python3
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA	02110-1301	USA

from gi.repository import printerd
from gi.repository import GObject
from gi.repository import Gtk
from gi.repository import Pango

IFACE_PRINTERD_PREFIX = "org.freedesktop.printerd"

IFACE_MANAGER = IFACE_PRINTERD_PREFIX + ".Manager"
IFACE_DEVICE  = IFACE_PRINTERD_PREFIX + ".Device"
IFACE_PRINTER = IFACE_PRINTERD_PREFIX + ".Printer"
IFACE_JOB     = IFACE_PRINTERD_PREFIX + ".Job"

class MainWindow(GObject.GObject):
    TVCOL_ID      = 0
    TVCOL_NAME    = 1
    TVCOL_STATE   = 2
    TVCOL_REASONS = 3
    TVCOL_PATH    = 4
    TVCOL_IFACE   = 5

    def __init__ (self):
        super (GObject.GObject, self).__init__ ()
        self.mainwindow = Gtk.Window ()
        treeview = Gtk.TreeView ()
        self.store = Gtk.TreeStore (GObject.TYPE_STRING,
                                    GObject.TYPE_STRING,
                                    GObject.TYPE_STRING,
                                    GObject.TYPE_STRING,
                                    GObject.TYPE_STRING,
                                    GObject.TYPE_OBJECT)
        treeview.set_model (self.store)
        for name, col in [("ID", self.TVCOL_ID),
                          ("Name", self.TVCOL_NAME),
                          ("State", self.TVCOL_STATE),
                          ("Reasons", self.TVCOL_REASONS),
                          ("Path", self.TVCOL_PATH)]:
            cell = Gtk.CellRendererText ()
            cell.set_property ("ellipsize", Pango.EllipsizeMode.END)
            cell.set_property ("width-chars", 20)
            column = Gtk.TreeViewColumn (name, cell, text=col)
            column.set_resizable (True)
            treeview.append_column (column)

        self.mainwindow.add (treeview)
        self.mainwindow.show_all ()
        self.mainwindow.connect ("delete-event", Gtk.main_quit)
        self.devices = dict()  # D-Bus path to store iter
        self.printers = dict() # D-Bus path to store iter
        self.jobs = dict()     # D-Bus path to store iter

        self.client = printerd.Client.new_sync (None)
        manager = self.client.get_object_manager ()
        manager.connect ('object-added', self.object_added)
        manager.connect ('object-removed', self.object_removed)
        objects = manager.get_objects ()
        for obj in objects:
            ifaces = obj.get_interfaces ()
            for iface in ifaces:
                if iface.get_info ().name in [IFACE_DEVICE, IFACE_PRINTER]:
                    self.interface_added (manager, obj, iface)

        for obj in objects:
            ifaces = obj.get_interfaces ()
            for iface in ifaces:
                if iface.get_info ().name == IFACE_JOB:
                    self.interface_added (manager, obj, iface)

    def object_added (self, manager, obj):
        print ("object added: %s" % repr (obj))
        obj.connect ('interface-added', self.interface_added)
        obj.connect ('interface-removed', self.interface_removed)
        ifaces = obj.get_interfaces ()
        for iface in ifaces:
            self.interface_added (manager, obj, iface)

    def interface_added (self, manager, obj, iface):
        print ("interface added: %s" % repr (iface))
        name = iface.get_info ().name
        if name == IFACE_DEVICE:
            self.device_added (obj, iface)
        elif name == IFACE_PRINTER:
            self.printer_added (obj, iface)
        elif name == IFACE_JOB:
            self.job_added (obj, iface)

    def device_added (self, obj, iface):
        path = obj.get_object_path ()
        print ("Device at %s" % path)
        iter = self.store.append (None)
        self.devices[path] = iter
        self.store.set (iter, self.TVCOL_NAME, "Device")
        self.store.set (iter, self.TVCOL_PATH, path)
        self.store.set (iter, self.TVCOL_IFACE, iface)

    def printer_added (self, obj, iface):
        path = obj.get_object_path ()
        print ("Printer at %s" % path)
        printeriter = self.store.append (None)
        self.printers[path] = printeriter
        name = iface.get_property ('name')
        self.store.set (printeriter, self.TVCOL_NAME, name)
        self.store.set (printeriter, self.TVCOL_PATH, path)
        self.store.set (printeriter, self.TVCOL_IFACE, iface)
        iface.connect ("notify::state", self.printer_state_changed)
        iface.connect ("notify::state-reasons",
                       self.printer_state_reasons_changed)

        self.set_printer_state (printeriter, iface)
        self.set_printer_state_reasons (printeriter, iface)

    def get_printer_path_from_iface (self, iface):
        obj = iface.get_object ()
        path = obj.get_object_path ()
        if path not in self.printers:
            return None

        return path

    def set_printer_state (self, printeriter, iface):
        statemap = { 3: "Idle",
                     4: "Processing",
                     5: "Stopped" }
        state = iface.get_property ('state')
        state_str = statemap.get (state, "Unknown")
        self.store.set_value (printeriter, self.TVCOL_STATE, state_str)

    def set_printer_state_reasons (self, printeriter, iface):
        reasons = iface.get_property ('state-reasons')
        self.store.set_value (printeriter, self.TVCOL_REASONS, str (reasons))

    def printer_state_changed (self, iface, param):
        path = self.get_printer_path_from_iface (iface)
        if path == None:
            print ("Non-existent printer changed?!")
            return

        print (path + " changed state")
        self.set_printer_state (self.printers[path], iface)

    def printer_state_reasons_changed (self, iface, param):
        path = self.get_printer_path_from_iface (iface)
        if path == None:
            print ("Non-existent printer changed?!")
            return

        print (path + " changed state-reasons")
        self.set_printer_state_reasons (self.printers[path], iface)

    def job_added (self, obj, iface):
        path = obj.get_object_path ()
        print ("Job at %s" % path)
        printer = iface.get_property ('printer')
        if not printer in self.printers:
            print ("Orphan job?!")
            return

        jobiter = self.store.append (self.printers[printer])
        self.jobs[path] = jobiter
        self.store.set (jobiter, self.TVCOL_ID, str (iface.get_property ('id')))
        self.store.set (jobiter, self.TVCOL_NAME, iface.get_property ('name'))
        self.store.set (jobiter, self.TVCOL_PATH, path)
        self.store.set (jobiter, self.TVCOL_IFACE, iface)
        iface.connect ("notify::state", self.job_state_changed)
        iface.connect ("notify::state-reasons", self.job_state_reasons_changed)

        self.set_job_state (jobiter, iface)
        self.set_job_state_reasons (jobiter, iface)

    def get_job_path_from_iface (self, iface):
        obj = iface.get_object ()
        path = obj.get_object_path ()
        if path not in self.jobs:
            return None

        return path

    def set_job_state (self, jobiter, iface):
        statemap = { 3: "Pending",
                     4: "Held",
                     5: "Processing",
                     6: "Paused",
                     7: "Canceled",
                     8: "Aborted",
                     9: "Completed" }
        state = iface.get_property ('state')
        state_str = statemap.get (state, "Unknown")
        self.store.set_value (jobiter, self.TVCOL_STATE, state_str)

    def job_state_changed (self, iface, param):
        path = self.get_job_path_from_iface (iface)
        if path == None:
            print ("Non-existent job changed?!")
            return

        print (path + " changed state")
        self.set_job_state (self.jobs[path], iface)

    def set_job_state_reasons (self, jobiter, iface):
        reasons = iface.get_property ('state-reasons')
        self.store.set_value (jobiter, self.TVCOL_REASONS, str (reasons))

    def job_state_reasons_changed (self, iface, param):
        path = self.get_job_path_from_iface (iface)
        if path == None:
            print ("Non-existent job changed?!")
            return

        print (path + " changed state-reasons")
        self.set_job_state_reasons (self.jobs[path], iface)

    def object_removed (self, manager, obj):
        path = obj.get_property ('g-object-path')
        print ("object removed: %s" % path)
        if path in self.jobs:
            iter = self.jobs[path]
        elif path in self.printers:
            iter = self.printers[path]
            while self.store.iter_has_child (iter):
                print ("Iter for path %s has child; removing" % path)
                jobiter = self.store.iter_nth_child (iter, 0)
                jobpath = self.store.get_value (jobiter, self.TVCOL_PATH)
                del self.jobs[jobpath]
                self.store.remove (jobiter)
        elif path in self.devices:
            iter = self.devices[path]
        else:
            print ("unknown path: %s" % path)
            return

        if iter == None:
            return

        self.store.remove (iter)

    def interface_removed (self, manager, obj, iface):
        print ("interface removed: %s" % repr (iface))
        name = iface.get_info ().name
        path = obj.get_object_path ()
        if name == IFACE_JOB:
            if not path in self.jobs:
                print ("Non-existent job removed?!")
                return

            self.store.remove (self.jobs[path])
            del self.jobs[path]
        elif name == IFACE_PRINTER:
            if not path in self.printers:
                print ("Non-existent job removed?!")
                return

            self.store.remove (self.printers[path])
            del self.printers[path]

mainwindow = MainWindow ()
Gtk.main ()
