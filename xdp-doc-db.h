#ifndef XDP_DB
#define XDP_DB

#include <glib-object.h>

#include "xdp-enums.h"

G_BEGIN_DECLS

#define XDP_TYPE_DOC_DB (xdp_doc_db_get_type())

G_DECLARE_FINAL_TYPE(XdpDocDb, xdp_doc_db, XDP, DOC_DB, GObject);

XdpDocDb *         xdp_doc_db_new             (const char          *filename,
					       GError             **error);
gboolean           xdp_doc_db_save            (XdpDocDb            *db,
					       GError             **error);
gboolean           xdp_doc_db_is_dirty        (XdpDocDb            *db);
void               xdp_doc_db_dump            (XdpDocDb            *db);
GVariant *         xdp_doc_db_lookup_doc      (XdpDocDb            *db,
					       const char          *doc_id);
GVariant *         xdp_doc_db_lookup_app      (XdpDocDb            *db,
					       const char          *app_id);
char **            xdp_doc_db_list_docs       (XdpDocDb            *db);
char **            xdp_doc_db_list_apps       (XdpDocDb            *db);
char *             xdp_doc_db_create_doc      (XdpDocDb            *db,
					       const char          *uri,
					       const char          *title);
gboolean           xdp_doc_db_set_permissions (XdpDocDb            *db,
					       const char          *doc_id,
					       const char          *app_id,
					       XdpPermissionFlags   permissions,
					       gboolean             add);

XdpPermissionFlags xdp_doc_get_permissions    (GVariant            *doc,
					       const char          *app_id);
gboolean           xdp_doc_has_permissions    (GVariant            *doc,
					       const char          *app_id,	
					       XdpPermissionFlags   permissions);
const char *       xdp_doc_get_uri            (GVariant            *doc);
const char *       xdp_doc_get_title          (GVariant            *doc);
gboolean           xdp_doc_has_title          (GVariant            *doc);

G_END_DECLS

#endif /* XDP_DB */
