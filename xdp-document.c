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

static GHashTable *documents;

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

XdpDocument *
xdp_document_new (GomRepository *repo,
                  const char *url)
{
  return g_object_new (XDP_TYPE_DOCUMENT,
                       "repository", repo,
                       "url", url);
}

gint64
xdp_document_get_id (XdpDocument *doc)
{
  return doc->id;
}

static void
xdp_document_handle_read (XdpDocument *doc,
                          GDBusMethodInvocation *invocation,
                          GVariant *parameters)
{
  const char *window;

  g_variant_get (parameters, "(s)", &window);
}

struct {
  const char *name;
  const char *args;
  void (*callback) (XdpDocument *doc,
                    GDBusMethodInvocation *invocation,
                    GVariant *parameters);
} doc_methods[] = {
  { "Read", "(s)", xdp_document_handle_read}
};

void
xdp_document_handle_call (XdpDocument *doc,
                          GDBusMethodInvocation *invocation)
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
                  (doc_methods[i].callback) (doc, invocation, parameters);
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
ensure_documents (void)
{
  if (documents == NULL)
    documents = g_hash_table_new_full (g_int64_hash, g_int64_equal,
                                       NULL, g_object_unref);
}

XdpDocument *
xdp_document_lookup (gint64 id)
{
  ensure_documents ();
  return g_hash_table_lookup (documents, &id);
}

void
xdp_document_insert (XdpDocument *doc)
{
  ensure_documents ();
  g_hash_table_insert (documents, &doc->id, g_object_ref (doc));
}
