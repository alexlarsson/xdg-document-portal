#ifndef XDP_PERMISSIONS_H
#define XDP_PERMISSIONS_H

#include <gom/gom.h>
#include "xdp-document.h"

G_BEGIN_DECLS

#define XDP_TYPE_PERMISSIONS (xdp_permissions_get_type())

G_DECLARE_FINAL_TYPE(XdpPermissions, xdp_permissions, XDP, PERMISSIONS, GomResource);

XdpPermissions *   xdp_permissions_new             (GomRepository      *repo,
                                                    XdpDocument        *doc,
                                                    const char         *app_id,
                                                    XdpPermissionFlags  permissions,
                                                    gboolean            transient);
gint64             xdp_permissions_get_id          (XdpPermissions     *permissions);
char *             xdp_permissions_get_handle      (XdpPermissions     *permissions);
const char *       xdp_permissions_get_app_id      (XdpPermissions     *permissions);
XdpPermissionFlags xdp_permissions_get_permissions (XdpPermissions     *permissions);

G_END_DECLS

#endif /* XDP_PERMISSIONS_H */
