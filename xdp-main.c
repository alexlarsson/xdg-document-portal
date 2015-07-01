#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include "xdp-dbus.h"
#include "xdp-doc-db.h"
#include "xdp-error.h"
#include "xdp-util.h"

typedef struct
{
  char *doc_id;
  int fd;
  char *owner;
  guint flags;

  GDBusMethodInvocation *finish_invocation;
} XdpDocUpdate;


static XdpDocDb *db = NULL;
static GDBusNodeInfo *introspection_data = NULL;
static GList *updates = NULL;

#define ALLOWED_ATTRIBUTES (  \
  "standard::name,"           \
  "standard::display-name,"   \
  "standard::edit-name,"      \
  "standard::copy-name,"      \
  "standard::icon,"            \
  "standard::symbolic-icon,"   \
  "standard::content-type,"    \
  "standard::size,"             \
  "standard::allocated-size,"   \
  "etag::value,"                \
  "access::can-read,"           \
  "access::can-write,"          \
  "time::modified,"             \
  "time::modified-usec,"        \
  "time::access,"               \
  "time::access-usec,"          \
  "time::changed,"              \
  "time::changed-usec,"         \
  "time::created,"              \
  "time::created-usec,"         \
  "unix::device,"               \
  "unix::inode,"                \
  "unix::mode,"                 \
  "unix::nlink,"                \
  "unix::uid,"                  \
  "unix::gid"                   \
                              )

static void queue_db_save (void);

static XdpDocUpdate *
find_update (const char *doc_id, guint32 update_id)
{
  GList *l;

  for (l = updates; l != NULL; l = l->next)
    {
      XdpDocUpdate *update = l->data;

      if (update->fd == update_id &&
          strcmp (doc_id, update->doc_id) == 0)
        return update;
    }

  return NULL;
}

static XdpDocUpdate *
find_any_update (const char *doc_id)
{
  GList *l;

  for (l = updates; l != NULL; l = l->next)
    {
      XdpDocUpdate *update = l->data;

      if (strcmp (doc_id, update->doc_id) == 0)
        return update;
    }

  return NULL;
}

static void
handle_document_read (const char *doc_id,
                      GVariant *doc,
                      GDBusMethodInvocation *invocation,
                      const char *app_id,
                      GVariant *parameters)
{
  g_autoptr(GFile) file = NULL;
  g_autofree char *path = NULL;
  GUnixFDList *fd_list = NULL;
  g_autoptr(GError) error = NULL;
  int fd, fd_id;
  GVariant *retval;

  if (!xdp_doc_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_READ))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_ALLOWED,
                                             "No permissions to open file");
      return;
    }

  if (xdp_doc_has_title (doc))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_WRITTEN,
                                             "Document not written yet");
      return;
    }

  file = g_file_new_for_uri (xdp_doc_get_uri (doc));
  path = g_file_get_path (file);

  fd = open (path, O_CLOEXEC | O_RDONLY);
  if (fd == -1)
    {
      int errsv = errno;
      if (errsv == ENOENT)
        g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NO_FILE,
                                               "Document file does not exist");
      else
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
handle_document_grant_permissions (const char *doc_id,
                                   GVariant *doc,
                                   GDBusMethodInvocation *invocation,
                                   const char *app_id,
                                   GVariant *parameters)
{
  const char *target_app_id;
  char **permissions;
  XdpPermissionFlags perms;
  gint i;

  g_variant_get (parameters, "(&s^a&s)", &target_app_id, &permissions);

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
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_FOUND,
                                                 "No such permission: %s", permissions[i]);
          return;
        }
    }

  /* Must have grant-permissions and all the newly granted permissions */
  if (!xdp_doc_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_GRANT_PERMISSIONS | perms))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_ALLOWED,
                                             "Not enough permissions");
      return;
    }

  xdp_doc_db_set_permissions (db, doc_id, target_app_id, perms, TRUE);
  queue_db_save ();
}

static void
handle_document_revoke_permissions (const char *doc_id,
                                    GVariant *doc,
                                    GDBusMethodInvocation *invocation,
                                    const char *app_id,
                                    GVariant *parameters)
{
  const char *target_app_id;
  char **permissions;
  XdpPermissionFlags perms;
  gint i;

  g_variant_get (parameters, "(&s^a&s)", &target_app_id, &permissions);

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
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_FOUND,
                                                 "No such permission: %s", permissions[i]);
          return;
        }
    }

  /* Must have grant-permissions, or be itself */
  if (!xdp_doc_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_GRANT_PERMISSIONS) ||
      strcmp (app_id, target_app_id) == 0)
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_ALLOWED,
                                             "Not enough permissions");
      return;
    }

  xdp_doc_db_set_permissions (db, doc_id, target_app_id,
                              xdp_doc_get_permissions (doc, target_app_id) & ~perms,
                              FALSE);
  queue_db_save ();
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
  char **attributes;
  GVariantBuilder builder;
  gint i;
  static GFileAttributeMatcher *allowed_mask = NULL;

  if (allowed_mask == NULL)
    allowed_mask = g_file_attribute_matcher_new (ALLOWED_ATTRIBUTES);

  info = g_file_query_info_finish (file, result, &error);
  if (info == NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NO_FILE,
                                               "Document file does not exist");
      else
        g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                               error->message);
      return;
    }

  g_file_info_set_attribute_mask (info, allowed_mask);

  attributes = g_file_info_list_attributes (info, NULL);

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
  g_strfreev (attributes);

  g_dbus_method_invocation_return_value (invocation, g_variant_new ("(a{sv})", &builder));
}

static void
handle_document_get_info (const char *doc_id,
                          GVariant *doc,
                          GDBusMethodInvocation *invocation,
                          const char *app_id,
                          GVariant *parameters)
{
  g_autoptr (GFile) file = NULL;
  InfoData *data;

  if (!xdp_doc_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_READ))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_ALLOWED,
                                             "No permissions to get file info");
      return;
    }

  file = g_file_new_for_uri (xdp_doc_get_uri (doc));

  data = g_new (InfoData, 1);
  data->invocation = invocation;
  data->permissions = xdp_doc_get_permissions (doc, app_id);

  if (xdp_doc_has_title (doc))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_WRITTEN,
                                             "Document not written yet");
      return;
    }

  g_file_query_info_async (file, ALLOWED_ATTRIBUTES,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           NULL,
                           get_info_cb,
                           data);
}

static void
document_update_free (XdpDocUpdate *update)
{
  g_clear_pointer (&update->doc_id, g_free);
  close (update->fd);
  g_free (update->owner);
  g_free (update);
}

static void
handle_document_prepare_update (const char *doc_id,
                                GVariant *doc,
                                GDBusMethodInvocation *invocation,
                                const char *app_id,
                                GVariant *parameters)
{
  const char *etag, **flags;
  g_autoptr(GFile) file = NULL;
  g_autofree char *path = NULL;
  g_autofree char *dir = NULL;
  g_autofree char *basename = NULL;
  g_autofree char *template = NULL;
  GUnixFDList *fd_list = NULL;
  g_autoptr(GError) error = NULL;
  int fd = -1, ro_fd = -1, fd_id;
  GVariant *retval;
  XdpDocUpdate *update;
  int i;
  guint update_flags = 0;

  g_variant_get (parameters, "(&s^a&s)", &etag, &flags);

  for (i = 0; flags[i] != NULL; i++)
    {
      if (strcmp (flags[i], "ensure-create") == 0)
        update_flags |= XDP_UPDATE_FLAGS_ENSURE_CREATE;
      else
        g_debug ("Unknown update flag %s\n", flags[i]);
    }

  if (!xdp_doc_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_WRITE))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_ALLOWED,
                                             "No permissions to open file");
      return;
    }

  if (!xdp_doc_has_title (doc) &&
      (update_flags & XDP_UPDATE_FLAGS_ENSURE_CREATE) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_EXISTS,
                                             "The document is already created");
      return;
    }

  file = g_file_new_for_uri (xdp_doc_get_uri (doc));
  if (xdp_doc_has_title (doc))
    {
      dir = g_file_get_path (file);
      basename = g_strdup (xdp_doc_get_title (doc));
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

  update = g_new0 (XdpDocUpdate, 1);
  update->doc_id = g_strdup (doc_id);
  update->fd = ro_fd;
  ro_fd = -1;
  update->flags = update_flags;
  update->owner = g_strdup (g_dbus_method_invocation_get_sender (invocation));

  updates = g_list_append (updates, update);

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
finish_update_copy_cb (GObject *source_object,
                       GAsyncResult *res,
                       gpointer user_data)
{
  GOutputStream *output = G_OUTPUT_STREAM (source_object);
  XdpDocUpdate *update = user_data;
  GVariant *retval;
  g_autoptr(GError) error = NULL;

  if (!xdp_copy_fd_to_out_async_finish (output, res, &error))
    {
      g_dbus_method_invocation_return_gerror (update->finish_invocation, error);
      goto out;
    }

  retval = g_variant_new ("()");
  g_dbus_method_invocation_return_value (update->finish_invocation, retval);

 out:
  document_update_free (update);
}

static void
handle_document_finish_update (const char *doc_id,
                               GVariant *doc,
                               GDBusMethodInvocation *invocation,
                               const char *app_id,
                               GVariant *parameters)
{
  guint32 id;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) dir = NULL;
  g_autoptr(GFile) dest = NULL;
  g_autoptr(GFileOutputStream) output = NULL;
  g_autofree char *uri = NULL;
  XdpDocUpdate *update = NULL;

  g_variant_get (parameters, "(u)", &id);

  update = find_update (doc_id, id);

  if (update == NULL ||
      strcmp (update->owner, g_dbus_method_invocation_get_sender (invocation)) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_FOUND,
                                             "No such update to finish");
      goto out;
    }

  updates = g_list_remove (updates, update);

  if (!xdp_doc_has_permissions (doc, app_id, XDP_PERMISSION_FLAGS_WRITE))
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_ALLOWED,
                                             "No permissions to write file");
      goto out;
    }

  /* Here we replace the target file using a copy, this is to disconnect the final
     file from all modifications that the writing app could do to the original file
     after calling finish_update. We don't want to pass a fd to another app that
     may change under its feet. */

  if (xdp_doc_has_title (doc))
    {
      int version = 0;
      dir = g_file_new_for_uri (xdp_doc_get_uri (doc));

      do
        {
          g_clear_object (&dest);
          if (version == 0)
            dest = g_file_get_child (dir, xdp_doc_get_title (doc));
          else
            {
              g_autofree char *filename = g_strdup_printf ("%s.%d",
                                                           xdp_doc_get_title (doc), version);
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

      uri = g_file_get_uri (dest);
      xdp_doc_db_update_doc (db, doc_id, uri, "");
      queue_db_save ();
    }
  else
    {
      if ((update->flags & XDP_UPDATE_FLAGS_ENSURE_CREATE) != 0)
        {
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_EXISTS,
                                                 "The document is already created");
          goto out;
        }

      dest = g_file_new_for_uri (xdp_doc_get_uri (doc));

      output = g_file_replace (dest, NULL, FALSE, 0, NULL, &error);
      if (output == NULL)
        {
          g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_FAILED,
                                                 "%s", error->message);
          goto out;
        }
    }

  update->finish_invocation = invocation;
  xdp_copy_fd_to_out_async (update->fd, G_OUTPUT_STREAM (output),
                            finish_update_copy_cb, update);

  return;

 out:
  if (update)
    document_update_free (update);
}

static void
handle_document_abort_update (const char *doc_id,
                              GVariant *doc,
                              GDBusMethodInvocation *invocation,
                              const char *app_id,
                              GVariant *parameters)
{
  guint32 id;
  XdpDocUpdate *update = NULL;
  GVariant *retval;

  g_variant_get (parameters, "(u)", &id);

  update = find_update (doc_id, id);

  if (update == NULL ||
      strcmp (update->owner, g_dbus_method_invocation_get_sender (invocation)) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, XDP_ERROR, XDP_ERROR_NOT_FOUND,
                                             "No such update to finish");
      goto out;
    }

  updates = g_list_remove (updates, update);

  // TOD: doc->outstanding_operations--; */

  retval = g_variant_new ("()");
  g_dbus_method_invocation_return_value (invocation, retval);

 out:
  if (update)
    document_update_free (update);
}

static void
handle_document_delete (const char *doc_id,
                        GVariant *doc,
                        GDBusMethodInvocation *invocation,
                        const char *app_id,
                        GVariant *parameters)
{

  XdpDocUpdate *update = NULL;

  update = find_any_update (doc_id);
  if (update)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_OPERATIONS_PENDING,
                                             "Document has pending operations");
      return;
    }

  xdp_doc_db_delete_doc (db, doc_id);
  queue_db_save ();

  g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
}

struct {
  const char *name;
  const char *args;
  void (*callback) (const char *doc_id,
                    GVariant *doc,
                    GDBusMethodInvocation *invocation,
                    const char *app_id,
                    GVariant *parameters);
} doc_methods[] = {
  { "Read", "()", handle_document_read},
  { "GrantPermissions", "(sas)", handle_document_grant_permissions},
  { "RevokePermissions", "(s)", handle_document_revoke_permissions},
  { "GetInfo", "()", handle_document_get_info},
  { "PrepareUpdate", "(sas)", handle_document_prepare_update},
  { "FinishUpdate", "(u)", handle_document_finish_update},
  { "AbortUpdate", "(u)", handle_document_abort_update},
  { "Delete", "()", handle_document_delete}
};

void
handle_document_call (const char *doc_id,
                      GVariant *doc,
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
                  (doc_methods[i].callback) (doc_id, doc, invocation, app_id, parameters);
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
got_doc_app_id_cb (GObject *source_object,
                   GAsyncResult *res,
                   gpointer user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (source_object);
  g_autofree char *id = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *app_id = NULL;

  app_id = xdp_invocation_lookup_app_id_finish (invocation, res, &error);

  if (app_id == NULL)
    g_dbus_method_invocation_return_gerror (invocation, error);
  else
    {
      g_autoptr(GVariant) doc = xdp_doc_db_lookup_doc (db, id);

      if (doc == NULL)
        g_dbus_method_invocation_return_error_literal (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_UNKNOWN_OBJECT,
                                                       "No such document");
      else
        handle_document_call (id, doc, invocation, app_id);
    }
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
  xdp_invocation_lookup_app_id (invocation, NULL, got_doc_app_id_cb, user_data);
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

  if (node != NULL)
    {
      p = g_ptr_array_new ();
      g_ptr_array_add (p, g_dbus_interface_info_ref (xdp_dbus_document_interface_info ()));
      g_ptr_array_add (p, NULL);
      return (GDBusInterfaceInfo **) g_ptr_array_free (p, FALSE);
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
  gpointer user_data_to_return = NULL;

  if (node != NULL &&
      strlen (node) > 0 &&
      g_strcmp0 (interface_name, "org.freedesktop.portal.Document") == 0)
    {
      user_data_to_return = g_strdup (node);
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

static guint save_timeout = 0;

static gboolean
queue_db_save_timeout (gpointer user_data)
{
  g_autoptr(GError) error = NULL;

  save_timeout = 0;

  if (xdp_doc_db_is_dirty (db))
    {
      if (!xdp_doc_db_save (db, &error))
        g_warning ("db save: %s\n", error->message);
    }

  return FALSE;
}

static void
queue_db_save (void)
{
  if (save_timeout != 0)
    return;

  if (xdp_doc_db_is_dirty (db))
    save_timeout = g_timeout_add_seconds (10, queue_db_save_timeout, NULL);
}

static void
portal_add (GDBusMethodInvocation *invocation,
            const char *app_id)
{
  GVariant *parameters;
  g_autofree char *id = NULL;
  const char *uri;

  if (app_id[0] != '\0')
    {
      /* don't allow this from within the sandbox */
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_NOT_ALLOWED,
                                             "Not allowed inside sandbox");
      return;
    }

  parameters = g_dbus_method_invocation_get_parameters (invocation);
  g_variant_get (parameters, "(&s)", &uri);

  id = xdp_doc_db_create_doc (db, uri, "");
  g_dbus_method_invocation_return_value (invocation,
                                         g_variant_new ("(s)", id));
  queue_db_save ();
}

static void
portal_add_local (GDBusMethodInvocation *invocation,
                  const char *app_id)
{
  GVariant *parameters;
  GDBusMessage *message;
  GUnixFDList *fd_list;
  g_autofree char *id = NULL;
  g_autofree char *proc_path = NULL;
  g_autofree char *uri = NULL;
  int fd_id, fd, fds_len, fd_flags;
  const int *fds;
  char path_buffer[PATH_MAX+1];
  g_autoptr(GFile) file = NULL;
  ssize_t symlink_size;
  struct stat st_buf, real_st_buf;

  parameters = g_dbus_method_invocation_get_parameters (invocation);
  g_variant_get (parameters, "(h)", &fd_id);

  message = g_dbus_method_invocation_get_message (invocation);
  fd_list = g_dbus_message_get_unix_fd_list (message);

  fd = -1;
  if (fd_list != NULL)
    {
      fds = g_unix_fd_list_peek_fds (fd_list, &fds_len);
      if (fd_id < fds_len)
        fd = fds[fd_id];
    }

  proc_path = g_strdup_printf ("/proc/self/fd/%d", fd);

  if (fd == -1 ||
      /* Must be able to fstat */
      fstat (fd, &st_buf) < 0 ||
      /* Must be a regular file */
      (st_buf.st_mode & S_IFMT) != S_IFREG ||
      /* Must be able to get fd flags */
      (fd_flags = fcntl (fd, F_GETFL)) == -1 ||
      /* Must be able to read */
      (fd_flags & O_ACCMODE) == O_WRONLY ||
      /* Must be able to read path from /proc/self/fd */
      (symlink_size = readlink (proc_path, path_buffer, sizeof (path_buffer) - 1)) < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_INVALID_ARGUMENT,
                                             "Invalid fd passed");
      return;
    }

  path_buffer[symlink_size] = 0;

  if (lstat (path_buffer, &real_st_buf) < 0 ||
      st_buf.st_dev != real_st_buf.st_dev ||
      st_buf.st_ino != real_st_buf.st_ino)
    {
      /* Don't leak any info about real file path existance, etc */
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_INVALID_ARGUMENT,
                                             "Invalid fd passed");
      return;
    }

  file = g_file_new_for_path (path_buffer);
  uri = g_file_get_uri (file);

  id = xdp_doc_db_create_doc (db, uri, "");

  if (app_id[0] != '\0')
    {
      /* also grant app-id perms based on file_mode */
      guint32 perms = XDP_PERMISSION_FLAGS_GRANT_PERMISSIONS | XDP_PERMISSION_FLAGS_READ;
      if ((fd_flags & O_ACCMODE) == O_RDWR)
        perms |= XDP_PERMISSION_FLAGS_WRITE;
      xdp_doc_db_set_permissions (db, id, app_id, perms, TRUE);
    }

  g_dbus_method_invocation_return_value (invocation,
                                         g_variant_new ("(s)", id));
  queue_db_save ();
}

static void
portal_new (GDBusMethodInvocation *invocation,
            const char *app_id)
{
  GVariant *parameters;
  g_autofree char *id = NULL;
  const char *uri;
  const char *title;

  if (app_id[0] != '\0')
    {
      /* don't allow this from within the sandbox for now */
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_NOT_ALLOWED,
                                             "Not allowed inside sandbox");
      return;
    }

  parameters = g_dbus_method_invocation_get_parameters (invocation);
  g_variant_get (parameters, "(&s&s)", &uri, &title);

  if (title == NULL || title[0] == '\0')
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_INVALID_ARGUMENT,
                                             "Title must not be empty");
      return;
    }

  id = xdp_doc_db_create_doc (db, uri, title);
  g_dbus_method_invocation_return_value (invocation,
                                         g_variant_new ("(s)", id));
  queue_db_save ();
}

static void
portal_new_local (GDBusMethodInvocation *invocation,
                  const char *app_id)
{
  GVariant *parameters;
  GDBusMessage *message;
  GUnixFDList *fd_list;
  g_autofree char *id = NULL;
  g_autofree char *proc_path = NULL;
  g_autofree char *uri = NULL;
  int fd_id, fd, fds_len, fd_flags;
  const int *fds;
  char path_buffer[PATH_MAX+1];
  g_autoptr(GFile) file = NULL;
  ssize_t symlink_size;
  struct stat st_buf, real_st_buf;
  const char *title;

  parameters = g_dbus_method_invocation_get_parameters (invocation);
  g_variant_get (parameters, "(h&s)", &fd_id, &title);

  if (title == NULL || title[0] == '\0')
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_INVALID_ARGUMENT,
                                             "Title must not be empty");
      return;
    }

  message = g_dbus_method_invocation_get_message (invocation);
  fd_list = g_dbus_message_get_unix_fd_list (message);

  fd = -1;
  if (fd_list != NULL)
    {
      fds = g_unix_fd_list_peek_fds (fd_list, &fds_len);
      if (fd_id < fds_len)
        fd = fds[fd_id];
    }

  proc_path = g_strdup_printf ("/proc/self/fd/%d", fd);

  if (fd == -1 ||
      /* Must be able to fstat */
      fstat (fd, &st_buf) < 0 ||
      /* Must be a directory file */
      (st_buf.st_mode & S_IFMT) != S_IFDIR ||
      /* Must be able to get fd flags */
      (fd_flags = fcntl (fd, F_GETFL)) == -1 ||
      /* Must be able to read */
      (fd_flags & O_ACCMODE) == O_WRONLY ||
      /* Must be able to read path from /proc/self/fd */
      (symlink_size = readlink (proc_path, path_buffer, sizeof (path_buffer) - 1)) < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_INVALID_ARGUMENT,
                                             "Invalid fd passed");
      return;
    }

  path_buffer[symlink_size] = 0;

  if (lstat (path_buffer, &real_st_buf) < 0 ||
      st_buf.st_dev != real_st_buf.st_dev ||
      st_buf.st_ino != real_st_buf.st_ino)
    {
      /* Don't leak any info about real file path existance, etc */
      g_dbus_method_invocation_return_error (invocation,
                                             XDP_ERROR, XDP_ERROR_INVALID_ARGUMENT,
                                             "Invalid fd passed");
      return;
    }

  file = g_file_new_for_path (path_buffer);
  uri = g_file_get_uri (file);

  id = xdp_doc_db_create_doc (db, uri, title);

  if (app_id[0] != '\0')
    {
      /* also grant app-id perms based on file_mode */
      guint32 perms = XDP_PERMISSION_FLAGS_GRANT_PERMISSIONS | XDP_PERMISSION_FLAGS_READ;
      if ((fd_flags & O_ACCMODE) == O_RDWR)
        perms |= XDP_PERMISSION_FLAGS_WRITE;
      xdp_doc_db_set_permissions (db, id, app_id, perms, TRUE);
    }

  g_dbus_method_invocation_return_value (invocation,
                                         g_variant_new ("(s)", id));
  queue_db_save ();
}

typedef void (*PortalMethod) (GDBusMethodInvocation *invocation,
                              const char *app_id);

static void
got_app_id_cb (GObject *source_object,
               GAsyncResult *res,
               gpointer user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (source_object);
  g_autoptr(GError) error = NULL;
  g_autofree char *app_id = NULL;
  PortalMethod portal_method = user_data;

  app_id = xdp_invocation_lookup_app_id_finish (invocation, res, &error);

  if (app_id == NULL)
    g_dbus_method_invocation_return_gerror (invocation, error);
  else
    portal_method (invocation, app_id);
}

static gboolean
handle_add_method (XdpDbusDocumentPortal *portal,
                   GDBusMethodInvocation *invocation,
                   const char            *uri,
                   gpointer               callback)
{
  xdp_invocation_lookup_app_id (invocation, NULL, got_app_id_cb, portal_add);

  return TRUE;
}

static gboolean
handle_add_local_method (XdpDbusDocumentPortal *portal,
                         GDBusMethodInvocation *invocation,
                         int                    handle,
                         gpointer               callback)
{
  xdp_invocation_lookup_app_id (invocation, NULL, got_app_id_cb, portal_add_local);

  return TRUE;
}

static gboolean
handle_new_method (XdpDbusDocumentPortal *portal,
                   GDBusMethodInvocation *invocation,
                   const char            *base_uri,
                   const char            *title,
                   gpointer               callback)
{
  xdp_invocation_lookup_app_id (invocation, NULL, got_app_id_cb, portal_new);

  return TRUE;
}

static gboolean
handle_new_local_method (XdpDbusDocumentPortal *portal,
                         GDBusMethodInvocation *invocation,
                         int                    base_handle,
                         const char            *title,
                         gpointer               callback)
{
  xdp_invocation_lookup_app_id (invocation, NULL, got_app_id_cb, portal_new_local);

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

  g_signal_connect (helper, "handle-add", G_CALLBACK (handle_add_method), NULL);
  g_signal_connect (helper, "handle-add-local", G_CALLBACK (handle_add_local_method), NULL);
  g_signal_connect (helper, "handle-new", G_CALLBACK (handle_new_method), NULL);
  g_signal_connect (helper, "handle-new-local", G_CALLBACK (handle_new_local_method), NULL);

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

static void
session_bus_closed (GDBusConnection *connection,
                    gboolean         remote_peer_vanished,
                    GError          *bus_error)
{
  g_autoptr(GError) error = NULL;

  if (xdp_doc_db_is_dirty (db))
    {
      if (!xdp_doc_db_save (db, &error))
        g_warning ("db save: %s\n", error->message);
    }
}

int
main (int    argc,
      char **argv)
{
  guint owner_id;
  GMainLoop *loop;
  GBytes *introspection_bytes;
  g_autoptr(GList) object_types = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *data_path = NULL;
  g_autofree char *db_path = NULL;
  g_autoptr(GFile) data_dir = NULL;
  g_autoptr(GFile) db_file = NULL;
  GDBusConnection  *session_bus;

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
  db_file = g_file_get_child (data_dir, "main.gvdb");
  db_path = g_file_get_path (db_file);

  db = xdp_doc_db_new (db_path, &error);
  if (db == NULL)
    {
      g_print ("%s\n", error->message);
      return 2;
    }

  session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (session_bus == NULL)
    {
      g_print ("No session bus: %s\n", error->message);
      return 3;
    }

  g_signal_connect (session_bus, "closed", G_CALLBACK (session_bus_closed), NULL);

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
