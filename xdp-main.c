#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <gom/gom.h>
#include "xdp-dbus.h"
#include "xdp-main.h"
#include "xdp-document.h"
#include "xdp-permissions.h"
#include "xdp-error.h"
#include "xdp-util.h"

static GomRepository *repository = NULL;
static GDBusNodeInfo *introspection_data = NULL;


static void
got_doc_app_id_cb (GObject *source_object,
                   GAsyncResult *res,
                   gpointer user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (source_object);
  g_autoptr(XdpDocument) doc = user_data;
  g_autoptr(GError) error = NULL;
  char *app_id;

  app_id = xdp_invocation_lookup_app_id_finish (invocation, res, &error);

  if (app_id == NULL)
    g_dbus_method_invocation_return_gerror (invocation, error);
  else
    xdp_document_handle_call (doc, invocation, app_id);

  g_free (app_id);
}

static void
got_document_cb (GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(XdpDocument) doc = NULL;
  g_autoptr(GDBusMethodInvocation) invocation = user_data;

  doc = xdg_document_load_finish (repository, res, &error);

  if (doc == NULL)
    {
      if (g_error_matches (error, GOM_ERROR, GOM_ERROR_REPOSITORY_EMPTY_RESULT))
        g_dbus_method_invocation_return_error_literal (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_UNKNOWN_OBJECT,
                                                       error->message);
      else /* TODO: Use real dbus errors */
        g_dbus_method_invocation_return_gerror (invocation, error);
    }
  else
    xdp_invocation_lookup_app_id (invocation, NULL, got_doc_app_id_cb, g_object_ref (doc));
}

static void
document_method_call (GDBusConnection       *connection,
                      const gchar           *sender,
                      const gchar           *object_path,
                      const gchar           *interface_name,
                      const gchar           *method_name,
                      GVariant              *parameters,
                      GDBusMethodInvocation *invocation,
                      gpointer               user_data)
{
  gint64 id = *(gint64*)user_data;

  g_free (user_data);

  xdp_document_load (repository, id, NULL, got_document_cb, g_object_ref (invocation));
}

const GDBusInterfaceVTable document_vtable =
  {
    document_method_call,
    NULL,
    NULL
  };


static gchar **
subtree_enumerate (GDBusConnection       *connection,
                   const gchar           *sender,
                   const gchar           *object_path,
                   gpointer               user_data)
{
  gchar **nodes;
  GPtrArray *p;

  p = g_ptr_array_new ();
  g_ptr_array_add (p, NULL);
  nodes = (gchar **) g_ptr_array_free (p, FALSE);

  return nodes;
}

static GDBusInterfaceInfo **
subtree_introspect (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *node,
                    gpointer               user_data)
{
  GPtrArray *p;
  char *end;
  gint64 id;

  if (node != NULL)
    {
      id = g_ascii_strtoll (node, &end, 10);
      if (id != 0 && *end == 0)
        {
          p = g_ptr_array_new ();
          g_ptr_array_add (p, g_dbus_interface_info_ref (xdp_dbus_document_interface_info ()));
          g_ptr_array_add (p, NULL);
          return (GDBusInterfaceInfo **) g_ptr_array_free (p, FALSE);
        }
    }

  return NULL;
}

static const GDBusInterfaceVTable *
subtree_dispatch (GDBusConnection             *connection,
                  const gchar                 *sender,
                  const gchar                 *object_path,
                  const gchar                 *interface_name,
                  const gchar                 *node,
                  gpointer                    *out_user_data,
                  gpointer                     user_data)
{
  const GDBusInterfaceVTable *vtable_to_return = NULL;
  char *end;
  gint64 id;
  gpointer user_data_to_return = NULL;

  if (node != NULL &&
      (id = g_ascii_strtoll (node, &end, 10)) != 0 &&
      *end == 0 &&
      g_strcmp0 (interface_name, "org.freedesktop.portal.Document") == 0)
    {
      user_data_to_return = g_memdup (&id, sizeof (gint64));
      vtable_to_return = &document_vtable;
    }

  *out_user_data = user_data_to_return;
  return vtable_to_return;
}

const GDBusSubtreeVTable subtree_vtable =
  {
    subtree_enumerate,
    subtree_introspect,
    subtree_dispatch
  };

static void
got_document_for_uri_cb (GObject *source_object,
                         GAsyncResult *result,
                         gpointer data)
{
  GDBusMethodInvocation *invocation = data;
  g_autoptr(GError) error = NULL;
  XdpDocument *doc;

  doc = xdp_document_for_uri_finish (repository, result, &error);
  if (doc == NULL)
    g_dbus_method_invocation_return_error (invocation,
                                           XDP_ERROR, XDP_ERROR_FAILED,
                                           "Failed to store: %s", error->message);
  else
    g_dbus_method_invocation_return_value (invocation,
                                           g_variant_new ("(x)", xdp_document_get_id (doc)));
}

static void
got_app_id_cb (GObject *source_object,
               GAsyncResult *res,
               gpointer user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (source_object);
  g_autoptr(GError) error = NULL;
  g_autofree char *app_id = NULL;

  app_id = xdp_invocation_lookup_app_id_finish (invocation, res, &error);

  if (app_id == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
    }
  else if (app_id[0] != '\0')
    {
      /* don't allow this from within the sandbox for now */
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_FAILED,
                                             "Not allowed inside sandbox");
    }
  else
    {
      GVariant *parameters;
      const char *uri;

      parameters = g_dbus_method_invocation_get_parameters (invocation);
      g_variant_get (parameters, "(&s)", &uri);

      xdp_document_for_uri (repository, uri, NULL, got_document_for_uri_cb, g_object_ref (invocation));

    }
}

static gboolean
handle_add (XdpDbusDocumentPortal *portal,
            GDBusMethodInvocation *invocation,
            const char            *type)
{
  xdp_invocation_lookup_app_id (invocation, NULL, got_app_id_cb, NULL);

  return TRUE;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  XdpDbusDocumentPortal *helper;
  GError *error = NULL;
  guint registration_id;

  helper = xdp_dbus_document_portal_skeleton_new ();

  g_signal_connect (helper, "handle-add",
                    G_CALLBACK (handle_add), NULL);

  xdp_connection_track_name_owners (connection);

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (helper),
                                         connection,
                                         "/org/freedesktop/portal/document",
                                         &error))
    {
      g_warning ("error: %s\n", error->message);
      g_error_free (error);
    }

  registration_id = g_dbus_connection_register_subtree (connection,
                                                        "/org/freedesktop/portal/document",
                                                        &subtree_vtable,
                                                        G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,
                                                        NULL,  /* user_data */
                                                        NULL,  /* user_data_free_func */
                                                        NULL); /* GError** */


  g_assert (registration_id > 0);
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

  object_types = g_list_prepend (object_types, GINT_TO_POINTER(XDP_TYPE_DOCUMENT));
  object_types = g_list_prepend (object_types, GINT_TO_POINTER(XDP_TYPE_PERMISSIONS));

  if (!gom_repository_automatic_migrate_sync (repository, 1, object_types, &error))
    {
      g_printerr ("Failed to convert migrate database: %s\n", error->message);
      return 1;
    }

  introspection_bytes = g_resources_lookup_data ("/org/freedesktop/portal/DocumentPortal/org.freedesktop.portal.documents.xml", 0, NULL);
  g_assert (introspection_bytes != NULL);

  introspection_data = g_dbus_node_info_new_for_xml (g_bytes_get_data (introspection_bytes, NULL), NULL);

  owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             "org.freedesktop.portal.DocumentPortal",
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
