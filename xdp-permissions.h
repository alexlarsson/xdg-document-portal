#ifndef XDP_PERMISSIONS_H
#define XDP_PERMISSIONS_H

#include <gom/gom.h>
#include "xdp-document.h"

G_BEGIN_DECLS

typedef enum {
  XDP_PERMISSION_FLAGS_READ              = (1<<0),
  XDP_PERMISSION_FLAGS_WRITE             = (1<<1),
  XDP_PERMISSION_FLAGS_GRANT_PERMISSION  = (1<<2),
} XdpPermissionFlags;

#define XDP_TYPE_PERMISSIONS (xdp_permissions_get_type())

G_DECLARE_FINAL_TYPE(XdpPermissions, xdp_permissions, XDP, PERMISSIONS, GomResource);

XdpPermissions *   xdp_permissions_new             (GomRepository      *repo,
                                                    XdpDocument        *doc,
                                                    const char         *app_id,
                                                    XdpPermissionFlags  permissions,
                                                    gboolean            transient);
gint64             xdp_permissions_get_id          (XdpPermissions     *doc);
const char *       xdp_permissions_get_app_id      (XdpPermissions     *doc);
XdpPermissionFlags xdp_permissions_get_permissions (XdpPermissions     *doc);

G_END_DECLS

#endif /* XDP_PERMISSIONS_H */
