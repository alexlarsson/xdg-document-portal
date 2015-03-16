#ifndef XDP_ERROR_H
#define XDP_ERROR_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * XdpErrorEnum:
 */
typedef enum {
  XDP_ERROR_FAILED     = 0
} XdpErrorEnum;


#define XDP_ERROR xdp_error_quark()

GQuark  xdp_error_quark      (void);

G_END_DECLS

#endif /* XDP_ERROR_H */
