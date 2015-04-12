#include "config.h"

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>

#include "xdp-dbus.h"

gboolean
xdp_dbus_document_call_prepare_update_with_unix_fd_list_sync (XdpDbusDocument *proxy,
                                                              const gchar     *arg_etag,
                                                              gchar          **arg_flags,
                                                              guint32         *out_id,
                                                              GUnixFDList    **out_fd_list,
                                                              GVariant       **out_fd,
                                                              GCancellable    *cancellable,
                                                              GError         **error)
{
  GVariant *_ret;

  _ret = g_dbus_proxy_call_with_unix_fd_list_sync (G_DBUS_PROXY (proxy),
                                                   "PrepareUpdate",
                                                   g_variant_new ("(s^as)",
                                                                  arg_etag,
                                                                  arg_flags),
                                                   G_DBUS_CALL_FLAGS_NONE,
                                                   -1,
                                                   NULL,
                                                   out_fd_list,
                                                   cancellable,
                                                   error);
  if (_ret == NULL)
    goto _out;
  g_variant_get (_ret, "(u@h)", out_id, out_fd);
  g_variant_unref (_ret);
_out:
  return _ret != NULL;
}

int
main (int    argc,
      char **argv)
{
  XdpDbusDocument *proxy;
  GDBusConnection *bus;
  g_autoptr(GError) error;
  g_autoptr(GVariant) fd_v = NULL;
  GUnixFDList *fd_list = NULL;
  g_autofree char *path = NULL;
  guint32 id;
  gint32 handle;
  int fd;
  gssize n_read, n_written;
  char buffer[8192], *p;
  gboolean res;
  char *flags[] = {NULL};

  setlocale (LC_ALL, "");

  if (argc < 2)
    {
      g_printerr ("Missing arg\n");
      return 1;
    }

  g_set_prgname (argv[0]);

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (bus == NULL)
    {
      g_printerr ("Can't get session bus: %s\n", error->message);
      return 1;
    }

  path = g_build_filename ("/org/freedesktop/portal/document", argv[1], NULL);
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

  fd_list = g_unix_fd_list_new ();
  if (!xdp_dbus_document_call_prepare_update_with_unix_fd_list_sync (proxy, "", flags, &id, &fd_list, &fd_v, NULL, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  handle = g_variant_get_handle (fd_v);

  fd = g_unix_fd_list_get (fd_list, handle, &error);
  if (fd == -1)
    {
      g_printerr ("Can't get fd: %s\n", error->message);
      return 1;
    }

  res = TRUE;
  do
    {
      do {
        n_read = read (STDIN_FILENO, buffer, sizeof (buffer));
      } while (n_read == -1 && errno == EINTR);
      if (n_read == -1)
        {
          res = FALSE;
          break;
        }

      if (n_read == 0)
        break;

      p = buffer;
      while (n_read > 0)
        {
          do {
            n_written = write (fd, p, n_read);
          } while (n_written == -1 && errno == EINTR);
          if (n_written == -1)
            {
              res = FALSE;
              break;
            }

          p += n_written;
          n_read -= n_written;
        }
    }
  while (res);

  g_object_unref (fd_list);


  if (!xdp_dbus_document_call_finish_update_sync (proxy, id, NULL, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  return 0;
}
