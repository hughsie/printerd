#!/usr/bin/python3
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ForkingMixIn
import os
import socket
import cups
from io import BytesIO
from gi.repository import printerd

cups.require ("1.9.69")

if os.getuid() == 0:
    server_address = ('', 631)
else:
    server_address = ('', 8631)

class ObjectAddress:
    """
    Base class for manipulating object addresses.
    """

    DBUS_PATH_PREFIX = ""
    URI_FORMAT = ""
    ID_TYPE = str

    def __init__ (self, uri=None, path=None, id=None):
        if uri:
            self._id = self.ID_TYPE (uri[uri.rfind ("/") + 1:])
        elif path:
            if not path.startswith (self.DBUS_PATH_PREFIX):
                raise RuntimeError ("Invalid path")

            self._id = self.ID_TYPE (path[len (self.DBUS_PATH_PREFIX):])
        elif id:
            self._id = self.ID_TYPE (id)
        else:
            assert ((uri != None and path == None and id == None) or
                    (path != None and uri == None and id == None) or
                    (id != None and uri == None and path == None))

    def get_uri (self):
        return self.URI_FORMAT % (server_address[0],
                                  server_address[1],
                                  self._id)

    def get_path (self):
        return self.DBUS_PATH_PREFIX + self._id

    def get_id (self):
        return self._id

class PrinterAddress(ObjectAddress):
    DBUS_PATH_PREFIX = "/org/freedesktop/printerd/printer/"
    URI_FORMAT = "ipp://%s:%s/printers/%s"

class JobAddress(ObjectAddress):
    DBUS_PATH_PREFIX = "/org/freedesktop/printerd/job/"
    URI_FORMAT = "ipp://%s:%s/jobs/%s"
    ID_TYPE = int

class Attributes(dict):
    """
    IPP attributes as a dict.
    """

    def __init__ (self, attributes):
        dict.__init__ (self)
        for attribute in attributes:
            self[attribute.name] = attribute

    def get_value (self, k, n=0, d=None):
        """
        Return the named and indexed attribute value if it exists,
        otherwise return default.
        """
        try:
            return self[k].values[n]
        except (KeyError, IndexError):
            return d

class PdClient:
    """
    Class for getting objects using printerd.Client.
    """

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
    """
    Base class implementing common IPP parts.
    """

    IPP_METHODS = {}

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
            method_name = self.IPP_METHODS[op]
            method = getattr (self, method_name)
        except (KeyError, AttributeError):
            # Not implemented
            self.send_error (501)
            return

        try:
            method ()
        except:
            # Internal error
            self.send_error (500)
            raise

        # Send response
        self.wfile.flush ()

    def send_ipp_response (self, req):
        self.send_response (200)
        self.send_header ("Content-type", "application/ipp")
        self.end_headers ()
        req.state = cups.IPP_IDLE
        self.log_message ("Response: %s" % repr (req.attributes))
        req.writeIO (self.wfile.write)

    def send_ipp_error (self, statuscode):
        req = self.ipprequest
        req.statuscode = statuscode
        self.send_ipp_response (req)

class PdIPPServer(IPPServer):
    """
    Class providing implementations of IPP operations.
    """

    IPP_METHODS = { cups.IPP_OP_CUPS_GET_PRINTERS: "ipp_CUPS_Get_Printers" }

    def ipp_CUPS_Get_Printers (self):
        req = self.ipprequest
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

            name = PrinterAddress (path=objpath).get_id ()
            req.add (cups.IPPAttribute (cups.IPP_TAG_PRINTER,
                                        cups.IPP_TAG_NAME,
                                        "printer-name",
                                        name))
            req.add (cups.IPPAttribute (cups.IPP_TAG_PRINTER,
                                        cups.IPP_TAG_URI,
                                        "device-uri",
                                        printer.props.device_uris[0]))

        self.send_ipp_response (req)

class ForkingHTTPServer(ForkingMixIn, HTTPServer):
    pass

class SocketInheritingIPPServer(ForkingHTTPServer):
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

if os.environ.get ('LISTEN_PID', None) == str (os.getpid ()):
    SYSTEMD_FIRST_SOCKET_FD = 3
    ippd = SocketInheritingIPPServer (server_address, PdIPPServer,
                                      fd=SYSTEMD_FIRST_SOCKET_FD)
else:
    ippd = HTTPServer (server_address, PdIPPServer)

if __name__ == '__main__':
    try:
        ippd.serve_forever ()
    except KeyboardInterrupt:
        pass
