#ifndef XDP_MAIN_H
#define XDP_MAIN_H

#include <gio/gio.h>

G_BEGIN_DECLS

void  xdp_invocation_lookup_app_id        (GDBusMethodInvocation  *invocation,
                                           GCancellable           *cancellable,
                                           GAsyncReadyCallback     callback,
                                           gpointer                user_data);
char *xdg_invocation_lookup_app_id_finish (GDBusMethodInvocation  *invocation,
                                           GAsyncResult           *result,
                                           GError                **error);

G_END_DECLS

#endif /* XDP_MAIN_H */
