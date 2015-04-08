#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "xdp-document.h"
#include "xdp-error.h"
#include "xdp-permissions.h"

struct _XdpDocument
{
  GomResource parent;

  gint64 id;
  char *uri;

  GList *permissions;

  GList *updates;
};

typedef struct
{
  int fd;
  char *owner;
} XdpDocumentUpdate;


G_DEFINE_TYPE(XdpDocument, xdp_document, GOM_TYPE_RESOURCE)

enum {
  PROP_0,
  PROP_ID,
  PROP_URI,
  LAST_PROP
};

static GParamSpec *gParamSpecs [LAST_PROP];

static GHashTable *documents;
static GHashTable *loads;
static GHashTable *adds;

typedef struct {
  gint64 id;
  XdpDocument *doc;
  GList *pending;
} DocumentLoad;

typedef struct
{
  gchar *uri;
  GList *pending;
} DocumentAdd;

static void
xdp_document_finalize (GObject *object)
{
  XdpDocument *doc = (XdpDocument *)object;

  g_free (doc->uri);

  G_OBJECT_CLASS (xdp_document_parent_class)->finalize (object);
}

static void
xdp_document_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  XdpDocument *doc = (XdpDocument *)object;

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_int64 (value, doc->id);
      break;

    case PROP_URI:
      g_value_set_string (value, doc->uri);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
xdp_document_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  XdpDocument *doc = (XdpDocument *)object;

  switch (prop_id)
    {
    case PROP_ID:
      doc->id = g_value_get_int64 (value);
      break;

    case PROP_URI:
      g_free (doc->uri);
      doc->uri = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
xdp_document_class_init (XdpDocumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GomResourceClass *resource_class;

  object_class->finalize = xdp_document_finalize;
  object_class->get_property = xdp_document_get_property;
  object_class->set_property = xdp_document_set_property;

  resource_class = GOM_RESOURCE_CLASS(klass);
  gom_resource_class_set_table(resource_class, "documents");

  gParamSpecs [PROP_ID] =
    g_param_spec_int64 ("id", _("Id"), _("Unique Id"),
                        G_MININT64,
                        G_MAXINT64,
                        0,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_ID,
                                   gParamSpecs [PROP_ID]);
  gom_resource_class_set_primary_key(resource_class, "id");

  gParamSpecs[PROP_URI] = g_param_spec_string("uri", "Uri",
                                              "Location of data.",
                                              NULL, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_URI,
                                   gParamSpecs[PROP_URI]);
  gom_resource_class_set_notnull(resource_class, "uri");

}

static void
xdp_document_init (XdpDocument *self)
{
}

XdpDocument *
xdp_document_new (GomRepository *repo,
                  const char *uri)
{
  return g_object_new (XDP_TYPE_DOCUMENT,
                       "repository", repo,
                       "uri", uri,
                       NULL);
}

gint64
xdp_document_get_id (XdpDocument *doc)
{
  return doc->id;
}

XdpPermissionFlags
xdp_document_get_permissions (XdpDocument *doc,
                              const char *app_id)
{
  XdpPermissionFlags flags = 0;
  GList *l;

  if (*app_id == 0)
    return XDP_PERMISSION_FLAGS_ALL;

  for (l = doc->permissions; l != NULL; l = l->next)
    {
      XdpPermissions *permissions = l->data;
      if (strcmp (xdp_permissions_get_app_id (permissions), app_id) == 0)
        flags |= xdp_permissions_get_permissions (permissions);
    }

  return flags;
}

typedef struct
{
  GTask *task;
  XdpDocument *doc;
} PermissionData;

static void
permissions_saved (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
  GomResource *resource = GOM_RESOURCE (source_object);
  XdpPermissions *permissions = XDP_PERMISSIONS (source_object);
  g_autofree PermissionData *data = user_data;
  g_autoptr (GTask) task = data->task;
  g_autoptr (XdpDocument) doc = data->doc;
  g_autoptr (GError) error = NULL;

  if (!gom_resource_save_finish (resource, result, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  doc->permissions = g_list_prepend (doc->permissions, permissions);
  g_task_return_pointer (task, g_object_ref (permissions), g_object_unref);
}

void
xdp_document_grant_permissions (XdpDocument        *doc,
                                const char         *app_id,
                                XdpPermissionFlags  perms,
                                GCancellable       *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
  XdpPermissions *permissions = NULL;
  GomRepository *repository;
  g_autoptr (GTask) task = NULL;
  PermissionData *data;

  task = g_task_new (doc, cancellable, callback, user_data);

  g_object_get (doc, "repository", &repository, NULL);

  permissions = xdp_permissions_new (repository, doc, app_id, perms, FALSE);

  data = g_new (PermissionData, 1);
  data->task = g_object_ref (task);
  data->doc = g_object_ref (doc);

  gom_resource_save_async (GOM_RESOURCE (permissions), permissions_saved, data);
}

gint64
xdp_document_grant_permissions_finish (XdpDocument   *doc,
                                       GAsyncResult  *result,
                                       GError       **error)
{
  XdpPermissions *permissions;

  permissions = g_task_propagate_pointer (G_TASK (result), error);

  if (permissions)
    return xdp_permissions_get_id (permissions);

  return 0;
}

gboolean
xdp_document_has_permissions (XdpDocument *doc,
                              const char *app_id,
                              XdpPermissionFlags perms)
{
  XdpPermissionFlags flags;

  flags = xdp_document_get_permissions (doc, app_id);

  return (flags & perms) == perms;
}

static void
xdp_document_handle_read (XdpDocument *doc,
                          GDBusMethodInvocation *invocation,
                          const char *app_id,
                          GVariant *parameters)
{
  const char *window;
  g_autoptr(GFile) file = NULL;
  g_autofree char *path = NULL;
  GUnixFDList *fd_list = NULL;
  g_autoptr(GError) error = NULL;
  int fd, fd_id;
  GVariant *retval;

  g_variant_get (parameters, "(&s)", &window);

  if (!xdp_document_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_READ))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "No permissions to open file");
      return;
    }

  file = g_file_new_for_uri (doc->uri);
  path = g_file_get_path (file);

  fd = open (path, O_CLOEXEC | O_RDONLY);
  if (fd == -1)
    {
      int errsv = errno;
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "Unable to open file: %s", strerror(errsv));
      return;
    }

  fd_list = g_unix_fd_list_new ();
  fd_id = g_unix_fd_list_append (fd_list, fd, &error);
  close (fd);
  if (fd_id == -1)
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "Unable to append fd: %s", error->message);
      goto out;
    }

  retval = g_variant_new ("(h)", fd_id);
  g_dbus_method_invocation_return_value_with_unix_fd_list (invocation, retval, fd_list);

 out:
  if (fd_list)
    g_object_unref (fd_list);
}

static void
document_update_free (XdpDocumentUpdate *update)
{
  close (update->fd);
  g_free (update->owner);
  g_free (update);
}

static void
xdp_document_handle_prepare_update (XdpDocument *doc,
                                    GDBusMethodInvocation *invocation,
                                    const char *app_id,
                                    GVariant *parameters)
{
  const char *window, *etag, **flags;
  g_autoptr(GFile) file = NULL;
  g_autofree char *path = NULL;
  g_autofree char *dir = NULL;
  g_autofree char *basename = NULL;
  g_autofree char *template = NULL;
  GUnixFDList *fd_list = NULL;
  g_autoptr(GError) error = NULL;
  int fd = -1, ro_fd = -1, fd_id;
  GVariant *retval;
  XdpDocumentUpdate *update;

  g_variant_get (parameters, "(&s&s^a&s)", &window, &etag, &flags);

  if (!xdp_document_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_WRITE))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "No permissions to open file");
      return;
    }

  file = g_file_new_for_uri (doc->uri);
  path = g_file_get_path (file);
  basename = g_file_get_basename (file);
  dir = g_path_get_dirname (path);
  template = g_strconcat (dir, "/.", basename, ".XXXXXX", NULL);

  fd = g_mkstemp_full (template, O_RDWR, 0600);
  if (fd == -1)
    {
      int errsv = errno;
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "Unable to open temp storage: %s", strerror(errsv));
      goto out;
    }

  ro_fd = open (template, O_RDONLY);
  if (ro_fd == -1)
    {
      int errsv = errno;
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "Unable to reopen temp storage: %s", strerror(errsv));
      goto out;
    }

  if (unlink (template))
    {
      int errsv = errno;
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "Unable to unlink temp storage: %s", strerror(errsv));
      goto out;
    }

  fd_list = g_unix_fd_list_new ();
  fd_id = g_unix_fd_list_append (fd_list, fd, &error);
  if (fd_id == -1)
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "Unable to append fd: %s", error->message);
      goto out;
    }

  update = g_new0 (XdpDocumentUpdate, 1);
  update->fd = ro_fd;
  ro_fd = -1;
  update->owner = g_strdup (g_dbus_method_invocation_get_sender (invocation));

  doc->updates = g_list_append (doc->updates, update);

  retval = g_variant_new ("(uh)", update->fd, fd_id);
  g_dbus_method_invocation_return_value_with_unix_fd_list (invocation, retval, fd_list);

 out:
  if (ro_fd != -1)
    close (ro_fd);
  if (fd != -1)
    close (fd);
  if (fd_list)
    g_object_unref (fd_list);
}

static void
xdp_document_handle_finish_update (XdpDocument *doc,
                                   GDBusMethodInvocation *invocation,
                                   const char *app_id,
                                   GVariant *parameters)
{
  const char *window;
  guint32 id;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) dest = NULL;
  g_autoptr(GFileOutputStream) output = NULL;
  XdpDocumentUpdate *update = NULL;
  GVariant *retval;
  gssize n_read;
  char buffer[8192];
  GList *l;

  g_variant_get (parameters, "(&su)", &window, &id);

  if (!xdp_document_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_WRITE))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "No permissions to open file");
      return;
    }

  for (l = doc->updates; l != NULL; l = l->next)
    {
      XdpDocumentUpdate *l_update = l->data;

      if (l_update->fd == id)
        {
          update = l_update;
          break;
        }
    }

  if (update == NULL ||
      strcmp (update->owner, g_dbus_method_invocation_get_sender (invocation)) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "No such update to finish");
      return;
    }

  doc->updates = g_list_remove (doc->updates, update);

  dest = g_file_new_for_uri (doc->uri);
  output = g_file_replace (dest, NULL, FALSE, 0, NULL, &error);
  if (output == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "%s", error->message);
      goto out;
    }

  do
    {
      do
        n_read = read (update->fd, buffer, sizeof (buffer));
      while (n_read == -1 && errno == EINTR);
      if (n_read == -1)
        {
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                                 "Error reading file");
          goto out;
        }

      if (n_read == 0)
        break;

      if (!g_output_stream_write_all (G_OUTPUT_STREAM (output), buffer, n_read, NULL, NULL, &error))
        {
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                                 "Error writing file: %s", error->message);
          goto out;
        }
    }
  while (1);

  retval = g_variant_new ("()");
  g_dbus_method_invocation_return_value (invocation, retval);

 out:
  document_update_free (update);
}

static void
permissions_granted (GObject *source_object,
                     GAsyncResult *result,
                     gpointer data)
{
  XdpDocument *doc = XDP_DOCUMENT (source_object);
  g_autoptr(GDBusMethodInvocation) invocation = data;
  gint64 cookie;
  g_autoptr(GError) error = NULL;

  cookie = xdp_document_grant_permissions_finish (doc, result, &error);
  if (cookie == 0)
    g_dbus_method_invocation_return_gerror (invocation, error);
  else
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(x)", cookie));
}

static void
xdp_document_handle_grant_permissions (XdpDocument *doc,
                                       GDBusMethodInvocation *invocation,
                                       const char *app_id,
                                       GVariant *parameters)
{
  const char *target_app_id;
  char **permissions;
  XdpPermissionFlags perms;
  gint i;

  g_variant_get (parameters, "(&s^a&s)", &target_app_id, &permissions);

  if (!xdp_document_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_GRANT_PERMISSIONS))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "No permissions to grant permissions");
      return;
    }

  perms = 0;
  for (i = 0; permissions[i]; i++)
    {
      if (strcmp (permissions[i], "read") == 0)
        perms |= XDP_PERMISSION_FLAGS_READ;
      else if (strcmp (permissions[i], "write") == 0)
        perms |= XDP_PERMISSION_FLAGS_WRITE;
      else if (strcmp (permissions[i], "grant-permissions") == 0)
        perms |= XDP_PERMISSION_FLAGS_GRANT_PERMISSIONS;
      else
        {
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                                 "No such permission: %s", permissions[i]);
          return;
        }
    }

  xdp_document_grant_permissions (doc, target_app_id, perms, NULL, permissions_granted, g_object_ref (invocation));
}

struct {
  const char *name;
  const char *args;
  void (*callback) (XdpDocument *doc,
                    GDBusMethodInvocation *invocation,
                    const char *app_id,
                    GVariant *parameters);
} doc_methods[] = {
  { "Read", "(s)", xdp_document_handle_read},
  { "PrepareUpdate", "(ssas)", xdp_document_handle_prepare_update},
  { "FinishUpdate", "(su)", xdp_document_handle_finish_update},
  { "GrantPermissions", "(sas)", xdp_document_handle_grant_permissions}
};

void
xdp_document_handle_call (XdpDocument *doc,
                          GDBusMethodInvocation *invocation,
                          const char *app_id)
{
  const char *method_name = g_dbus_method_invocation_get_method_name (invocation);
  const gchar *interface_name = g_dbus_method_invocation_get_interface_name (invocation);
  GVariant *parameters = g_dbus_method_invocation_get_parameters (invocation);
  int i;

  if (strcmp (interface_name, "org.freedesktop.portal.Document") == 0)
    {
      for (i = 0; i < G_N_ELEMENTS (doc_methods); i++)
        {
          if (strcmp (method_name, doc_methods[i].name) == 0)
            {
              if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE (doc_methods[i].args)))
                {
                  g_dbus_method_invocation_return_error (invocation,
                                                         G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                                         "Invalid arguments for %s.%s, expecting %s", interface_name, method_name, doc_methods[i].args);
                  break;
                }
              else
                {
                  (doc_methods[i].callback) (doc, invocation, app_id, parameters);
                }
              break;
            }
        }
      if (i == G_N_ELEMENTS (doc_methods))
        g_dbus_method_invocation_return_error (invocation,
                                               G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                               "Method %s is not implemented on interface %s", method_name, interface_name);
    }
  else
    g_dbus_method_invocation_return_error (invocation,
                                           G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                           "Interface %s not implemented", interface_name);
}

static void
document_load_free (DocumentLoad *load)
{
  g_list_free_full (load->pending, g_object_unref);
  g_clear_object (&load->doc);
  g_free (load);
}

static void
document_load_abort (DocumentLoad *load, GError *error)
{
  GList *l;

  for (l = load->pending; l != NULL; l = l->next)
    {
      GTask *task = l->data;
      if (error)
        g_task_return_new_error (task, error->domain, error->code, error->message);
      else
        g_task_return_new_error (task, XDP_ERROR, XDP_ERROR_FAILED, "Loading document failed");
    }
  g_hash_table_remove (loads, &load->id);
}

static void
document_load_complete (DocumentLoad *load)
{
  GList *l;

  for (l = load->pending; l != NULL; l = l->next)
    {
      GTask *task = l->data;
      g_task_return_pointer (task, g_object_ref (load->doc), g_object_unref);
    }
  g_hash_table_remove (loads, &load->id);
}

static void
document_add_free (DocumentAdd *add)
{
  g_list_free_full (add->pending, g_object_unref);
  g_free (add->uri);
  g_free (add);
}

static void
document_add_abort (DocumentAdd *add, GError *error)
{
  GList *l;

  for (l = add->pending; l != NULL; l = l->next)
    {
      GTask *task = l->data;
      if (error)
        g_task_return_new_error (task, error->domain, error->code, error->message);
      else
        g_task_return_new_error (task, XDP_ERROR, XDP_ERROR_FAILED, "Adding document failed");
    }
  g_hash_table_remove (adds, add->uri);
}

static void
document_add_complete (DocumentAdd *add, XdpDocument *doc)
{
  GList *l;

  for (l = add->pending; l != NULL; l = l->next)
    {
      GTask *task = l->data;

      g_task_return_pointer (task, g_object_ref (doc), g_object_unref);
    }

  g_hash_table_remove (adds, add->uri);
}

static void
ensure_documents (void)
{
  if (documents == NULL)
    documents = g_hash_table_new_full (g_int64_hash, g_int64_equal,
                                       NULL, g_object_unref);

  if (loads == NULL)
    loads = g_hash_table_new_full (g_int64_hash, g_int64_equal,
                                   NULL, (GDestroyNotify)document_load_free);

  if (adds == NULL)
    adds = g_hash_table_new_full (g_str_hash, g_str_equal,
                                   NULL, (GDestroyNotify)document_add_free);
}

XdpDocument *
xdg_document_load_finish (GomRepository *repository,
                          GAsyncResult    *result,
                          GError         **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
fetch_permissions_cb (GObject *source_object,
                      GAsyncResult *res,
                      gpointer user_data)
{
  GomResourceGroup *group = GOM_RESOURCE_GROUP (source_object);
  g_autoptr (GError) error = NULL;
  DocumentLoad *load = user_data;
  int i;

  if (!gom_resource_group_fetch_finish (group, res, &error))
    {
      document_load_abort (load, error);
      return;
    }

  for (i = 0; i < gom_resource_group_get_count (group); i++)
    {
      GomResource *res = gom_resource_group_get_index (group, i);

      load->doc->permissions = g_list_prepend (load->doc->permissions, g_object_ref (res));
    }

  g_hash_table_insert (documents, &load->doc->id, g_object_ref (load->doc));
  document_load_complete (load);
}

static void
find_permissions_cb (GObject *source_object,
                     GAsyncResult *res,
                     gpointer user_data)
{
  GomRepository *repository = GOM_REPOSITORY (source_object);
  DocumentLoad *load = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr(GomResourceGroup) group = NULL;

  group = gom_repository_find_finish (repository, res, &error);

  if (group == NULL)
    {
      document_load_abort (load, error);
      return;
    }

  gom_resource_group_fetch_async (group, 0, gom_resource_group_get_count (group),
                                  fetch_permissions_cb, load);
}

static void
find_one_doc_cb (GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
  GomRepository *repository = GOM_REPOSITORY (source_object);
  DocumentLoad *load = user_data;
  g_autoptr (XdpDocument) doc = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr(GomFilter) filter = NULL;
  GValue value = { 0, };

  doc = (XdpDocument *)gom_repository_find_one_finish (repository, res, &error);

  if (doc == NULL)
    {
      document_load_abort (load, error);
      return;
   }

  load->doc = g_object_ref (doc);

  g_value_init (&value, G_TYPE_INT64);
  g_value_set_int64 (&value, doc->id);
  filter = gom_filter_new_eq (XDP_TYPE_PERMISSIONS, "document", &value);

  gom_repository_find_async (repository, XDP_TYPE_PERMISSIONS,
                             filter,
                             find_permissions_cb, load);
}

void
xdp_document_load (GomRepository      *repository,
                   gint64              id,
                   GCancellable       *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer            user_data)
{
  XdpDocument *doc;
  DocumentLoad *load;
  g_autoptr (GTask) task = NULL;

  ensure_documents ();

  task = g_task_new (repository,
                     cancellable,
                     callback,
                     user_data);

  doc = g_hash_table_lookup (documents, &id);

  if (doc)
    g_task_return_pointer (task, g_object_ref (doc), g_object_unref);
  else
    {
      load = g_hash_table_lookup (loads, &id);
      if (load == NULL)
        {
          g_autoptr(GomFilter) filter = NULL;
          GValue value = { 0, };

          g_value_init (&value, G_TYPE_INT64);
          g_value_set_int64 (&value, id);
          filter = gom_filter_new_eq (XDP_TYPE_DOCUMENT, "id", &value);

          load = g_new0 (DocumentLoad, 1);
          load->id = id;
          load->pending = g_list_append (load->pending, g_object_ref (task));

          g_hash_table_insert (loads, &load->id, load);
          gom_repository_find_one_async (repository, XDP_TYPE_DOCUMENT,
                                         filter,
                                         find_one_doc_cb, load);
        }
      else
        load->pending = g_list_append (load->pending, g_object_ref (task));
    }
}

static void
document_saved (GObject *source_object,
                GAsyncResult *result,
                gpointer data)
{
  GomResource *resource = GOM_RESOURCE (source_object);
  XdpDocument *doc = XDP_DOCUMENT (source_object);
  DocumentAdd *add = data;
  g_autoptr(GError) error = NULL;

  if (!gom_resource_save_finish (resource, result, &error))
    {
      document_add_abort (add, error);
      return;
    }

  g_hash_table_insert (documents, &doc->id, doc);
  document_add_complete (add, doc);
}

static void
document_loaded (GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
  GomRepository *repository = GOM_REPOSITORY (source_object);
  DocumentAdd *add = user_data;
  g_autoptr(XdpDocument) doc = NULL;
  g_autoptr(GError) error = NULL;

  doc = xdg_document_load_finish (repository, res, &error);
  if (doc == NULL)
    {
      document_add_abort (add, error);
      return;
    }

  document_add_complete (add, doc);
}

static void
find_one_for_uri_cb (GObject *source_object,
                     GAsyncResult *res,
                     gpointer user_data)
{
  GomRepository *repository = GOM_REPOSITORY (source_object);
  DocumentAdd *add = user_data;
  g_autoptr (XdpDocument) doc = NULL;
  g_autoptr (GError) error = NULL;

  doc = (XdpDocument *)gom_repository_find_one_finish (repository, res, &error);

  if (doc == NULL)
    {
      if (g_error_matches (error, GOM_ERROR, GOM_ERROR_REPOSITORY_EMPTY_RESULT))
        {
          g_clear_error (&error);
          doc = xdp_document_new (repository, add->uri);
          gom_resource_save_async (GOM_RESOURCE (g_object_ref (doc)), document_saved, add);
        }
      else
        document_add_abort (add, error);
    }
  else
    xdp_document_load (repository, doc->id, NULL, document_loaded, add);
}

void
xdp_document_for_uri (GomRepository      *repository,
                      const char         *uri,
                      GCancellable       *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer            user_data)
{
  XdpDocument *doc;
  g_autoptr (GTask) task = NULL;
  GHashTableIter iter;
  DocumentAdd *add;

  ensure_documents ();

  task = g_task_new (repository, cancellable, callback, user_data);

  g_hash_table_iter_init (&iter, documents);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&doc))
    {
      if (strcmp (uri, doc->uri) == 0)
        {
          g_task_return_pointer (task, g_object_ref (doc), g_object_unref);
          return;
        }
    }

  add = g_hash_table_lookup (adds, uri);
  if (add == NULL)
    {
      g_autoptr(GomFilter) filter = NULL;
      GValue value = { 0, };

      add = g_new0 (DocumentAdd, 1);
      add->uri = g_strdup (uri);
      add->pending = g_list_append (add->pending, g_object_ref (task));

      g_hash_table_insert (adds, add->uri, add);

      g_value_init (&value, G_TYPE_STRING);
      g_value_set_string (&value, uri);
      filter = gom_filter_new_eq (XDP_TYPE_DOCUMENT, "uri", &value);

      gom_repository_find_one_async (repository, XDP_TYPE_DOCUMENT,
                                     filter, find_one_for_uri_cb, add);
    }
  else
    add->pending = g_list_append (add->pending, g_object_ref (task));
}

XdpDocument *
xdp_document_for_uri_finish (GomRepository *repository,
                             GAsyncResult    *result,
                             GError         **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}
