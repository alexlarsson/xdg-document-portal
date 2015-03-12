#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <gom/gom.h>
#include "xdg-document-portal-dbus.h"
#include "xdp-document.h"

static GDBusNodeInfo *introspection_data = NULL;

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  XdpDbusDocumentPortal *helper;
  GError *error = NULL;

  helper = xdp_dbus_document_portal_skeleton_new ();

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
  g_autoptr(GList) object_types = NULL;
  g_autoptr(GomAdapter) adapter = NULL;
  g_autoptr(GomRepository) repository = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *data_path = NULL;
  g_autofree char *uri = NULL;
  g_autoptr(GFile) data_dir = NULL;
  g_autoptr(GFile) db_file = NULL;

  setlocale (LC_ALL, "");

  /* Avoid even loading gvfs to avoid accidental confusion */
  g_setenv ("GIO_USE_VFS", "local", TRUE);
  
  g_set_prgname (argv[0]);

  data_path = g_build_filename (g_get_user_data_dir(), "xdg-document-portal", NULL);
  if (g_mkdir_with_parents  (data_path, 0700))
    {
      g_printerr ("Unable to create dir %s\n", data_path);
      return 1;
    }

  data_dir = g_file_new_for_path (data_path);
  db_file = g_file_get_child (data_dir, "main.db");
  uri = g_file_get_uri (db_file);

  adapter = gom_adapter_new ();
  if (!gom_adapter_open_sync (adapter, uri, &error))
    {
      g_printerr ("Failed to open database: %s\n", error->message);
      return 1;
    }

  repository = gom_repository_new (adapter);

  object_types = g_list_prepend (NULL, GINT_TO_POINTER(XDP_TYPE_DOCUMENT));
  if (!gom_repository_automatic_migrate_sync (repository, 1, object_types, &error))
    {
      g_printerr ("Failed to convert migrate database: %s\n", error->message);
      return 1;
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

  g_bus_unown_name (owner_id);

  g_dbus_node_info_unref (introspection_data);


  return 0;
}
