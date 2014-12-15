FROM fedora:21
MAINTAINER Tim Waugh <twaugh@redhat.com>

RUN yum -y update; yum clean all
RUN yum -y install git; yum clean all
RUN yum -y install automake autoconf gnome-common glib2-devel intltool gtk-doc gobject-introspection-devel libgudev1-devel polkit-devel cups-devel systemd-devel make; yum clean all
RUN yum -y install cups cups-filters; yum clean all

RUN mkdir /printerd && cd /printerd && git clone git://github.com/hughsie/printerd.git && cd printerd && ./autogen.sh && ./configure && make && (make check || (cat test-suite.log; exit 1)) && make install 

CMD [ "/usr/local/libexec/printerd" ]
