/* omarchy-private.h: what the library's files share and apps don't see. */
#pragma once

#ifndef OMARCHY_GTK_COMPILATION
#define OMARCHY_GTK_COMPILATION
#endif
#include "omarchy-gtk.h"
#include "omarchy-tokens.h"

G_BEGIN_DECLS

/* The target tokens, and the tokens on screen (mid-fade colors; everything else the target). */
const OmarchyTokens *omarchy_tokens (void);
const OmarchyTokens *omarchy_paint (void);
/* I18n.t(key, english) */
const char *omarchy_t (const char *key, const char *fallback);

/* OMARCHY_GTK_SNAP=out.png: render the first window and exit (omarchy-snap.c). */
void omarchy_snap_watch (void);

/* CSS (omarchy-css.c) */
void omarchy_css_install (GdkDisplay *display);
void omarchy_css_update_structure (const OmarchyTokens *t);
void omarchy_css_update_colors (const OmarchyTokens *t);

/* Text (omarchy-draw.c) */
PangoFontDescription *omarchy_font (int px, gboolean bold);
PangoLayout *omarchy_layout (GtkWidget *w, const char *text, int px, gboolean bold);
/* The layout's box as Qt's Text boxes it: logical width, the font's line height. */
void omarchy_layout_size (PangoLayout *l, int *width, int *height);
/* A glyph's box as Qt boxes it: the advance, widened by ink that overhangs it. Returns the
 * x offset to draw at inside that box. */
int omarchy_glyph_box (PangoLayout *l, int *width, int *height);
/* Horizontal ink center of `text` from the layout origin, measured at 100× (Qt's OpticalGlyph). */
double omarchy_ink_center_x (GtkWidget *w, const char *text, int px, gboolean bold);
/* Draw a glyph centered optically horizontally by ink and vertically on its line box. */
void omarchy_draw_optical_glyph (GtkWidget *w, GtkSnapshot *s, const char *text, int px, float x, float y,
                                 float width, float height, const GdkRGBA *color);
void omarchy_draw_layout (GtkSnapshot *s, PangoLayout *l, float x, float y, const GdkRGBA *color);
int omarchy_line_height (int px);

/* Drawing (omarchy-draw.c) */
void omarchy_draw_fill (GtkSnapshot *s, const graphene_rect_t *r, float radius, const GdkRGBA *c);
void omarchy_draw_fill_corners (GtkSnapshot *s, const graphene_rect_t *r, float tl, float tr, float br, float bl,
                                const GdkRGBA *c);
void omarchy_draw_border (GtkSnapshot *s, const graphene_rect_t *r, float radius, const OmarchyBorder *b);
/* Fill inside the border, then the border: Qt's Rectangle with a border spec (BorderSurface). */
void omarchy_draw_surface (GtkSnapshot *s, const graphene_rect_t *r, float radius, const GdkRGBA *fill,
                           const OmarchyBorder *b);
OmarchyBorder omarchy_border_none (void);
OmarchyBorder omarchy_border_flat (const GdkRGBA *c, float width);
OmarchyBorder omarchy_border_with_width (const OmarchyBorder *b, float width);
gboolean omarchy_border_is_none (const OmarchyBorder *b);
float omarchy_border_max (const OmarchyBorder *b);
/* The widest of each side over several specs (reserve so state changes never relayout). */
void omarchy_border_reserve (float out[4], const OmarchyBorder *const *specs, int n);
float omarchy_radius (void);
float omarchy_radius_small (void);
int omarchy_space (double px);

/* The color a role names, on screen now (mid-fade too): foreground, dim, faint, accent, urgent,
 * success, warning, muted, background, selected, popup, tooltip, track. */
GdkRGBA omarchy_role_color (const char *role);

/* Widgets (omarchy-widget.c) */
/* Redraw on every repaint of the theme (fade frames), relayout on restyle. */
void omarchy_widget_follow (GtkWidget *w);
/* Hovered (PRELIGHT) */
gboolean omarchy_widget_hovered (GtkWidget *w);
/* Focus that should look like hover: keyboard focus (focus-visible). */
gboolean omarchy_widget_focus_shown (GtkWidget *w);
gboolean omarchy_widget_pressed (GtkWidget *w);
/* A click gesture that sets ACTIVE while held and calls `clicked(widget, button)` on release
 * inside. Button 0 (a synthesized test click) counts as the primary. */
typedef void (*OmarchyClicked) (GtkWidget *w, guint button, double x, double y);
GtkGesture *omarchy_widget_add_click (GtkWidget *w, OmarchyClicked clicked);
/* A key controller calling `pressed`; TRUE stops the key. */
typedef gboolean (*OmarchyKeyPressed) (GtkWidget *w, guint keyval, GdkModifierType state);
GtkEventController *omarchy_widget_add_keys (GtkWidget *w, OmarchyKeyPressed pressed);
gboolean omarchy_key_is_activate (guint keyval);
/* The key a reading-direction press means: h/l as Left/Right with vim, mirrored in RTL. */
guint omarchy_reading_key (GtkWidget *w, guint keyval, gboolean vim);
/* Children of a container widget (composite widgets with a GtkBox inside). */
void omarchy_widget_dispose_children (GtkWidget *w);

/* Popups (omarchy-field.c): a popover with no chrome of its own (the content draws the popups
 * surface), opened below a rect of `anchor` `gap` pixels down. */
GtkPopover *omarchy_popup_new (GtkWidget *anchor, GtkWidget *content);
void omarchy_popup_show_below (GtkPopover *pop, GtkWidget *anchor, const graphene_rect_t *trigger, int gap);

/* get_/set_ for a gboolean or string field backed by a property (relayout: queue a resize). */
#define OMARCHY_BOOL_ACCESSORS(Type, type, field, PROPS, PROP, relayout)            \
  gboolean omarchy_##type##_get_##field (Type *self) { return self->field; }        \
  void omarchy_##type##_set_##field (Type *self, gboolean v)                        \
  {                                                                                 \
    if (self->field == !!v)                                                         \
      return;                                                                       \
    self->field = !!v;                                                              \
    if (relayout)                                                                   \
      gtk_widget_queue_resize (GTK_WIDGET (self));                                  \
    else                                                                            \
      gtk_widget_queue_draw (GTK_WIDGET (self));                                    \
    g_object_notify_by_pspec (G_OBJECT (self), PROPS[PROP]);                        \
  }

#define OMARCHY_STRING_ACCESSORS(Type, type, field, PROPS, PROP, relayout)          \
  const char *omarchy_##type##_get_##field (Type *self) { return self->field; }     \
  void omarchy_##type##_set_##field (Type *self, const char *v)                     \
  {                                                                                 \
    if (!g_strcmp0 (self->field, v))                                                \
      return;                                                                       \
    g_free (self->field);                                                           \
    self->field = g_strdup (v ? v : "");                                            \
    if (relayout)                                                                   \
      gtk_widget_queue_resize (GTK_WIDGET (self));                                  \
    else                                                                            \
      gtk_widget_queue_draw (GTK_WIDGET (self));                                    \
    g_object_notify_by_pspec (G_OBJECT (self), PROPS[PROP]);                        \
  }

G_END_DECLS
