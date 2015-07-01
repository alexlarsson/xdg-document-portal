NULL =

AM_CPPFLAGS =						\
	$(NULL)

libexec_PROGRAMS = \
	xdg-document-portal \
	$(NULL)

bin_PROGRAMS = \
	xdp \
	$(NULL)

dbus_built_sources = xdp-dbus.c xdp-dbus.h
BUILT_SOURCES = $(dbus_built_sources)

$(dbus_built_sources) : Makefile.am org.freedesktop.portal.documents.xml
	$(AM_V_GEN) $(GDBUS_CODEGEN)				\
		--interface-prefix org.freedesktop.portal.	\
		--c-namespace XdpDbus				\
		--generate-c-code xdp-dbus				\
		$(srcdir)/org.freedesktop.portal.documents.xml		\
		$(NULL)

resource_files = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/xdg-document-portal.gresource.xml)

xdp-resources.h: xdg-document-portal.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(srcdir) --c-name _xdg_document --generate-header

xdp-resources.c: xdg-document-portal.gresource.xml $(resource_files)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(srcdir) --c-name _xdg_document --generate-source

# D-BUS service file
%.service: %.service.in config.log
	$(AM_V_GEN) $(SED) -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

servicedir = $(DBUS_SERVICE_DIR)
service_in_files = org.freedesktop.portal.DocumentPortal.service.in
service_DATA = org.freedesktop.portal.DocumentPortal.service

xdg_document_portal_SOURCES = \
	xdp-main.c		\
	xdp-error.c		\
	xdp-error.h		\
	xdp-enums.h		\
	xdp-doc-db.h		\
	xdp-doc-db.c		\
	gvdb/gvdb-reader.c	\
	gvdb/gvdb-builder.c	\
	$(dbus_built_sources)	\
	xdp-resources.h		\
	xdp-resources.c		\
	xdp-util.h		\
	xdp-util.c		\
	$(NULL)

xdg_document_portal_LDADD = $(BASE_LIBS)
xdg_document_portal_CFLAGS = $(BASE_CFLAGS)

xdp_SOURCES = \
	xdp-tool.c		\
	xdp-cat.c		\
	xdp-add.c		\
	xdp-add-local.c		\
	xdp-new.c		\
	xdp-new-local.c		\
	xdp-update.c		\
	xdp-info.c		\
	$(dbus_built_sources)	\
	$(NULL)

xdp_LDADD = $(BASE_LIBS)
xdp_CFLAGS = $(BASE_CFLAGS)
