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
    TVCOL_NAME  = 0
    TVCOL_PATH  = 1
    TVCOL_IFACE = 2

    def __init__ (self):
        super (GObject.GObject, self).__init__ ()
        self.mainwindow = Gtk.Window ()
        treeview = Gtk.TreeView ()
        self.store = Gtk.TreeStore (GObject.TYPE_STRING,
                                    GObject.TYPE_STRING,
                                    GObject.TYPE_OBJECT)
        treeview.set_model (self.store)
        for name, col, setter in [("Name", self.TVCOL_NAME, None),
                                  ("State", None, self.update_state),
                                  ("Path", self.TVCOL_PATH, None)]:
            cell = Gtk.CellRendererText ()
            cell.set_property ("ellipsize", Pango.EllipsizeMode.END)
            cell.set_property ("width-chars", 20)
            if setter:
                column.set_cell_data_func (cell, setter, None)
                column = Gtk.TreeViewColumn (name, cell)
            else:
                column = Gtk.TreeViewColumn (name, cell, text=col)

            column.set_resizable (True)
            treeview.append_column (column)

        self.mainwindow.add (treeview)
        self.mainwindow.show_all ()
        self.mainwindow.connect ("delete-event", Gtk.main_quit)
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
                if iface.get_info ().name == IFACE_PRINTER:
                    self.interface_added (manager, obj, iface)

        for obj in objects:
            ifaces = obj.get_interfaces ()
            for iface in ifaces:
                if iface.get_info ().name == IFACE_JOB:
                    self.interface_added (manager, obj, iface)

    def update_state (self, column, cell, model, iter, data):
        iface = model.get_value (iter, self.TVCOL_IFACE)
        name = iface.get_info ().name
        state = {}
        if name == IFACE_PRINTER:
            state = { 3: "Idle",
                      4: "Processing",
                      5: "Stopped" }
        elif name == IFACE_JOB:
            state = { 3: "Pending",
                      4: "Held",
                      5: "Processing",
                      6: "Paused",
                      7: "Canceled",
                      8: "Aborted",
                      9: "Completed" }

        state_str = state.get (iface.get_property ('state'), "Unknown")
        cell.set_property ("text", state_str)

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
        if name == IFACE_PRINTER:
            self.printer_added (obj, iface)
        elif name == IFACE_JOB:
            self.job_added (obj, iface)

    def printer_added (self, obj, iface):
        path = obj.get_object_path ()
        print ("Printer at %s" % path)
        iter = self.store.append (None)
        self.printers[path] = iter
        self.store.set (iter, self.TVCOL_NAME, iface.get_property ('name'))
        self.store.set (iter, self.TVCOL_PATH, path)
        self.store.set (iter, self.TVCOL_IFACE, iface)
        iface.connect ("notify::state", self.printer_changed)

    def job_added (self, obj, iface):
        path = obj.get_object_path ()
        print ("Job at %s" % path)
        printer = iface.get_property ('printer')
        if not printer in self.printers:
            print ("Orphan job?!")
            return

        iter = self.store.append (self.printers[printer])
        self.jobs[path] = iter
        self.store.set (iter, self.TVCOL_NAME, iface.get_property ('name'))
        self.store.set (iter, self.TVCOL_PATH, path)
        self.store.set (iter, self.TVCOL_IFACE, iface)
        iface.connect ("notify::state", self.job_changed)

    def job_changed (self, iface, param):
        obj = iface.get_object ()
        path = obj.get_object_path ()
        print (path + " changed")
        if path not in self.jobs:
            print ("Non-existent job changed?!")
            return

        iter = self.jobs[path]
        self.store.set_value (iter, self.TVCOL_IFACE, iface)

    def printer_changed (self, iface, param):
        obj = iface.get_object ()
        path = obj.get_object_path ()
        print (path + " changed")
        if path not in self.printers:
            print ("Non-existent printer changed?!")
            return

        iter = self.printers[path]
        self.store.set_value (iter, self.TVCOL_IFACE, iface)

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
