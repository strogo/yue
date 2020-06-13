// Copyright 2016 Cheng Zhao. All rights reserved.
// Use of this source code is governed by the license that can be found in the
// LICENSE file.

#include "nativeui/gtk/util/widget_util.h"

#include "base/logging.h"
#include "nativeui/gfx/color.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xatom.h>  // XA_CARDINAL
#endif

namespace nu {

bool GtkVersionCheck(int major, int minor, int micro) {
  static int actual_major = gtk_get_major_version();
  if (actual_major > major)
    return true;
  else if (actual_major < major)
    return false;

  static int actual_minor = gtk_get_minor_version();
  if (actual_minor > minor)
    return true;
  else if (actual_minor < minor)
    return false;

  static int actual_micro = gtk_get_micro_version();
  if (actual_micro >= micro)
    return true;
  else
    return false;
}

SizeF GetPreferredSizeForWidget(GtkWidget* widget) {
  GtkRequisition size;
  gtk_widget_get_preferred_size(widget, nullptr, &size);
  return SizeF(size.width, size.height);
}

void ApplyStyle(GtkWidget* widget,
                base::StringPiece name,
                base::StringPiece style) {
  void* old = g_object_get_data(G_OBJECT(widget), name.data());
  if (old)
    gtk_style_context_remove_provider(
        gtk_widget_get_style_context(widget),
        GTK_STYLE_PROVIDER(old));

  GtkCssProvider* provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(
      provider, style.data(), style.length(), nullptr);
  gtk_style_context_add_provider(
      gtk_widget_get_style_context(widget),
      GTK_STYLE_PROVIDER(provider), G_MAXUINT);
  // Store the provider inside widget.
  g_object_set_data_full(G_OBJECT(widget), name.data(), provider,
                         g_object_unref);
}

bool IsUsingCSD(GtkWindow* window) {
  GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(window));
  return gtk_style_context_has_class(context, "csd") ||
         gtk_style_context_has_class(context, "csd-solid");
}

void EnableCSD(GtkWindow* window) {
  // Required for CSD to work.
  gtk_window_set_decorated(window, true);

  // Setting a hidden titlebar to force using CSD rendering.
  gtk_window_set_titlebar(window, gtk_label_new("you should not see me"));

  // Since we are not using any titlebar, we have to override the border
  // radius of the client decoration to avoid having rounded shadow for the
  // rectange window.
  if (!g_object_get_data(G_OBJECT(window), "rectangle-decoration"))
    ApplyStyle(GTK_WIDGET(window), "rectangle-decoration",
               // Using 0 would triger a bug of GTK's shadow rendering code.
               "decoration { border-radius: 0.01px; }");
}

void DisableCSD(GtkWindow* window) {
  gtk_window_set_titlebar(window, nullptr);
  gtk_window_set_decorated(window, false);
}

bool GetNativeFrameInsets(GtkWidget* window, InsetsF* insets) {
#ifdef GDK_WINDOWING_X11
  GdkWindow* gdkwindow = gtk_widget_get_window(window);
  DCHECK(gdkwindow) << "Can only get native frame from realized window.";

  GdkDisplay* display = gdk_window_get_display(gdkwindow);
  if (!GDK_IS_X11_DISPLAY(display))
    return false;

  Atom type;
  int format;
  unsigned long nitems, bytes_after;  // NOLINT(runtime/int)
  unsigned char* data;
  Status status = XGetWindowProperty(
      GDK_DISPLAY_XDISPLAY(display),
      GDK_WINDOW_XID(gdkwindow),
      gdk_x11_get_xatom_by_name_for_display(display, "_NET_FRAME_EXTENTS"),
      0, 4, false, XA_CARDINAL, &type, &format, &nitems, &bytes_after, &data);
  if (status != Success || !data || nitems != 4)
    return false;

  float s = gtk_widget_get_scale_factor(window);
  long* p = reinterpret_cast<long*>(data);  // NOLINT(runtime/int)
  *insets = InsetsF(p[2] / s, p[0] / s, p[3] / s, p[1] / s);
  if (data)
    XFree(data);
  return true;
#else
  return false;
#endif
}

InsetsF GetClientShadow(GtkWindow* window) {
  GdkWindow* gdkwindow = gtk_widget_get_window(GTK_WIDGET(window));
  DCHECK(gdkwindow) << "Can only get client shadow from realized window.";
  // Bounds without client shadow.
  int x, y, width, height;
  gtk_window_get_position(window, &x, &y);
  gtk_window_get_size(window, &width, &height);
  // Bounds with client shadow.
  int sx, sy, swidth, sheight;
  gdk_window_get_geometry(gdkwindow, &sx, &sy, &swidth, &sheight);
  return InsetsF(y - sy,
                 x - sx,
                 (sy + sheight) - (y + height),
                 (sx + swidth) - (x + width));
}

void ForceSizeAllocation(GtkWindow* window, GtkWidget* view) {
  // Call get_preferred_width before size allocation, otherwise GTK would print
  // warnings like "How does the code know the size to allocate?".
  gint tmp;
  gtk_widget_get_preferred_width(view, &tmp, nullptr);
  gtk_widget_get_preferred_height(view, &tmp, nullptr);

  GdkRectangle rect = { 0, 0 };
  gtk_window_get_size(window, &rect.width, &rect.height);
  gtk_widget_size_allocate(view, &rect);
}

void ResizeWindow(GtkWindow* window, bool resizable, int width, int height) {
  // Clear current size requests.
  GtkWidget* vbox = gtk_bin_get_child(GTK_BIN(window));
  gtk_widget_set_size_request(GTK_WIDGET(window), -1, -1);
  gtk_widget_set_size_request(vbox, -1, -1);

  if (resizable || !IsUsingCSD(window)) {
    // gtk_window_resize only works for resizable window.
    if (resizable)
      gtk_window_resize(window, width, height);
    else
      gtk_widget_set_size_request(GTK_WIDGET(window), width, height);
  } else {
    // Setting size request on the window results in weird behavior for
    // unresizable CSD windows, probably related to size of shadows.
    gtk_widget_set_size_request(vbox, width, height);
  }

  // Set default size otherwise GTK may do weird things when setting size
  // request or changing resizable property.
  gtk_window_set_default_size(window, width, height);

  // Notify the content view of the resize.
  ForceSizeAllocation(window, vbox);
}

}  // namespace nu