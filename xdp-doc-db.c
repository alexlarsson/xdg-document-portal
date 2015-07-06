#include "config.h"

#include <string.h>
#include <gio/gio.h>

#include "gvdb/gvdb-reader.h"
#include "gvdb/gvdb-builder.h"

#include "xdp-doc-db.h"

struct _XdpDocDb {
  GObject parent;
  GVariant *no_doc;

  char *filename;
  GvdbTable *gvdb;

  /* Map document id => GVariant (uri, title, array[(appid, perms)]) */
  GvdbTable *doc_table;
  GHashTable *doc_updates;

  /* (reverse) Map app id => [ document id ]*/
  GvdbTable *app_table;
  GHashTable *app_updates;

  /* (reverse) Map uri (with no title) => [ document id ]*/
  GvdbTable *uri_table;
  GHashTable *uri_updates;

  gboolean dirty;
};

G_DEFINE_TYPE(XdpDocDb, xdp_doc_db, G_TYPE_OBJECT)

static GVariant *
xdp_doc_new (const char *uri,
             const char *title,
             GVariant *permissions)
{
  return g_variant_new ("(&s&s@a(su))", uri, title, permissions);
}

char *
xdp_doc_dup_basename (GVariant *doc)
{
  g_autoptr(GFile) file = g_file_new_for_uri (xdp_doc_get_uri (doc));

  return g_file_get_basename (file);
}

char *
xdp_doc_dup_dirname (GVariant *doc)
{
  g_autofree char *path = xdp_doc_dup_path (doc);

  return g_path_get_dirname (path);
}

char *
xdp_doc_dup_path (GVariant *doc)
{
  g_autoptr(GFile) file = g_file_new_for_uri (xdp_doc_get_uri (doc));

  return g_file_get_path (file);
}

const char *
xdp_doc_get_uri (GVariant *doc)
{
  const char *res;

  g_variant_get_child (doc, 0, "&s", &res);
  return res;
}

const char *
xdp_doc_get_title (GVariant *doc)
{
  const char *res;

  g_variant_get_child (doc, 1, "&s", &res);
  return res;
}

gboolean
xdp_doc_has_title (GVariant *doc)
{
  const char *title = xdp_doc_get_title (doc);
  return *title != 0;
}

static void
xdp_doc_db_finalize (GObject *object)
{
  XdpDocDb *db = (XdpDocDb *)object;

  g_clear_pointer (&db->filename, g_free);
  g_clear_pointer (&db->no_doc, g_variant_unref);

  g_clear_pointer (&db->gvdb, gvdb_table_free);
  g_clear_pointer (&db->doc_table, gvdb_table_free);
  g_clear_pointer (&db->app_table, gvdb_table_free);
  g_clear_pointer (&db->uri_table, gvdb_table_free);

  g_clear_pointer (&db->doc_updates, g_hash_table_unref);
  g_clear_pointer (&db->app_updates, g_hash_table_unref);
  g_clear_pointer (&db->uri_updates, g_hash_table_unref);

  G_OBJECT_CLASS (xdp_doc_db_parent_class)->finalize (object);
}

static void
xdp_doc_db_class_init (XdpDocDbClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = xdp_doc_db_finalize;
}

static void
xdp_doc_db_init (XdpDocDb *db)
{
  db->no_doc =  xdp_doc_new ("NONE", "NONE",
                             g_variant_new_array (G_VARIANT_TYPE ("(su)"), NULL, 0));
}

XdpDocDb *
xdp_doc_db_new (const char *filename,
                GError **error)
{
  XdpDocDb *db = g_object_new (XDP_TYPE_DOC_DB, NULL);
  GvdbTable *gvdb;
  GError *my_error = NULL;

  gvdb = gvdb_table_new (filename, TRUE, &my_error);
  if (gvdb == NULL)
    {
      if (g_error_matches (my_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_error_free (my_error);
      else
        {
          g_propagate_error (error, my_error);
          return NULL;
        }
    }

  db->filename = g_strdup (filename);
  db->gvdb = gvdb;

  if (gvdb)
    {
      db->doc_table = gvdb_table_get_table (gvdb, "docs");
      db->app_table = gvdb_table_get_table (gvdb, "apps");
      db->uri_table = gvdb_table_get_table (gvdb, "uris");
    }

  db->doc_updates =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, (GDestroyNotify)g_variant_unref);
  db->app_updates =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, (GDestroyNotify)g_variant_unref);
  db->uri_updates =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, (GDestroyNotify)g_variant_unref);

  return db;
}

static gboolean
uri_empty (GVariant *uri)
{
  g_autoptr(GVariant) doc_array = g_variant_get_child_value (uri, 0);
  return g_variant_n_children (doc_array) == 0;
}

static gboolean
app_empty (GVariant *app)
{
  g_autoptr(GVariant) doc_array = g_variant_get_child_value (app, 0);
  return g_variant_n_children (doc_array) == 0;
}

gboolean
xdp_doc_db_save (XdpDocDb *db,
                 GError **error)
{
  GHashTable *root, *docs, *apps, *uris;
  GvdbTable *gvdb;
  char **keys;
  int i;

  root = gvdb_hash_table_new (NULL, NULL);
  docs = gvdb_hash_table_new (root, "docs");
  apps = gvdb_hash_table_new (root, "apps");
  uris = gvdb_hash_table_new (root, "uris");
  g_hash_table_unref (docs);
  g_hash_table_unref (apps);
  g_hash_table_unref (uris);

  keys = xdp_doc_db_list_docs (db);
  for (i = 0; keys[i] != NULL; i++)
    {
      g_autoptr(GVariant) doc = xdp_doc_db_lookup_doc (db, keys[i]);
      if (doc != NULL)
        {
          GvdbItem *item = gvdb_hash_table_insert (docs, keys[i]);
          gvdb_item_set_value (item, doc);
        }
    }
  g_strfreev (keys);

  keys = xdp_doc_db_list_apps (db);
  for (i = 0; keys[i] != NULL; i++)
    {
      g_autoptr(GVariant) app = xdp_doc_db_lookup_app (db, keys[i]);
      if (!app_empty (app))
        {
          GvdbItem *item = gvdb_hash_table_insert (apps, keys[i]);
          gvdb_item_set_value (item, app);
        }
    }
  g_strfreev (keys);

  keys = xdp_doc_db_list_uris (db);
  for (i = 0; keys[i] != NULL; i++)
    {
      g_autoptr(GVariant) uri = xdp_doc_db_lookup_uri (db, keys[i]);
      if (!uri_empty (uri))
        {
          GvdbItem *item = gvdb_hash_table_insert (uris, keys[i]);
          gvdb_item_set_value (item, uri);
        }
    }
  g_strfreev (keys);

  if (!gvdb_table_write_contents (root, db->filename, FALSE, error))
    {
      g_hash_table_unref (root);
      return FALSE;
    }

  g_hash_table_unref (root);

  gvdb = gvdb_table_new (db->filename, TRUE, error);
  if (gvdb == NULL)
    return FALSE;

  g_clear_pointer (&db->gvdb, gvdb_table_free);
  g_clear_pointer (&db->doc_table, gvdb_table_free);
  g_clear_pointer (&db->app_table, gvdb_table_free);
  g_clear_pointer (&db->uri_table, gvdb_table_free);

  g_hash_table_remove_all (db->doc_updates);
  g_hash_table_remove_all (db->app_updates);
  g_hash_table_remove_all (db->uri_updates);

  db->gvdb = gvdb;
  db->doc_table = gvdb_table_get_table (gvdb, "docs");
  db->app_table = gvdb_table_get_table (gvdb, "apps");
  db->uri_table = gvdb_table_get_table (gvdb, "uris");

  db->dirty = FALSE;

  return TRUE;
}

gboolean
xdp_doc_db_is_dirty (XdpDocDb *db)
{
  return db->dirty;
}

void
xdp_doc_db_dump (XdpDocDb *db)
{
  int i;
  char **docs, **apps, **uris;

  g_print ("docs:\n");
  docs = xdp_doc_db_list_docs (db);
  for (i = 0; docs[i] != NULL; i++)
    {
      g_autoptr(GVariant) doc = xdp_doc_db_lookup_doc (db, docs[i]);
      if (doc)
        g_print (" %s: %s\n", docs[i], g_variant_print (doc, FALSE));
    }
  g_strfreev (docs);

  g_print ("apps:\n");
  apps = xdp_doc_db_list_apps (db);
  for (i = 0; apps[i] != NULL; i++)
    {
      g_autoptr(GVariant) app = xdp_doc_db_lookup_app (db, apps[i]);
      g_print (" %s: %s\n", apps[i], g_variant_print (app, FALSE));
    }
  g_strfreev (apps);

  g_print ("uris:\n");
  uris = xdp_doc_db_list_apps (db);
  for (i = 0; apps[i] != NULL; i++)
    {
      g_autoptr(GVariant) uri = xdp_doc_db_lookup_uri (db, uris[i]);
      g_print (" %s: %s\n", uris[i], g_variant_print (uri, FALSE));
    }
  g_strfreev (uris);
}

GVariant *
xdp_doc_db_lookup_doc (XdpDocDb *db, const char *doc_id)
{
  GVariant *res;

  res = g_hash_table_lookup (db->doc_updates, doc_id);
  if (res)
    {
      if (res == db->no_doc)
        return NULL;
      return g_variant_ref (res);
    }

  if (db->doc_table)
    {
      res = gvdb_table_get_value (db->doc_table, doc_id);
      if (res)
        return g_variant_ref (res);
    }

  return NULL;
}

char **
xdp_doc_db_list_docs (XdpDocDb *db)
{
  GHashTableIter iter;
  gpointer key, value;
  GPtrArray *res;

  res = g_ptr_array_new ();

  g_hash_table_iter_init (&iter, db->doc_updates);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_ptr_array_add (res, g_strdup (key));

  if (db->doc_table)
    {
      char **table_docs = gvdb_table_get_names (db->doc_table, NULL);
      int i;

      for (i = 0; table_docs[i] != NULL; i++)
        {
          char *doc = table_docs[i];

          if (g_hash_table_lookup (db->doc_updates, doc) != NULL)
            g_free (doc);
          else
            g_ptr_array_add (res, doc);
        }
      g_free (table_docs);
    }

  g_ptr_array_add (res, NULL);
  return (char **)g_ptr_array_free (res, FALSE);
}

char **
xdp_doc_db_list_apps (XdpDocDb *db)
{
  GHashTableIter iter;
  gpointer key, value;
  GPtrArray *res;

  res = g_ptr_array_new ();

  g_hash_table_iter_init (&iter, db->app_updates);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_ptr_array_add (res, g_strdup (key));

  if (db->app_table)
    {
      char **table_apps = gvdb_table_get_names (db->app_table, NULL);
      int i;

      for (i = 0; table_apps[i] != NULL; i++)
        {
          char *app = table_apps[i];

          if (g_hash_table_lookup (db->app_updates, app) != NULL)
            g_free (app);
          else
            g_ptr_array_add (res, app);
        }
      g_free (table_apps);
    }

  g_ptr_array_add (res, NULL);
  return (char **)g_ptr_array_free (res, FALSE);
}

char **
xdp_doc_db_list_uris (XdpDocDb *db)
{
  GHashTableIter iter;
  gpointer key, value;
  GPtrArray *res;

  res = g_ptr_array_new ();

  g_hash_table_iter_init (&iter, db->uri_updates);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_ptr_array_add (res, g_strdup (key));

  if (db->uri_table)
    {
      char **table_uris = gvdb_table_get_names (db->uri_table, NULL);
      int i;

      for (i = 0; table_uris[i] != NULL; i++)
        {
          char *uri = table_uris[i];

          if (g_hash_table_lookup (db->uri_updates, uri) != NULL)
            g_free (uri);
          else
            g_ptr_array_add (res, uri);
        }
      g_free (table_uris);
    }

  g_ptr_array_add (res, NULL);
  return (char **)g_ptr_array_free (res, FALSE);
}

GVariant *
xdp_doc_db_lookup_app (XdpDocDb *db,
                       const char *app_id)
{
  GVariant *res;

  res = g_hash_table_lookup (db->app_updates, app_id);
  if (res)
    return g_variant_ref (res);

  if (db->app_table)
    {
      res = gvdb_table_get_value (db->app_table, app_id);
      if (res)
        return g_variant_ref (res);
    }

  return NULL;
}

GVariant *
xdp_doc_db_lookup_uri (XdpDocDb *db, const char *uri)
{
  GVariant *res;

  res = g_hash_table_lookup (db->uri_updates, uri);
  if (res)
    return g_variant_ref (res);

  if (db->uri_table)
    {
      res = gvdb_table_get_value (db->uri_table, uri);
      if (res)
        return g_variant_ref (res);
    }

  return NULL;
}

static void
xdp_doc_db_update_uri_docs (XdpDocDb *db,
                            const char *uri,
                            const char *doc_id,
                            gboolean added)
{
  g_autoptr(GVariant) old_uri;
  GVariantBuilder builder;
  GVariantIter iter;
  GVariant *child;
  GVariant *res, *array;
  g_autoptr(GVariant) doc_array = NULL;

  old_uri = xdp_doc_db_lookup_app (db, uri);

  g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

  if (old_uri)
    {
      doc_array = g_variant_get_child_value (old_uri, 0);
      g_variant_iter_init (&iter, doc_array);
      while ((child = g_variant_iter_next_value (&iter)))
        {
          const char *child_doc_id = g_variant_get_string (child, NULL);

          if (strcmp (doc_id, child_doc_id) == 0)
            {
              if (added)
                g_warning ("added doc already exist");
            }
          else
            g_variant_builder_add_value (&builder, child);

          g_variant_unref (child);
        }
    }

  if (added)
    g_variant_builder_add (&builder, "&s", doc_id);

  array = g_variant_builder_end (&builder);
  res = g_variant_new_tuple (&array, 1);

  g_hash_table_insert (db->uri_updates, g_strdup (uri),
                       g_variant_ref_sink (res));
}

static void
xdp_doc_db_insert_doc (XdpDocDb *db,
                       const char *id,
                       GVariant *doc)
{
  g_hash_table_insert (db->doc_updates, g_strdup (id),
                       g_variant_ref_sink (doc));
  db->dirty = TRUE;

  if (!xdp_doc_has_title (doc))
    xdp_doc_db_update_uri_docs (db, xdp_doc_get_uri (doc), id, TRUE);
}

char *
xdp_doc_db_create_doc (XdpDocDb *db,
                       const char *uri,
                       const char *title)
{
  GVariant *doc;
  int i;
  const char chars[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789";

  char id[7];

  if (title == NULL || *title == 0)
    {
      g_autoptr (GVariant) uri_v = xdp_doc_db_lookup_uri (db, uri);
      if (uri_v != NULL)
        {
          g_autoptr(GVariant) doc_array = g_variant_get_child_value (uri_v, 0);
          if (g_variant_n_children (doc_array) > 0)
            {
              const char *doc_id;
              g_variant_get_child (doc_array, 0, "&s", &doc_id);
              return g_strdup (doc_id);
            }
        }
    }

  while (TRUE)
    {
      g_autoptr(GVariant) existing_doc = NULL;
      for (i = 0; i < G_N_ELEMENTS(id) - 1; i++)
        id[i] = chars[g_random_int_range (0, strlen(chars))];
      id[i] = 0;

      existing_doc = xdp_doc_db_lookup_doc (db, id);
      if (existing_doc == NULL)
        break;
    }

  doc = xdp_doc_new (uri, title,
                     g_variant_new_array (G_VARIANT_TYPE ("(su)"), NULL, 0));
  xdp_doc_db_insert_doc (db, id, doc);

  return g_strdup (id);
}

static void
xdp_doc_db_update_app_docs (XdpDocDb *db,
                            const char *app_id,
                            const char *doc_id,
                            gboolean added)
{
  g_autoptr(GVariant) old_app = NULL;
  GVariantBuilder builder;
  GVariantIter iter;
  GVariant *child;
  GVariant *res, *array;
  g_autoptr(GVariant) doc_array = NULL;

  old_app = xdp_doc_db_lookup_app (db, app_id);

  g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

  if (old_app)
    {
      doc_array = g_variant_get_child_value (old_app, 0);
      g_variant_iter_init (&iter, doc_array);
      while ((child = g_variant_iter_next_value (&iter)))
        {
          const char *child_doc_id = g_variant_get_string (child, NULL);

          if (strcmp (doc_id, child_doc_id) == 0)
            {
              if (added)
                g_warning ("added doc already exist");
            }
          else
            g_variant_builder_add_value (&builder, child);

          g_variant_unref (child);
        }

    }

  if (added)
    g_variant_builder_add (&builder, "&s", doc_id);

  array = g_variant_builder_end (&builder);
  res = g_variant_new_tuple (&array, 1);

  g_hash_table_insert (db->app_updates, g_strdup (app_id),
                       g_variant_ref_sink (res));
}

gboolean
xdp_doc_db_delete_doc (XdpDocDb            *db,
                       const char          *doc_id)
{
  g_autoptr(GVariant) old_doc = NULL;
  g_autoptr(GVariant) old_perms = NULL;
  g_autoptr (GVariant) app_array = NULL;
  GVariant *child;
  GVariantIter iter;

  old_doc = xdp_doc_db_lookup_doc (db, doc_id);
  if (old_doc == NULL)
    {
      g_warning ("no doc %s found", doc_id);
      return FALSE;
    }

  xdp_doc_db_insert_doc (db, doc_id, db->no_doc);

  app_array = g_variant_get_child_value (old_doc, 2);
  g_variant_iter_init (&iter, app_array);
  while ((child = g_variant_iter_next_value (&iter)))
    {
      const char *child_app_id;
      guint32 old_perms;

      g_variant_get (child, "(&su)", &child_app_id, &old_perms);
      xdp_doc_db_update_app_docs (db, child_app_id, doc_id, FALSE);
      g_variant_unref (child);
    }

  xdp_doc_db_update_uri_docs (db, xdp_doc_get_uri (old_doc),
                              doc_id, FALSE);
  return TRUE;
}
gboolean
xdp_doc_db_update_doc (XdpDocDb *db,
                       const char *doc_id,
                       const char *uri,
                       const char *title)
{
  g_autoptr(GVariant) old_doc = NULL;
  g_autoptr(GVariant) old_perms = NULL;
  GVariant *doc;

  old_doc = xdp_doc_db_lookup_doc (db, doc_id);
  if (old_doc == NULL)
    {
      g_warning ("no doc %s found", doc_id);
      return FALSE;
    }
  old_perms = g_variant_get_child_value (old_doc, 2);
  doc = xdp_doc_new (uri, title, old_perms);
  xdp_doc_db_insert_doc (db, doc_id, doc);

  return TRUE;
}

gboolean
xdp_doc_db_set_permissions (XdpDocDb *db,
                            const char *doc_id,
                            const char *app_id,
                            XdpPermissionFlags permissions,
                            gboolean merge)
{
  g_autoptr(GVariant) old_doc;
  g_autoptr (GVariant) app_array = NULL;
  GVariant *doc;
  GVariantIter iter;
  GVariant *child;
  GVariantBuilder builder;
  gboolean found = FALSE;

  old_doc = xdp_doc_db_lookup_doc (db, doc_id);
  if (old_doc == NULL)
    {
      g_warning ("no doc %s found", doc_id);
      return FALSE;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

  app_array = g_variant_get_child_value (old_doc, 2);
  g_variant_iter_init (&iter, app_array);
  while ((child = g_variant_iter_next_value (&iter)))
    {
      const char *child_app_id;
      guint32 old_perms;

      g_variant_get (child, "(&su)", &child_app_id, &old_perms);

      if (strcmp (app_id, child_app_id) == 0)
        {
          found = TRUE;
          if (merge)
            permissions = permissions | old_perms;
          if (permissions != 0)
            g_variant_builder_add (&builder, "(&su)",
                                   app_id, (guint32)permissions);
        }
      else
        g_variant_builder_add_value (&builder, child);

      g_variant_unref (child);
    }

  if (permissions != 0 && !found)
    g_variant_builder_add (&builder, "(&su)", app_id, (guint32)permissions);

  doc = xdp_doc_new (xdp_doc_get_uri (old_doc),
                     xdp_doc_get_title (old_doc),
                     g_variant_builder_end (&builder));
  g_hash_table_insert (db->doc_updates, g_strdup (doc_id),
                       g_variant_ref_sink (doc));

  if (found && permissions == 0)
    xdp_doc_db_update_app_docs (db, app_id, doc_id, FALSE);
  else if (!found && permissions != 0)
    xdp_doc_db_update_app_docs (db, app_id, doc_id, TRUE);

  db->dirty = TRUE;

  return TRUE;
}

XdpPermissionFlags
xdp_doc_get_permissions (GVariant *doc,
                         const char *app_id)
{
  g_autoptr(GVariant) app_array = NULL;
  GVariantIter iter;
  GVariant *child;

  if (strcmp (app_id, "") == 0)
    return XDP_PERMISSION_FLAGS_ALL;

  app_array = g_variant_get_child_value (doc, 2);

  g_variant_iter_init (&iter, app_array);
  while ((child = g_variant_iter_next_value (&iter)))
    {
      const char *child_app_id;
      guint32 perms;

      g_variant_get_child (child, 0, "&s", &child_app_id);

      if (strcmp (app_id, child_app_id) == 0)
        {
          g_variant_get_child (child, 1, "u", &perms);
          return perms;
        }

      g_variant_unref (child);
    }

  return 0;
}

gboolean
xdp_doc_has_permissions (GVariant *doc,
                         const char *app_id,
                         XdpPermissionFlags perms)
{
  XdpPermissionFlags current_perms;

  current_perms = xdp_doc_get_permissions (doc, app_id);
  return (current_perms & perms) == perms;
}

char **
xdp_app_list_docs (GVariant *app)
{
  GPtrArray *res;
  g_autoptr(GVariant) doc_array = NULL;
  GVariantIter iter;
  GVariant *child;

  res = g_ptr_array_new ();

  doc_array = g_variant_get_child_value (app, 0);

  g_variant_iter_init (&iter, doc_array);
  while ((child = g_variant_iter_next_value (&iter)))
    {
      g_ptr_array_add (res, g_variant_dup_string (child, NULL));
      g_variant_unref (child);
    }

  g_ptr_array_add (res, NULL);

  return (char **)g_ptr_array_free (res, FALSE);
}
