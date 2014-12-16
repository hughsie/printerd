FROM fedora:21
MAINTAINER Tim Waugh <twaugh@redhat.com>

# Apply updates
RUN yum -y update; yum clean all

# Install build tools
RUN yum -y install git automake autoconf intltool gtk-doc gnome-common; yum clean all

# Install build dependencies
RUN yum -y install glib2-devel gobject-introspection-devel libgudev1-devel polkit-devel cups-devel systemd-devel make; yum clean all

# Install printerd runtime dependencies (for test-suite)
RUN yum -y install cups cups-filters; yum clean all

# Build and install
RUN mkdir /printerd && cd /printerd && git clone git://github.com/hughsie/printerd.git && cd printerd && ./autogen.sh --prefix=/usr && make && (make check || (cat test-suite.log; exit 1)) && make install

# Install ippd runtime dependencies
RUN yum -y install python3 python3-gobject python3-cups; yum clean all
RUN yum -y update --enablerepo=updates-testing python3-cups; yum clean all

# Prepare to start everything up
RUN mkdir -p /var/run/dbus
ADD run-printerd.sh /printerd/run-printerd.sh
RUN chmod +x /printerd/run-printerd.sh
CMD [ "/printerd/run-printerd.sh" ]
