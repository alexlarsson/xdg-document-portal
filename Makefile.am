NULL =

AM_CPPFLAGS =						\
	$(NULL)

libexec_PROGRAMS = \
	xdg-document-portal \
	$(NULL)

dbus_built_sources = xdg-document-portal-dbus.c xdg-document-portal-dbus.h
BUILT_SOURCES = $(dbus_built_sources)

$(dbus_built_sources) : Makefile.am org.freedesktop.portal.documents.xml
	$(AM_V_GEN) $(GDBUS_CODEGEN)				\
		--interface-prefix org.freedesktop.portal.	\
		--c-namespace XdpDbus				\
		--generate-c-code xdg-document-portal-dbus		\
		$(srcdir)/org.freedesktop.portal.documents.xml		\
		$(NULL)

resource_files = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/xdg-document-portal.gresource.xml)

xdg-document-portal-resources.h: xdg-document-portal.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(srcdir) --c-name _xdg_document --generate-header

xdg-document-portal-resources.c: xdg-document-portal.gresource.xml $(resource_files)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(srcdir) --c-name _xdg_document --generate-source

# D-BUS service file
%.service: %.service.in config.log
	$(AM_V_GEN) $(SED) -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

servicedir = $(DBUS_SERVICE_DIR)
service_in_files = org.freedesktop.portal.DocumentPortal.service.in
service_DATA = org.freedesktop.portal.DocumentPortal.service

xdg_document_portal_SOURCES = \
	xdg-document-portal.c	\
	xdp-document.c		\
	xdp-document.h		\
	$(dbus_built_sources)		\
	xdg-document-portal-resources.h		\
	xdg-document-portal-resources.c		\
	$(NULL)

xdg_document_portal_LDADD = $(BASE_LIBS) $(GOM_LIBS)
xdg_document_portal_CFLAGS = $(BASE_CFLAGS) $(GOM_CFLAGS)
