#include "xdp-error.h"

#include <gio/gio.h>

static const GDBusErrorEntry xdp_error_entries[] = {
  {XDP_ERROR_FAILED,                           "org.freedesktop.portal.document.Failed"},
};

GQuark
xdp_error_quark (void)
{
  static volatile gsize quark_volatile = 0;

  g_dbus_error_register_error_domain ("xdg--error-quark",
                                      &quark_volatile,
                                      xdp_error_entries,
                                      G_N_ELEMENTS (xdp_error_entries));
  return (GQuark) quark_volatile;
}
