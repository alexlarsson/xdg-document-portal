#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "xdp-permissions.h"
#include "xdp-document.h"
#include "xdp-error.h"
#include "xdp-main.h"

struct _XdpPermissions
{
  GomResource parent;

  gint64 id;
  gint64 document;
  char *app_id;
  XdpPermissionFlags permissions;
  gboolean transient;
};


G_DEFINE_TYPE(XdpPermissions, xdp_permissions, GOM_TYPE_RESOURCE)

enum {
  PROP_0,
  PROP_ID,
  PROP_DOCUMENT,
  PROP_APP_ID,
  PROP_PERMISSIONS,
  PROP_TRANSIENT,
  LAST_PROP
};

static GParamSpec *gParamSpecs [LAST_PROP];

static void
xdp_permissions_finalize (GObject *object)
{
  XdpPermissions *doc = (XdpPermissions *)object;

  g_free (doc->app_id);

  G_OBJECT_CLASS (xdp_permissions_parent_class)->finalize (object);
}

static void
xdp_permissions_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  XdpPermissions *doc = (XdpPermissions *)object;

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_int64 (value, doc->id);
      break;

    case PROP_DOCUMENT:
      g_value_set_int64 (value, doc->document);
      break;

    case PROP_APP_ID:
      g_value_set_string (value, doc->app_id);
      break;

    case PROP_PERMISSIONS:
      g_value_set_uint (value, doc->permissions);
      break;

    case PROP_TRANSIENT:
      g_value_set_boolean (value, doc->transient);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
xdp_permissions_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  XdpPermissions *doc = (XdpPermissions *)object;

  switch (prop_id)
    {
    case PROP_ID:
      doc->id = g_value_get_int64 (value);
      break;

    case PROP_DOCUMENT:
      doc->document = g_value_get_int64 (value);
      break;

    case PROP_APP_ID:
      g_free (doc->app_id);
      doc->app_id = g_value_dup_string (value);
      break;

    case PROP_PERMISSIONS:
      doc->permissions = g_value_get_uint (value);
      break;

    case PROP_TRANSIENT:
      doc->transient = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
xdp_permissions_class_init (XdpPermissionsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GomResourceClass *resource_class;

  object_class->finalize = xdp_permissions_finalize;
  object_class->get_property = xdp_permissions_get_property;
  object_class->set_property = xdp_permissions_set_property;

  resource_class = GOM_RESOURCE_CLASS(klass);
  gom_resource_class_set_table (resource_class, "permissions");

  gParamSpecs [PROP_ID] =
    g_param_spec_int64 ("id", _("Id"), _("Unique Id"),
                        G_MININT64,
                        G_MAXINT64,
                        0,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_ID,
                                   gParamSpecs [PROP_ID]);
  gom_resource_class_set_primary_key (resource_class, "id");

  gParamSpecs [PROP_DOCUMENT] =
    g_param_spec_int64 ("document", _("Document"), _("Document"),
                        G_MININT64,
                        G_MAXINT64,
                        0,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_DOCUMENT,
                                   gParamSpecs [PROP_DOCUMENT]);
  gom_resource_class_set_reference (resource_class, "document", "documents", "id");
  gom_resource_class_set_notnull (resource_class, "document");

  gParamSpecs[PROP_APP_ID] = g_param_spec_string("app-id", "App id",
                                                 "Application.",
                                                 NULL, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_APP_ID,
                                   gParamSpecs[PROP_APP_ID]);
  gom_resource_class_set_notnull (resource_class, "app-id");

  gParamSpecs [PROP_PERMISSIONS] =
    g_param_spec_uint ("permissions", _("Permissions"), _("permission flags"),
                       0,
                       G_MAXUINT,
                       0,
                       (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_PERMISSIONS,
                                   gParamSpecs [PROP_PERMISSIONS]);

  gParamSpecs [PROP_TRANSIENT] =
    g_param_spec_boolean ("transient", _("Transient"), _("Transient"),
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_TRANSIENT,
                                   gParamSpecs [PROP_TRANSIENT]);
  gom_resource_class_set_property_set_mapped (resource_class, "transient", FALSE);
}

static void
xdp_permissions_init (XdpPermissions *self)
{
}

XdpPermissions *
xdp_permissions_new (GomRepository *repo,
                     XdpDocument *doc,
                     const char *app_id,
                     XdpPermissionFlags permissions,
                     gboolean transient)
{
  return g_object_new (XDP_TYPE_PERMISSIONS,
                       "repository", repo,
                       "app-id", app_id,
                       "document", xdp_document_get_id (doc),
                       "permissions", (guint)permissions,
                       "transient", transient);
}

gint64
xdp_permissions_get_id (XdpPermissions *doc)
{
  return doc->id;
}

const char *
xdp_permissions_get_app_id (XdpPermissions *doc)
{
  return doc->app_id;
}

XdpPermissionFlags
xdp_permissions_get_permissions (XdpPermissions *doc)
{
  return (XdpPermissionFlags)doc->permissions;
}
