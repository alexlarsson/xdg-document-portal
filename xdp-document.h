#ifndef XDP_DOCUMENT_H
#define XDP_DOCUMENT_H

#include <gom/gom.h>

G_BEGIN_DECLS

#define XDP_TYPE_DOCUMENT (xdp_document_get_type())

G_DECLARE_FINAL_TYPE(XdpDocument, xdp_document, XDP, DOCUMENT, GomResource);

XdpDocument *xdp_document_new (GomRepository *repo,
			       const char *url);

XdpDocument *xdp_document_lookup (gint64 id);
void xdp_document_insert (XdpDocument *doc);

void xdp_document_handle_call (XdpDocument *doc,
			       GDBusMethodInvocation *invocation);

G_END_DECLS

#endif /* XDP_DOCUMENT_H */
