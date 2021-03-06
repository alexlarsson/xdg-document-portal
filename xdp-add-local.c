#include "config.h"

#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "xdp-dbus.h"

int
do_add_local (int argc, char *argv[])
{
  GDBusConnection *bus;
  g_autoptr(GFile) file = NULL;
  g_autofree char *path = NULL;
  char *appid = NULL;
  guint32 doc_id;
  g_autoptr(GError) error = NULL;
  GUnixFDList *fd_list = NULL;
  GVariant *ret;
  int fd, fd_id;
  char *permissions[4] = { "read", "write", "grant-permissions", NULL };

  setlocale (LC_ALL, "");

  if (argc < 1)
    {
      g_printerr ("Usage: xdp add FILE [APPID]\n");
      return 1;
    }

  file = g_file_new_for_commandline_arg (argv[0]);
  path = g_file_get_path (file);
  if (argc > 1)
    appid = argv[1];

  fd = open (path, O_RDONLY);

  if (fd == -1)
    {
      perror ("Error opening path");
      return 1;
    }

  fd_list = g_unix_fd_list_new ();
  fd_id = g_unix_fd_list_append (fd_list, fd, &error);
  close (fd);

  if (fd_id == -1)
    {
      g_printerr ("Error creating handle: %s\n", error->message);
      return 1;
    }

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (bus == NULL)
    {
      g_printerr ("Can't get session bus: %s\n", error->message);
      return 1;
    }

  ret = g_dbus_connection_call_with_unix_fd_list_sync (bus,
                                                       "org.freedesktop.portal.DocumentPortal",
                                                       "/org/freedesktop/portal/document",
                                                       "org.freedesktop.portal.DocumentPortal",
                                                       "AddLocal",
                                                       g_variant_new ("(h)", fd_id),
                                                       G_VARIANT_TYPE ("(u)"),
                                                       G_DBUS_CALL_FLAGS_NONE,
                                                       30000,
                                                       fd_list, NULL,
                                                       NULL,
                                                       &error);
  if (ret == NULL)
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  g_object_unref (fd_list);

  g_variant_get (ret, "(u)", &doc_id);

  g_print ("document id: %x\n", doc_id);

  if (appid != NULL)
    {
      ret = g_dbus_connection_call_sync (bus,
                                         "org.freedesktop.portal.DocumentPortal",
                                         "/org/freedesktop/portal/document",
                                         "org.freedesktop.portal.DocumentPortal",
                                         "GrantPermissions",
                                         g_variant_new ("(us^as)", doc_id, appid, permissions),
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
