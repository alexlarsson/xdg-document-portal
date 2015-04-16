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
  char *appid;
  char *title;
  gint64 handle;
  g_autoptr(GError) error = NULL;
  GVariant *ret;
  g_autofree char *path = NULL;
  char *permissions[4] = { "read", "write", "grant-permissions", NULL };
  gboolean transient = FALSE;

  setlocale (LC_ALL, "");

  if (argc > 0 && g_strcmp0 (argv[0], "--transient") == 0)
    {
      transient = TRUE;
      argv += 1;
      argc -= 1;
    }

  if (argc != 3)
    {
      g_printerr ("Usage: xdp new [--transient] URI TITLE APPID\n");
      return 1;
    }

  file = g_file_new_for_commandline_arg (argv[0]);
  uri = g_file_get_uri (file);
  title = argv[1];
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
                                     g_variant_new ("(ssb)", uri, title, transient),
                                     G_VARIANT_TYPE ("(x)"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     30000,
                                     NULL,
                                     &error);
  if (ret == NULL)
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  g_variant_get (ret, "(x)", &handle);

  g_print ("document handle: %ld\n", handle);

  g_variant_unref (ret);

  path = g_strdup_printf ("/org/freedesktop/portal/document/%ld", handle);

  ret = g_dbus_connection_call_sync (bus,
                                     "org.freedesktop.portal.DocumentPortal",
                                     path,
                                     "org.freedesktop.portal.Document",
                                     "GrantPermissions",
                                     g_variant_new ("(s^asb)", appid, permissions, transient),
                                     G_VARIANT_TYPE ("(x)"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     30000,
                                     NULL,
                                     &error);
  if (ret == NULL)
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  g_variant_get (ret, "(x)", &handle);

  g_print ("permission handle: %ld\n", handle);

  return 0;
}
