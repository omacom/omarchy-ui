/* omarchy-draw.c: text and surfaces, drawn the way the Qt kit draws them. */
#include "omarchy-private.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

/* ---- text -------------------------------------------------------------------------------- */

PangoFontDescription *
omarchy_font (int px, gboolean bold)
{
  const OmarchyTokens *t = omarchy_tokens ();
  PangoFontDescription *d = pango_font_description_new ();
  pango_font_description_set_family (d, t->resolved_family ? t->resolved_family : "monospace");
  pango_font_description_set_absolute_size (d, MAX (1, px) * PANGO_SCALE);
  pango_font_description_set_weight (d, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
  return d;
}

PangoLayout *
omarchy_layout (GtkWidget *w, const char *text, int px, gboolean bold)
{
  PangoLayout *l = gtk_widget_create_pango_layout (w, text ? text : "");
  PangoFontDescription *d = omarchy_font (px, bold);
  pango_layout_set_font_description (l, d);
  pango_font_description_free (d);
  return l;
}

int
omarchy_line_height (int px)
{
  /* Qt's line box: the font's ascent + descent. */
  static GHashTable *cache;
  static char *cached_family;
  const OmarchyTokens *t = omarchy_tokens ();
  if (!cache || g_strcmp0 (cached_family, t->resolved_family) != 0) {
    g_clear_pointer (&cache, g_hash_table_unref);
    cache = g_hash_table_new (g_direct_hash, g_direct_equal);
    g_free (cached_family);
    cached_family = g_strdup (t->resolved_family);
  }
  gpointer hit = g_hash_table_lookup (cache, GINT_TO_POINTER (px));
  if (hit)
    return GPOINTER_TO_INT (hit);
  /* The font's own ascent + descent per pixel of size, read at a large size so no rounding
   * creeps in, then ceil(size × ratio): the line box Qt's Text and the Rust core use. */
  static double ratio;
  static char *ratio_family;
  if (!ratio || g_strcmp0 (ratio_family, t->resolved_family) != 0) {
    PangoFontMap *map = pango_cairo_font_map_get_default ();
    g_autoptr (PangoContext) ctx = pango_font_map_create_context (map);
    cairo_font_options_t *fo = cairo_font_options_create ();
    cairo_font_options_set_hint_metrics (fo, CAIRO_HINT_METRICS_OFF);
    pango_cairo_context_set_font_options (ctx, fo);
    cairo_font_options_destroy (fo);
    PangoFontDescription *d = omarchy_font (1000, FALSE);
    PangoFontMetrics *m = pango_context_get_metrics (ctx, d, NULL);
    ratio = (pango_font_metrics_get_ascent (m) + pango_font_metrics_get_descent (m)) / (double) PANGO_SCALE / 1000.0;
    pango_font_metrics_unref (m);
    pango_font_description_free (d);
    g_free (ratio_family);
    ratio_family = g_strdup (t->resolved_family);
  }
  int h = (int) ceil (px * ratio - 1e-6);
  h = MAX (1, h);
  g_hash_table_insert (cache, GINT_TO_POINTER (px), GINT_TO_POINTER (h));
  return h;
}

void
omarchy_layout_size (PangoLayout *l, int *width, int *height)
{
  PangoRectangle logical;
  pango_layout_get_extents (l, NULL, &logical);
  /* Qt lays text out at its fractional advance; whole pixels to the nearest match it best. */
  if (width)
    *width = (int) lround (logical.width / (double) PANGO_SCALE);
  if (height) {
    int lines = pango_layout_get_line_count (l);
    *height = (int) ceil (logical.height / (double) PANGO_SCALE);
    if (lines <= 1)
      *height = MAX (*height, 1);
  }
}

int
omarchy_glyph_box (PangoLayout *l, int *width, int *height)
{
  PangoRectangle ink, logical;
  pango_layout_get_extents (l, &ink, &logical);
  double advance = logical.width / (double) PANGO_SCALE;
  double x = ink.x / (double) PANGO_SCALE, w = ink.width / (double) PANGO_SCALE;
  int left = (int) ceil (MAX (0, -x));
  int right = x + w > advance ? (int) ceil (ceil (x + w) - advance) : 0;
  if (width)
    *width = (int) ceil (advance) + left + right;
  if (height)
    *height = (int) ceil (logical.height / (double) PANGO_SCALE);
  return left;
}

double
omarchy_ink_center_x (GtkWidget *w, const char *text, int px, gboolean bold)
{
  g_autoptr (PangoLayout) l = omarchy_layout (w, text, MAX (1, px) * 100, bold);
  PangoRectangle ink, logical;
  pango_layout_get_extents (l, &ink, &logical);
  if (ink.width <= 0)
    return logical.width / 2.0 / PANGO_SCALE / 100.0;
  return (ink.x + ink.width / 2.0) / PANGO_SCALE / 100.0;
}

void
omarchy_draw_optical_glyph (GtkWidget *w, GtkSnapshot *s, const char *text, int px, float x, float y, float width,
                            float height, const GdkRGBA *color)
{
  if (!text || !*text)
    return;
  g_autoptr (PangoLayout) l = omarchy_layout (w, text, px, FALSE);
  double c = omarchy_ink_center_x (w, text, px, FALSE);
  int lh = omarchy_line_height (px);
  omarchy_draw_layout (s, l, (float) (x + width / 2.0 - c), y + roundf ((height - lh) / 2.0f), color);
}

void
omarchy_draw_layout (GtkSnapshot *s, PangoLayout *l, float x, float y, const GdkRGBA *color)
{
  gtk_snapshot_save (s);
  gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (x, y));
  gtk_snapshot_append_layout (s, l, color);
  gtk_snapshot_restore (s);
}

/* ---- borders ----------------------------------------------------------------------------- */

OmarchyBorder
omarchy_border_none (void)
{
  OmarchyBorder b;
  memset (&b, 0, sizeof b);
  return b;
}

OmarchyBorder
omarchy_border_flat (const GdkRGBA *c, float width)
{
  OmarchyBorder b = omarchy_border_none ();
  b.color = *c;
  for (int i = 0; i < 4; i++)
    b.widths[i] = width;
  return b;
}

OmarchyBorder
omarchy_border_with_width (const OmarchyBorder *src, float width)
{
  OmarchyBorder b = *src;
  for (int i = 0; i < 4; i++)
    b.widths[i] = width;
  return b;
}

float
omarchy_border_max (const OmarchyBorder *b)
{
  return MAX (MAX (b->widths[0], b->widths[1]), MAX (b->widths[2], b->widths[3]));
}

gboolean
omarchy_border_is_none (const OmarchyBorder *b)
{
  return omarchy_border_max (b) <= 0;
}

void
omarchy_border_reserve (float out[4], const OmarchyBorder *const *specs, int n)
{
  for (int i = 0; i < 4; i++)
    out[i] = 0;
  for (int k = 0; k < n; k++)
    if (specs[k])
      for (int i = 0; i < 4; i++)
        out[i] = MAX (out[i], specs[k]->widths[i]);
}

float
omarchy_radius (void)
{
  return omarchy_tokens ()->radius;
}

float
omarchy_radius_small (void)
{
  return MIN (4, omarchy_tokens ()->radius);
}

int
omarchy_space (double px)
{
  double n = px * omarchy_tokens ()->spacing_scale;
  return n <= 0 ? 0 : MAX (1, (int) lround (n));
}

static void
rounded (GskRoundedRect *out, const graphene_rect_t *r, float radius)
{
  float rad = MIN (radius, MIN (r->size.width, r->size.height) / 2);
  gsk_rounded_rect_init_from_rect (out, r, MAX (0, rad));
}

void
omarchy_draw_fill (GtkSnapshot *s, const graphene_rect_t *r, float radius, const GdkRGBA *c)
{
  if (c->alpha <= 0 || r->size.width <= 0 || r->size.height <= 0)
    return;
  if (radius <= 0) {
    gtk_snapshot_append_color (s, c, r);
    return;
  }
  GskRoundedRect rr;
  rounded (&rr, r, radius);
  gtk_snapshot_push_rounded_clip (s, &rr);
  gtk_snapshot_append_color (s, c, r);
  gtk_snapshot_pop (s);
}

void
omarchy_draw_fill_corners (GtkSnapshot *s, const graphene_rect_t *r, float tl, float tr, float br, float bl,
                           const GdkRGBA *c)
{
  if (c->alpha <= 0 || r->size.width <= 0 || r->size.height <= 0)
    return;
  float cap = MIN (r->size.width, r->size.height) / 2.0f;
  tl = CLAMP (tl, 0, cap);
  tr = CLAMP (tr, 0, cap);
  br = CLAMP (br, 0, cap);
  bl = CLAMP (bl, 0, cap);
  if (tl <= 0 && tr <= 0 && br <= 0 && bl <= 0) {
    gtk_snapshot_append_color (s, c, r);
    return;
  }
  const graphene_size_t stl = { tl, tl }, str = { tr, tr }, sbr = { br, br }, sbl = { bl, bl };
  GskRoundedRect rr;
  gsk_rounded_rect_init (&rr, r, &stl, &str, &sbr, &sbl);
  gtk_snapshot_push_rounded_clip (s, &rr);
  gtk_snapshot_append_color (s, c, r);
  gtk_snapshot_pop (s);
}

void
omarchy_draw_border (GtkSnapshot *s, const graphene_rect_t *r, float radius, const OmarchyBorder *b)
{
  if (omarchy_border_is_none (b) || r->size.width <= 0 || r->size.height <= 0)
    return;
  GskRoundedRect rr;
  rounded (&rr, r, radius);
  /* GTK's border is top, right, bottom, left, as the spec's widths are. */
  const float w[4] = { b->widths[0], b->widths[1], b->widths[2], b->widths[3] };
  if (b->n_stops < 2) {
    if (b->color.alpha <= 0)
      return;
    const GdkRGBA c[4] = { b->color, b->color, b->color, b->color };
    gtk_snapshot_append_border (s, &rr, w, c);
    return;
  }
  /* A gradient ring: the ring as a mask over a linear gradient at the spec's angle
   * (BorderGeometry.gradientEndpoints). */
  const GdkRGBA opaque[4] = { { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };
  float W = r->size.width, H = r->size.height;
  double rad = b->angle * G_PI / 180.0, dx = cos (rad), dy = sin (rad);
  double len = (fabs (W * dx) + fabs (H * dy)) / 2;
  graphene_point_t p1 = GRAPHENE_POINT_INIT (r->origin.x + W / 2 - dx * len, r->origin.y + H / 2 - dy * len);
  graphene_point_t p2 = GRAPHENE_POINT_INIT (r->origin.x + W / 2 + dx * len, r->origin.y + H / 2 + dy * len);
  GskColorStop stops[OMARCHY_MAX_STOPS];
  for (int i = 0; i < b->n_stops; i++) {
    stops[i].offset = (float) i / (b->n_stops - 1);
    stops[i].color = b->stops[i];
  }
  gtk_snapshot_push_mask (s, GSK_MASK_MODE_ALPHA);
  gtk_snapshot_append_border (s, &rr, w, opaque);
  gtk_snapshot_pop (s);
  gtk_snapshot_append_linear_gradient (s, r, &p1, &p2, stops, b->n_stops);
  gtk_snapshot_pop (s);
}

void
omarchy_draw_surface (GtkSnapshot *s, const graphene_rect_t *r, float radius, const GdkRGBA *fill,
                      const OmarchyBorder *b)
{
  if (fill && fill->alpha > 0) {
    if (b && !omarchy_border_is_none (b)) {
      /* Inside the border, as Qt's Rectangle fills: a translucent border never doubles up. */
      graphene_rect_t in = GRAPHENE_RECT_INIT (r->origin.x + b->widths[3], r->origin.y + b->widths[0],
                                               r->size.width - b->widths[1] - b->widths[3],
                                               r->size.height - b->widths[0] - b->widths[2]);
      float inner = MAX (0, radius - MAX (MAX (b->widths[0], b->widths[1]), MAX (b->widths[2], b->widths[3])));
      omarchy_draw_fill (s, &in, inner, fill);
    } else {
      omarchy_draw_fill (s, r, radius, fill);
    }
  }
  if (b)
    omarchy_draw_border (s, r, radius, b);
}
