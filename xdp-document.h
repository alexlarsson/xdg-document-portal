#ifndef XDP_DOCUMENT_H
#define XDP_DOCUMENT_H

#include <gom/gom.h>
#include "xdp-enums.h"

G_BEGIN_DECLS

#define XDP_TYPE_DOCUMENT (xdp_document_get_type())

G_DECLARE_FINAL_TYPE(XdpDocument, xdp_document, XDP, DOCUMENT, GomResource);

XdpDocument *xdp_document_new (GomRepository *repo,
                               const char *uri);

gint64 xdp_document_get_id (XdpDocument *doc);

XdpPermissionFlags xdp_document_get_permissions (XdpDocument *doc,
                                                 const char *app_id);
void xdp_document_grant_permissions (XdpDocument *doc,
                                     const char *app_id,
                                     XdpPermissionFlags perms);

void xdp_document_handle_call (XdpDocument *doc,
                               GDBusMethodInvocation *invocation,
                               const char *app_id);

void xdp_document_load (GomRepository      *repository,
                        gint64              id,
                        GCancellable       *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer            user_data);
XdpDocument * xdg_document_load_finish (GomRepository *repository,
                                        GAsyncResult    *result,
                                        GError         **error);
void xdp_document_for_uri (GomRepository      *repository,
                           const char         *uri,
                           GCancellable       *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer            user_data);
XdpDocument * xdp_document_for_uri_finish (GomRepository *repository,
                                           GAsyncResult    *result,
                                           GError         **error);

G_END_DECLS

#endif /* XDP_DOCUMENT_H */
