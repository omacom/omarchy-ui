/* omarchy-data-private.h: what Table, Tree, Sidebar and Calendar share: the row state ladder,
 * cut text, glyph boxes and the RTL mirror (spec/components/data.md, "Shared rules"). */
#pragma once

#include "omarchy-private.h"

#include <math.h>

G_BEGIN_DECLS

static inline gboolean
data_rtl (GtkWidget *w)
{
  return gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
}

/* The left edge of a box at `x` (from the start edge), `bw` wide, in a `W` wide widget. */
static inline float
data_x (GtkWidget *w, float W, float x, float bw)
{
  return data_rtl (w) ? W - x - bw : x;
}

/* The row ladder: the hover fill on the cursor row, else the selected fill, else nothing. */
static inline void
data_row_fill (GtkSnapshot *s, float x, float y, float w, float h, gboolean cursor, gboolean selected)
{
  const OmarchyTokens *p = omarchy_paint ();
  if (cursor)
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x, y, w, h), p->radius, &p->hover.fill);
  else if (selected)
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x, y, w, h), p->radius, &p->selected.fill);
}

/* A layout cut with "…" at `width` (< 0: never cut). */
static inline PangoLayout *
data_text (GtkWidget *w, const char *text, int px, gboolean bold, int width)
{
  PangoLayout *l = omarchy_layout (w, text, px, bold);
  if (width >= 0) {
    pango_layout_set_width (l, MAX (0, width) * PANGO_SCALE);
    pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
  }
  return l;
}

static inline int
data_text_width (PangoLayout *l)
{
  int tw;
  omarchy_layout_size (l, &tw, NULL);
  return tw;
}

/* The top of a `px` line box centered in a band `h` tall at `y` (Qt's verticalCenter). */
static inline float
data_center_y (float y, float h, int px)
{
  return y + roundf ((h - omarchy_line_height (px)) / 2);
}

/* Draw a layout whose box starts at `x` (from the start edge) in a `W` wide widget. */
static inline void
data_draw_text (GtkWidget *w, GtkSnapshot *s, PangoLayout *l, float W, float x, float y, const GdkRGBA *c)
{
  int tw = data_text_width (l);
  omarchy_draw_layout (s, l, data_x (w, W, x, tw), y, c);
}

/* A Nerd Font glyph as Qt boxes it: `*box` its width, drawn with its box at `x` (start edge). */
static inline void
data_draw_glyph (GtkWidget *w, GtkSnapshot *s, const char *glyph, int px, float W, float x, float y,
                 const GdkRGBA *c)
{
  if (!glyph || !*glyph)
    return;
  g_autoptr (PangoLayout) l = omarchy_layout (w, glyph, px, FALSE);
  int gw;
  int dx = omarchy_glyph_box (l, &gw, NULL);
  omarchy_draw_layout (s, l, data_x (w, W, x, gw) + dx, y, c);
}

static inline void
data_draw_optical_glyph (GtkWidget *w, GtkSnapshot *s, const char *glyph, int px, float W, float x, float y,
                         float slot_w, float slot_h, const GdkRGBA *c)
{
  omarchy_draw_optical_glyph (w, s, glyph, px, data_x (w, W, x, slot_w), y, slot_w, slot_h, c);
}

static inline int
data_glyph_width (GtkWidget *w, const char *glyph, int px)
{
  if (!glyph || !*glyph)
    return 0;
  g_autoptr (PangoLayout) l = omarchy_layout (w, glyph, px, FALSE);
  int gw;
  omarchy_glyph_box (l, &gw, NULL);
  return gw;
}

/* Qt's `hasCursor || activeFocus || hovering`. */
static inline gboolean
data_show_cursor (GtkWidget *w, gboolean has_cursor, gboolean hovering)
{
  return has_cursor || hovering || gtk_widget_has_focus (w);
}

/* Up/Down (and j/k) as -1/+1, else 0. */
static inline int
data_vertical_step (guint keyval)
{
  if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up || keyval == GDK_KEY_k)
    return -1;
  if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down || keyval == GDK_KEY_j)
    return 1;
  return 0;
}

G_END_DECLS
