#ifndef XDP_ENUMS_H
#define XDP_ENUMS_H

G_BEGIN_DECLS

typedef enum {
  XDP_PERMISSION_FLAGS_READ               = (1<<0),
  XDP_PERMISSION_FLAGS_WRITE              = (1<<1),
  XDP_PERMISSION_FLAGS_GRANT_PERMISSIONS  = (1<<2),

  XDP_PERMISSION_FLAGS_ALL               = ((1<<3) - 1)
} XdpPermissionFlags;

typedef enum {
  XDP_UPDATE_FLAGS_NONE               = 0,
  XDP_UPDATE_FLAGS_ENSURE_CREATE      = (1<<0),
} XdpUpdateFlags;

G_END_DECLS

#endif /* XDP_ENUMS_H */
