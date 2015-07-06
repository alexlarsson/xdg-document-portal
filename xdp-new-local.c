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
do_new_local (int argc, char **argv)
{
  GDBusConnection *bus;
  g_autoptr(GFile) file = NULL;
  g_autofree char *uri = NULL;
  char *appid = NULL;
  char *title;
  char *handle;
  GUnixFDList *fd_list = NULL;
  g_autoptr(GError) error = NULL;
  int fd, fd_id;
  GVariant *ret;
  g_autofree char *path = NULL;
  char *permissions[4] = { "read", "write", "grant-permissions", NULL };

  setlocale (LC_ALL, "");

  g_print ("argc: %d\n", argc);
  if (argc < 2)
    {
      g_printerr ("Usage: xdp new-local FILE TITLE [APPID]\n");
      return 1;
    }

  file = g_file_new_for_commandline_arg (argv[0]);
  title = argv[1];
  if (argc > 2)
    appid = argv[2];

  path = g_file_get_path (file);
  fd = open (path, O_RDONLY|O_DIRECTORY);
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
                                                       "NewLocal",
                                                       g_variant_new ("(hs)", fd_id, title),
                                                       G_VARIANT_TYPE ("(s)"),
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

  g_variant_get (ret, "(s)", &handle);

  g_print ("document handle: %s\n", handle);

  if (appid != NULL)
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
