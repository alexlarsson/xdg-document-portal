#include "config.h"

#include <locale.h>
#include <gio/gio.h>

#include "xdp-dbus.h"


int
main (int    argc,
      char **argv)
{
  GDBusConnection *bus;
  XdpDbusDocumentPortal *proxy;
  g_autoptr(GFile) file = NULL;
  g_autofree char *uri = NULL;
  char *appid;
  gint64 handle;
  g_autoptr(GError) error = NULL;

  setlocale (LC_ALL, "");

  if (argc < 3)
    {
      g_printerr ("Missing arg\n");
      return 1;
    }

  g_set_prgname (argv[0]);

  file = g_file_new_for_commandline_arg (argv[1]);
  uri = g_file_get_uri (file);
  appid = argv[2];

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (bus == NULL)
    {
      g_printerr ("Can't get session bus: %s\n", error->message);
      return 1;
    }

  proxy = xdp_dbus_document_portal_proxy_new_sync (bus,
                                                   G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                   "org.freedesktop.portal.DocumentsPortal",
                                                   "/org/freedesktop/portal/document",
                                                   NULL,
                                                   &error);
  if (proxy == NULL)
    {
      g_printerr ("Can't create document proxy: %s\n", error->message);
      return 1;
    }

  if (!xdp_dbus_document_portal_call_add_sync (proxy, uri, appid, &handle, NULL, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  g_print ("handle: %ld\n", handle);

  return 0;
}
