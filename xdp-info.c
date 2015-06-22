#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>

#include "xdp-dbus.h"

static void
print_asv (GVariant *asv)
{
  GVariantIter iter;
  gchar *key;
  GVariant *value;

  g_variant_iter_init (&iter, asv);
  while (g_variant_iter_loop (&iter, "{sv}", &key, &value))
    {
      gchar *s = g_variant_print (value, FALSE);
      g_print ("%s: %s\n", key, s);
      g_free (s);
    }
}

int
do_info (int argc, const char *argv[])
{
  XdpDbusDocument *proxy;
  GDBusConnection *bus;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  g_autoptr (GVariant) ret = NULL;
  g_autoptr (GVariant) asv = NULL;

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (bus == NULL)
    {
      g_printerr ("Can't get session bus: %s\n", error->message);
      return 1;
    }

  if (argc < 1)
    {
      g_printerr ("Usage: xdp info ID\n");
      return 1;
    }

  path = g_build_filename ("/org/freedesktop/portal/document", argv[0], NULL);
  proxy = xdp_dbus_document_proxy_new_sync (bus,
                                            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                            "org.freedesktop.portal.DocumentPortal",
                                            path,
                                            NULL, &error);
  if (proxy == NULL)
    {
      g_printerr ("Can't create document proxy: %s\n", error->message);
      return 1;
    }

  if (!xdp_dbus_document_call_get_info_sync (proxy, &ret, NULL, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  print_asv (ret);

  return 0;
}
