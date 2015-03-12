#ifndef XDP_DOCUMENT_H
#define XDP_DOCUMENT_H

#include <gom/gom.h>

G_BEGIN_DECLS

#define XDP_TYPE_DOCUMENT (xdp_document_get_type())

G_DECLARE_FINAL_TYPE(XdpDocument, xdp_document, XDP, DOCUMENT, GomResource)

G_END_DECLS

#endif /* XDP_DOCUMENT_H */
