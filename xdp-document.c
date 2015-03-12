#include <glib/gi18n.h>
#include <gio/gio.h>

#include "xdp-document.h"

struct _XdpDocument
{
  GomResource parent;

  gint64 id;
  char *url;
};

G_DEFINE_TYPE(XdpDocument, xdp_document, GOM_TYPE_RESOURCE)

enum {
  PROP_0,
  PROP_ID,
  PROP_URL,
  LAST_PROP
};

static GParamSpec *gParamSpecs [LAST_PROP];

static void
xdp_document_finalize (GObject *object)
{
  XdpDocument *doc = (XdpDocument *)object;

  g_free (doc->url);
  
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

    case PROP_URL:
      g_value_set_string (value, doc->url);
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

    case PROP_URL:
      g_free (doc->url);
      doc->url = g_value_dup_string (value);
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

  gParamSpecs[PROP_URL] = g_param_spec_string("url", "Url",
                                         "Location of data.",
					NULL, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_URL,
				   gParamSpecs[PROP_URL]);
  gom_resource_class_set_notnull(resource_class, "url");

}

static void
xdp_document_init (XdpDocument *self)
{
}
