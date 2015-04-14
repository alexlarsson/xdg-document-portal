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
  char *title;

  GList *permissions;
  GList *updates;
  GList *removals;
};

typedef struct
{
  int fd;
  char *owner;
} XdpDocumentUpdate;


typedef struct
{
  XdpDocument *doc;
  gint64 handle;
  GList *tasks;
} XdpPermissionsRemoval;

G_DEFINE_TYPE(XdpDocument, xdp_document, GOM_TYPE_RESOURCE)

enum {
  PROP_0,
  PROP_ID,
  PROP_URI,
  PROP_TITLE,
  LAST_PROP
};

static GParamSpec *gParamSpecs [LAST_PROP];

static GHashTable *documents;
static GHashTable *loads;
static GHashTable *adds;
static GHashTable *removals;

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

typedef struct
{
  gint64 id;
  XdpDocument *doc;
  GList *pending;
} DocumentRemoval;

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

    case PROP_TITLE:
      g_value_set_string (value, doc->title);
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

    case PROP_TITLE:
      g_free (doc->title);
      doc->title = g_value_dup_string (value);
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

  gParamSpecs[PROP_TITLE] = g_param_spec_string("title", "Title",
                                                "Title for new file.",
                                                NULL, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_TITLE,
                                   gParamSpecs[PROP_TITLE]);
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

XdpDocument *
xdp_document_new_with_title (GomRepository *repo,
                             const char *base_uri,
                             const char *title)
{
  return g_object_new (XDP_TYPE_DOCUMENT,
                       "repository", repo,
                       "uri", base_uri,
                       "title", title,
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
} SavePermissionsData;

static void
permissions_saved (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
  GomResource *resource = GOM_RESOURCE (source_object);
  XdpPermissions *permissions = XDP_PERMISSIONS (source_object);
  g_autofree SavePermissionsData *data = user_data;
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
  SavePermissionsData *data;

  task = g_task_new (doc, cancellable, callback, user_data);

  g_object_get (doc, "repository", &repository, NULL);

  permissions = xdp_permissions_new (repository, doc, app_id, perms, FALSE);

  data = g_new (SavePermissionsData, 1);
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

static void
permissions_deleted (GObject *source_object,
                     GAsyncResult *result,
                     gpointer data)
{
  GomResource *resource = GOM_RESOURCE (source_object);
  XdpPermissions *permissions = XDP_PERMISSIONS (source_object);
  XdpPermissionsRemoval *removal = data;
  XdpDocument *doc = removal->doc;
  g_autoptr (GError) error = NULL;
  GList *l;

  if (!gom_resource_delete_finish (resource, result, &error))
    {
      for (l = removal->tasks; l; l = l->next)
        {
          GTask *task = l->data;
          g_task_return_new_error (task, XDP_ERROR, XDP_ERROR_FAILED, "Failed to remove permission: %s", error->message);
        }
      g_list_free_full (removal->tasks, g_object_unref);
      doc->removals = g_list_remove (doc->removals, removal);
      g_free (removal);

      return;
    }

  doc->permissions = g_list_remove (doc->permissions, permissions);
  g_object_unref (permissions);

  for (l = removal->tasks; l; l = l->next)
    {
      GTask *task = l->data;
      g_task_return_boolean (task, TRUE);
    }
  g_list_free_full (removal->tasks, g_object_unref);
  doc->removals = g_list_remove (doc->removals, removal);
  g_free (removal);
}

void
xdp_document_revoke_permissions (XdpDocument *doc,
                                 gint64 handle,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
  XdpPermissions *permissions = NULL;
  g_autoptr (GTask) task = NULL;
  GList *l;
  XdpPermissionsRemoval *removal;

  task = g_task_new (doc, cancellable, callback, user_data);

  for (l = doc->removals; l; l = l->next)
    {
      removal = l->data;

      if (removal->handle == handle)
        {
          removal->tasks = g_list_prepend (removal->tasks, g_object_ref (task));
          return;
        }
    }

  for (l = doc->permissions; l; l = l->next)
    {
      permissions = l->data;

      if (xdp_permissions_get_id (permissions) == handle)
        {

          removal = g_new0 (XdpPermissionsRemoval, 1);
          removal->handle = handle;
          removal->tasks = g_list_prepend (removal->tasks, g_object_ref (task));
          removal->doc = doc;

          doc->removals = g_list_prepend (doc->removals, removal);

          gom_resource_delete_async (GOM_RESOURCE (permissions), permissions_deleted, removal);
          return;
        }
    }

  g_task_return_new_error (task, XDP_ERROR, XDP_ERROR_FAILED, "No such permissions");
}

gboolean
xdp_document_revoke_permissions_finish (XdpDocument *doc,
                                        GAsyncResult *result,
                                        GError **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
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

  if (doc->title != NULL)
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "Document not written yet");
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
  if (doc->title != NULL)
    {
      dir = g_file_get_path (file);
      basename = g_strdup (doc->title);
    }
  else
    {
      path = g_file_get_path (file);
      basename = g_file_get_basename (file);
      dir = g_path_get_dirname (path);
    }

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
new_document_saved (GObject *source_object,
                    GAsyncResult *result,
                    gpointer data)
{
  GomResource *resource = GOM_RESOURCE (source_object);
  GDBusMethodInvocation *invocation = data;
  GVariant *retval;
  g_autoptr(GError) error = NULL;

  if (!gom_resource_save_finish (resource, result, &error))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "Unable to save document resource: %s", error->message);
      return;
    }

  retval = g_variant_new ("()");
  g_dbus_method_invocation_return_value (invocation, retval);
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
  g_autoptr(GFile) dir = NULL;
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

  /* Here we replace the target file using a copy, this is to disconnect the final
     file from all modifications that the writing app could do to the original file
     after calling finish_update. We don't want to pass a fd to another app that
     may change under its feet. */

  if (doc->title)
    {
      int version = 0;
      dir = g_file_new_for_uri (doc->uri);

      do
        {
          g_clear_object (&dest);
          if (version == 0)
            dest = g_file_get_child (dir, doc->title);
          else
            {
              g_autofree char *filename = g_strdup_printf ("%s.%d", doc->title, version);
              dest = g_file_get_child (dir, filename);
            }
          version++;

          g_clear_error (&error);
          output = g_file_create (dest, 0, NULL, &error);
        }
      while (output == NULL && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS));

      if (output == NULL)
        {
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                                 "%s", error->message);
          goto out;
        }
    }
  else
    {
      dest = g_file_new_for_uri (doc->uri);

      output = g_file_replace (dest, NULL, FALSE, 0, NULL, &error);
      if (output == NULL)
        {
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                                 "%s", error->message);
          goto out;
        }
    }

  /* TODO:
     This copy should be done async, and ensure it can use splice().
     Take care though so that we're still handling the case of two concurrent
     updates to a new document. Only the first should look at the title
     and generate a uri. the next should use the same uri as the first.
  */

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

  if (doc->title)
    {
      g_clear_pointer (&doc->title, g_free);
      g_free (doc->uri);
      doc->uri = g_file_get_uri (dest);

      gom_resource_save_async (GOM_RESOURCE (doc), new_document_saved, invocation);
    }
  else
    {
      retval = g_variant_new ("()");
      g_dbus_method_invocation_return_value (invocation, retval);
    }

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

static void
permissions_revoked (GObject *source_object,
                     GAsyncResult *result,
                     gpointer data)
{
  XdpDocument *doc = XDP_DOCUMENT (source_object);
  GDBusMethodInvocation *invocation = data;
  g_autoptr(GError) error = NULL;

  xdp_document_revoke_permissions_finish (doc, result, &error);
  if (error)
    g_dbus_method_invocation_return_gerror (invocation, error);
  else
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
}

static void
xdp_document_handle_revoke_permissions (XdpDocument *doc,
                                        GDBusMethodInvocation *invocation,
                                        const char *app_id,
                                        GVariant *parameters)
{
  gint64 handle;

  g_variant_get (parameters, "(x)", &handle);

  if (!xdp_document_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_GRANT_PERMISSIONS))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "No permissions to revoke permissions");
      return;
    }

  xdp_document_revoke_permissions (doc, handle, NULL, permissions_revoked, invocation);
}

typedef struct {
  GDBusMethodInvocation *invocation;
  XdpPermissionFlags permissions;
} InfoData;

static void
get_info_cb (GObject *source_object,
             GAsyncResult *result,
             gpointer data)
{
  GFile *file = G_FILE (source_object);
  g_autofree InfoData *info_data = data;
  GDBusMethodInvocation *invocation = info_data->invocation;
  XdpPermissionFlags permissions = info_data->permissions;
  g_autoptr (GFileInfo) info = NULL;
  g_autoptr (GError) error = NULL;
  GVariant *parameters;
  const char *window;
  const char **attributes;
  GVariantBuilder builder;
  gint i;

  info = g_file_query_info_finish (file, result, &error);
  if (info == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }

  parameters = g_dbus_method_invocation_get_parameters (invocation);
  g_variant_get (parameters, "(&s^a&s)", &window, &attributes);

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  for (i = 0; attributes[i]; i++)
    {
      GFileAttributeType type;
      gpointer value;
      GVariant *v = NULL;

      if (!g_file_info_get_attribute_data (info, attributes[i], &type, &value, NULL))
        continue;

      switch (type)
        {
        case G_FILE_ATTRIBUTE_TYPE_STRING:
          v = g_variant_new_string ((gchar*)value);
          break;

        case G_FILE_ATTRIBUTE_TYPE_BYTE_STRING:
          v = g_variant_new_bytestring ((gchar*)value);
          break;

        case G_FILE_ATTRIBUTE_TYPE_BOOLEAN:
          v = g_variant_new_boolean (*(gboolean*)value);
          break;

        case G_FILE_ATTRIBUTE_TYPE_UINT32:
          v = g_variant_new_uint32 (*(guint32*)value);
          break;

        case G_FILE_ATTRIBUTE_TYPE_INT32:
          v = g_variant_new_int32 (*(gint32*)value);
          break;

        case G_FILE_ATTRIBUTE_TYPE_UINT64:
          v = g_variant_new_uint64 (*(guint64*)value);
          break;

        case G_FILE_ATTRIBUTE_TYPE_INT64:
          v = g_variant_new_int64 (*(gint64*)value);
          break;

        case G_FILE_ATTRIBUTE_TYPE_STRINGV:
          v = g_variant_new_strv ((const gchar * const *)value, -1);
          break;

        case G_FILE_ATTRIBUTE_TYPE_OBJECT:
        case G_FILE_ATTRIBUTE_TYPE_INVALID:
          continue;
        }

      if (strcmp (attributes[i], "access::can-read") == 0)
        {
          gboolean b = g_variant_get_boolean (v);

          g_variant_unref (v);
          v = g_variant_new_boolean (b && ((permissions & XDP_PERMISSION_FLAGS_READ) != 0));
        }
      else if (strcmp (attributes[i], "access::can-write") == 0)
        {
          gboolean b = g_variant_get_boolean (v);

          g_variant_unref (v);
          v = g_variant_new_boolean (b && ((permissions & XDP_PERMISSION_FLAGS_WRITE) != 0));
        }


      g_variant_builder_add (&builder, "{sv}", attributes[i], v);
    }

  g_dbus_method_invocation_return_value (invocation, g_variant_new ("(a{sv})", &builder));
}

static void
xdp_document_handle_get_info (XdpDocument *doc,
                              GDBusMethodInvocation *invocation,
                              const char *app_id,
                              GVariant *parameters)
{
  const char *window;
  const char **attributes;
  g_autoptr (GFile) file = NULL;
  GString *s = NULL;
  g_autofree char *attrs = NULL;
  gint i;
  const gchar * const allowed_attributes[] = {
    "standard::name",
    "standard::display-name",
    "standard::icon",
    "standard::symbolic-icon",
    "standard::content-type",
    "standard::size",
    "etag::value",
    "access::can-read",
    "access::can-write",
    NULL
  };
  InfoData *data;

  g_variant_get (parameters, "(&s^a&s)", &window, &attributes);

  if (!xdp_document_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_READ))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                             "No permissions to get file info");
      return;
    }

  s = g_string_new ("");
  for (i = 0; attributes[i]; i++)
    {
      if (!g_strv_contains (allowed_attributes, attributes[i]))
        {
          g_string_free (s, TRUE);
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                                 "Not an allowed attribute: %s", attributes[i]);
          return;
        }

      if (i > 0)
        g_string_append_c (s, ',');
      g_string_append (s, attributes[i]);
    }
  attrs = g_string_free (s, FALSE);

  file = g_file_new_for_uri (doc->uri);

  data = g_new (InfoData, 1);
  data->invocation = invocation;
  data->permissions = xdp_document_get_permissions (doc, app_id);

  g_file_query_info_async (file, attrs,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           NULL,
                           get_info_cb,
                           data);
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
  { "GrantPermissions", "(sas)", xdp_document_handle_grant_permissions},
  { "RevokePermissions", "(x)", xdp_document_handle_revoke_permissions},
  { "GetInfo", "(sas)", xdp_document_handle_get_info}
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
        g_task_return_new_error (task, error->domain, error->code, "%s", error->message);
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
        g_task_return_new_error (task, error->domain, error->code, "%s", error->message);
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
document_removal_free (DocumentRemoval *removal)
{
  g_list_free_full (removal->pending, g_object_unref);
  g_free (removal);
}

static void
document_removal_abort (DocumentRemoval *removal, GError *error)
{
  GList *l;

  for (l = removal->pending; l != NULL; l = l->next)
    {
      GTask *task = l->data;
      if (error)
        g_task_return_new_error (task, error->domain, error->code, "%s", error->message);
      else
        g_task_return_new_error (task, XDP_ERROR, XDP_ERROR_FAILED, "Removing document failed");
    }

  g_hash_table_remove (removals, &removal->id);
}

static void
document_removal_complete (DocumentRemoval *removal)
{
  GList *l;

  for (l = removal->pending; l != NULL; l = l->next)
    {
      GTask *task = l->data;

      g_task_return_boolean (task, TRUE);
    }

  g_hash_table_remove (removals, &removal->id);
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

  if (removals == NULL)
    removals = g_hash_table_new_full (g_int64_hash, g_int64_equal,
                                      NULL, (GDestroyNotify)document_removal_free);
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
      if (strcmp (uri, doc->uri) == 0 && doc->title == NULL)
        {
          g_task_return_pointer (task, g_object_ref (doc), g_object_unref);
          return;
        }
    }

  add = g_hash_table_lookup (adds, uri);
  if (add == NULL)
    {
      g_autoptr(GomFilter) filter = NULL;
      g_autoptr(GomFilter) filter_uri = NULL;
      g_autoptr(GomFilter) filter_title = NULL;
      GValue value = { 0, };

      add = g_new0 (DocumentAdd, 1);
      add->uri = g_strdup (uri);
      add->pending = g_list_append (add->pending, g_object_ref (task));

      g_hash_table_insert (adds, add->uri, add);

      g_value_init (&value, G_TYPE_STRING);
      g_value_set_string (&value, uri);
      filter_uri = gom_filter_new_eq (XDP_TYPE_DOCUMENT, "uri", &value);
      filter_title = gom_filter_new_is_null (XDP_TYPE_DOCUMENT, "title");
      filter = gom_filter_new_and (filter_uri, filter_title);

      gom_repository_find_one_async (repository, XDP_TYPE_DOCUMENT,
                                     filter, find_one_for_uri_cb, add);
    }
  else
    add->pending = g_list_append (add->pending, g_object_ref (task));
}

static void
document_with_title_saved (GObject *source_object,
                           GAsyncResult *result,
                           gpointer data)
{
  GomResource *resource = GOM_RESOURCE (source_object);
  XdpDocument *doc = XDP_DOCUMENT (source_object);
  g_autoptr(GTask) task = G_TASK (data);
  g_autoptr(GError) error = NULL;

  if (!gom_resource_save_finish (resource, result, &error))
    {
      g_task_return_new_error (task, error->domain, error->code, "%s", error->message);
      return;
    }

  g_hash_table_insert (documents, &doc->id, g_object_ref (doc));
  g_task_return_pointer (task, g_object_ref (doc), g_object_unref);
}

void
xdp_document_for_uri_and_title (GomRepository      *repository,
                                const char         *uri,
                                const char         *title,
                                GCancellable       *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
  g_autoptr (XdpDocument) doc = NULL;
  g_autoptr (GTask) task = NULL;

  ensure_documents ();

  task = g_task_new (repository, cancellable, callback, user_data);

  doc = xdp_document_new_with_title (repository, uri, title);
  gom_resource_save_async (GOM_RESOURCE (doc), document_with_title_saved, g_object_ref (task));
}

XdpDocument *
xdp_document_for_uri_finish (GomRepository *repository,
                             GAsyncResult    *result,
                             GError         **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
document_removed (GObject *source,
                  GAsyncResult *result,
                  gpointer data)
{
  GomResource *resource = GOM_RESOURCE (source);
  DocumentRemoval *removal = data;
  g_autoptr (GError) error = NULL;

  if (!gom_resource_delete_finish (resource, result, &error))
    {
      document_removal_abort (removal, error);
      return;
    }

  g_hash_table_remove (documents, &removal->id);
  document_removal_complete (removal);
}

static void
remove_document (GObject *source,
                 GAsyncResult *result,
                 gpointer data)
{
  XdpDocument *doc = XDP_DOCUMENT (source);
  DocumentRemoval *removal = data;
  g_autoptr (GError) error = NULL;

  if (!xdp_document_revoke_permissions_finish (doc, result, &error))
    {
      document_removal_abort (removal, error);
      return;
    }

  if (doc->removals != NULL)
    return;

  if (doc->permissions != NULL)
    {
      document_removal_abort (removal, NULL);
      return;
    }

  gom_resource_delete_async (GOM_RESOURCE (doc), document_removed, removal);
}

void
xdp_document_remove (GomRepository       *repository,
                     gint64               id,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
  g_autoptr (XdpDocument) doc = NULL;
  g_autoptr (GTask) task = NULL;
  DocumentRemoval *removal;

  ensure_documents ();

  task = g_task_new (repository, cancellable, callback, user_data);

  removal = g_hash_table_lookup (removals, &id);
  if (removal != NULL)
    {
      removal->pending = g_list_prepend (removal->pending, g_object_ref (task));
      return;
    }

  doc = g_hash_table_lookup (documents, &id);
  if (doc != NULL)
    {
      GList *l;

      removal = g_new0 (DocumentRemoval, 1);
      removal->id = id;
      removal->doc = doc;
      removal->pending = g_list_prepend (removal->pending, g_object_ref (task));
      g_hash_table_insert (removals, &removal->id, removal);

      l = doc->permissions;
      while (l)
        {
          GList *next = l->next;
          XdpPermissions *permissions = l->data;
          xdp_document_revoke_permissions (doc,
                                           xdp_permissions_get_id (permissions),
                                           NULL,
                                           remove_document,
                                           removal);
          l = next;
        }
      return;
    }

  g_task_return_new_error (task, XDP_ERROR, XDP_ERROR_FAILED, "No document with this handle");
}

gboolean
xdp_document_remove_finish (GomRepository  *repository,
                            GAsyncResult   *result,
                            GError        **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}
