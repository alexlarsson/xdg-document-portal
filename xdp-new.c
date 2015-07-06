#include "config.h"

#include <locale.h>
#include <gio/gio.h>

#include "xdp-dbus.h"

int
do_new (int argc, char **argv)
{
  GDBusConnection *bus;
  g_autoptr(GFile) file = NULL;
  g_autofree char *uri = NULL;
  char *appid = NULL;
  char *title;
  char *handle;
  g_autoptr(GError) error = NULL;
  GVariant *ret;
  g_autofree char *path = NULL;
  char *permissions[4] = { "read", "write", "grant-permissions", NULL };

  setlocale (LC_ALL, "");

  if (argc < 2)
    {
      g_printerr ("Usage: xdp new URI TITLE [APPID]\n");
      return 1;
    }

  file = g_file_new_for_commandline_arg (argv[0]);
  uri = g_file_get_uri (file);
  title = argv[1];
  if (argc > 2)
  appid = argv[2];

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (bus == NULL)
    {
      g_printerr ("Can't get session bus: %s\n", error->message);
      return 1;
    }

  ret = g_dbus_connection_call_sync (bus,
                                     "org.freedesktop.portal.DocumentPortal",
                                     "/org/freedesktop/portal/document",
                                     "org.freedesktop.portal.DocumentPortal",
                                     "New",
                                     g_variant_new ("(ss)", uri, title),
                                     G_VARIANT_TYPE ("(s)"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     30000,
                                     NULL,
                                     &error);
  if (ret == NULL)
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  g_variant_get (ret, "(s)", &handle);

  g_print ("document handle: %s\n", handle);

  if (appid)
    {
      path = g_strdup_printf ("/org/freedesktop/portal/document/%s", handle);

      ret = g_dbus_connection_call_sync (bus,
                                         "org.freedesktop.portal.DocumentPortal",
                                         path,
                                         "org.freedesktop.portal.Document",
                                         "GrantPermissions",
                                         g_variant_new ("(s^as)", appid, permissions),
                                         G_VARIANT_TYPE ("()"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         30000,
                                         NULL,
                                         &error);
      if (ret == NULL)
        {
          g_printerr ("%s\n", error->message);
          return 1;
        }
    }

  g_variant_unref (ret);

  return 0;
}
