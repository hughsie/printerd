#!/usr/bin/python3
from http.server import HTTPServer, BaseHTTPRequestHandler
import os
import socket
import cups
from io import BytesIO
from gi.repository import printerd

cups.require ("1.9.69")

class PdClient:
    IFACE_PRINTERD_PREFIX = "org.freedesktop.printerd"
    IFACE_MANAGER = IFACE_PRINTERD_PREFIX + ".Manager"
    IFACE_DEVICE  = IFACE_PRINTERD_PREFIX + ".Device"
    IFACE_PRINTER = IFACE_PRINTERD_PREFIX + ".Printer"
    IFACE_JOB     = IFACE_PRINTERD_PREFIX + ".Job"

    def __init__ (self):
        self.client = printerd.Client.new_sync ()
        self.object_manager = self.client.get_object_manager ()
        manager = self.object_manager.\
                  get_object ("/org/freedesktop/printerd/Manager").\
                  get_interface (self.IFACE_MANAGER)
        self.manager = manager

    def get_printer (self, objpath):
        return self.object_manager.\
            get_object (objpath).\
            get_interface (self.IFACE_PRINTER)

    def get_job (self, objpath):
        return self.object_manager.\
            get_object (objpath).\
            get_interface (self.IFACE_JOB)

class IPPServer(BaseHTTPRequestHandler):
    def __init__ (self, *args, **kwds):
        self.printerd = None
        super (BaseHTTPRequestHandler, self).__init__ (*args, **kwds)

    def get_printerd (self):
        if self.printerd:
            return

        self.printerd = PdClient ()

    def do_POST (self):
        if not 'content-length' in self.headers:
            self.send_error (501, "Empty request")
            return

        if self.headers.get ('content-type') != "application/ipp":
            self.send_error (501, "Bad content type")
            return

        try:
            length = int (self.headers.get ('content-length'))
        except (TypeError, ValueError):
            length = 0

        data = self.rfile.read (length)
        bytes = BytesIO (data)
        req = cups.IPPRequest ()
        try:
            req.readIO (bytes.read)
        except:
            self.send_error (400)
            return

        self.ipprequest = req
        op = req.operation
        self.log_message ("%s: %s" % (cups.ippOpString (op),
                                      repr (req.attributes)))
        try:
            if op == cups.IPP_OP_CUPS_GET_PRINTERS:
                self.handle_cups_get_printers ()
            else:
                self.send_error (501, "Not implemented")
        except:
            self.send_error (500)
            raise

    def handle_cups_get_printers (self):
        self.send_response (200)
        self.send_header ("Content-type", "application/ipp")
        self.end_headers ()

        req = self.ipprequest
        req.state = cups.IPP_IDLE
        self.get_printerd ()
        printers = self.printerd.manager.call_get_printers_sync ()
        if len (printers) > 0:
            req.statuscode = cups.IPP_OK
        else:
            req.statuscode = cups.IPP_NOT_FOUND

        first = True
        for objpath in printers:
            if not first:
                req.addSeparator ()

            first = False
            printer = self.printerd.get_printer (objpath)

            req.add (cups.IPPAttribute (cups.IPP_TAG_PRINTER,
                                        cups.IPP_TAG_NAME,
                                        "printer-name",
                                        printer.props.name))
            req.add (cups.IPPAttribute (cups.IPP_TAG_PRINTER,
                                        cups.IPP_TAG_URI,
                                        "device-uri",
                                        printer.props.device_uris[0]))

        try:
            req.writeIO (self.wfile.write)
        except:
            self.send_error (501)
            raise

class SocketInheritingIPPServer(HTTPServer):
    """
    An IPPServer subclass that takes over an inherited socket from
    systemd.
    """
    def __init__ (self, address_info, handler, fd, bind_and_activate=True):
        HTTPServer.__init__ (self, address_info, handler,
                            bind_and_activate=False)
        self.socket = socket.fromfd (fd, self.address_family, self.socket_type)
        if bind_and_activate:
            # Only activate, as systemd provides ready-bound sockets.
            self.server_activate ()

server_address = ('', 631)
if os.environ.get ('LISTEN_PID', None) == str (os.getpid ()):
    SYSTEMD_FIRST_SOCKET_FD = 3
    ippd = SocketInheritingIPPServer (server_address, IPPServer,
                                      fd=SYSTEMD_FIRST_SOCKET_FD)
else:
    ippd = HTTPServer (server_address, IPPServer)

try:
    ippd.serve_forever ()
except KeyboardInterrupt:
    pass
