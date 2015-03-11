#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include "xdg-document-portal-dbus.h"
#include <sqlite3.h>

static GDBusNodeInfo *introspection_data = NULL;
static sqlite3 *main_db;

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  XdpDocumentPortal *helper;
  GError *error = NULL;

  helper = xdp_document_portal_skeleton_new ();

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (helper),
                                         connection,
                                         "/org/freedesktop/portal/DocumentPortal",
                                         &error))
    {
      g_warning ("error: %s\n", error->message);
      g_error_free (error);
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  exit (1);
}

int
main (int    argc,
      char **argv)
{
  guint owner_id;
  GMainLoop *loop;
  GBytes *introspection_bytes;
  g_autofree char *db_path_dir = NULL;
  g_autofree char *db_path = NULL;
  int res;

  setlocale (LC_ALL, "");

  g_set_prgname (argv[0]);

  db_path_dir = g_build_filename (g_get_user_data_dir(), "xdg-document-portal", NULL);
  if (g_mkdir_with_parents  (db_path_dir, 0700))
    {
      g_error ("Unable to create dir %s\n", db_path_dir);
      exit (1);
    }

  db_path = g_build_filename (db_path_dir, "main.db", NULL);

  res = sqlite3_open (db_path, &main_db);
  if (res)
    {
      g_error ("Can't open database: %s\n", sqlite3_errmsg (main_db));
      exit(1);
    }

  introspection_bytes = g_resources_lookup_data ("/org/freedesktop/portal/DocumentPortal/org.freedesktop.portal.documents.xml", 0, NULL);
  g_assert (introspection_bytes != NULL);

  introspection_data = g_dbus_node_info_new_for_xml (g_bytes_get_data (introspection_bytes, NULL), NULL);

  owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             "org.freedesktop.portal.DocumentsPortal",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  sqlite3_close (main_db);

  g_bus_unown_name (owner_id);

  g_dbus_node_info_unref (introspection_data);


  return 0;
}
