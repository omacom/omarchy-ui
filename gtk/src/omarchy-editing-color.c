/* omarchy-editing-color.c: ColorPicker (ColorPicker.qml), drawn as the Qt kit draws it. The
 * square, the strips and the swatches are focusable parts (a private widget); the hex field is
 * an OmarchyTextField. */
#include "omarchy-editing-private.h"

#include <math.h>
#include <string.h>

static const char *const default_palette[] = { "red", "orange", "yellow", "green", "cyan", "blue", "magenta",
                                               "foreground", NULL };

typedef enum { PART_SQUARE, PART_HUE, PART_ALPHA, PART_SWATCH } PartKind;

/* ============================================================================ Picker ======= */

/**
 * OmarchyColorPicker:
 *
 * An HSV color picker, `space(300)` × `space(260)` by default:
 *
 * - a saturation/value square over the hue (saturation left to right, value top to bottom),
 *   its Slider knob filled with the picked color;
 * - a `space(14)` hue strip `md` to its right and, with #OmarchyColorPicker:show-alpha, an
 *   alpha strip (opaque at the top) `md` beyond;
 * - `lg` below, a preview swatch as tall as the hex field, then the field (`#rrggbb`, or
 *   `#rrggbbaa` below full alpha). A valid hex typed into it applies at once;
 * - `lg` below that, `space(20)` swatches of the theme's colors.toml
 *   (#OmarchyColorPicker:palette-names). Pressing one picks it and keeps the alpha.
 *
 * Arrows move the square's knob 1% (Shift: 10%), Up/Down move a focused strip, Return/Space
 * pick a focused swatch. Every change the user makes updates the color first, then emits
 * ::edited.
 */
struct _OmarchyColorPicker {
  GtkWidget parent_instance;
  double hue, saturation, value, alpha;
  gboolean show_alpha, cursor;
  char *surface;
  char **palette_names;
  GPtrArray *swatch_hex; /* char *, "#rrggbb" */
  GtkWidget *square, *hue_strip, *alpha_strip, *field;
  GPtrArray *swatches; /* GtkWidget * */
  char *hex;
  gboolean from_field, writing_field;
};

enum { P_PROP_0, P_PROP_HUE, P_PROP_SATURATION, P_PROP_VALUE, P_PROP_ALPHA, P_PROP_SHOW_ALPHA, P_PROP_CURSOR,
       P_PROP_PALETTE_NAMES, P_PROP_COLOR, P_PROP_HEX, P_N_PROPS };
static GParamSpec *p_props[P_N_PROPS];
enum { P_EDITED, P_N_SIGNALS };
static guint p_signals[P_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyColorPicker, omarchy_color_picker, GTK_TYPE_WIDGET)

/* HSV (0–1) to 0–255 channels, the arithmetic Qt's and Rune's pickers use. */
static void
hsv_to_rgb (double h, double s, double v, int rgb[3])
{
  double h6 = (h - floor (h)) * 6;
  int i = (int) floor (h6);
  double f = h6 - i;
  double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  double c[6][3] = { { v, t, p }, { q, v, p }, { p, v, t }, { p, q, v }, { t, p, v }, { v, p, q } };
  for (int k = 0; k < 3; k++)
    rgb[k] = (int) omarchy_editing_round (c[((i % 6) + 6) % 6][k] * 255);
}

static GdkRGBA
rgb_color (const int rgb[3], double alpha)
{
  return (GdkRGBA) { rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, (float) alpha };
}

static void
picker_rgb (OmarchyColorPicker *self, int rgb[3])
{
  hsv_to_rgb (self->hue, self->saturation, self->value, rgb);
}

static char *
make_hex (OmarchyColorPicker *self)
{
  int rgb[3];
  picker_rgb (self, rgb);
  if (self->alpha < 1)
    return g_strdup_printf ("#%02x%02x%02x%02x", rgb[0], rgb[1], rgb[2], (int) omarchy_editing_round (self->alpha * 255));
  return g_strdup_printf ("#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
}

static void
picker_redraw_parts (OmarchyColorPicker *self)
{
  gtk_widget_queue_draw (GTK_WIDGET (self));
  gtk_widget_queue_draw (self->square);
  gtk_widget_queue_draw (self->hue_strip);
  gtk_widget_queue_draw (self->alpha_strip);
  for (guint i = 0; i < self->swatches->len; i++)
    gtk_widget_queue_draw (g_ptr_array_index (self->swatches, i));
}

/* The color moved: refresh the hex (and the field, unless the field is where it came from). */
static void
picker_changed (OmarchyColorPicker *self)
{
  g_autofree char *hex = make_hex (self);
  if (g_strcmp0 (hex, self->hex)) {
    g_free (self->hex);
    self->hex = g_steal_pointer (&hex);
    g_object_notify_by_pspec (G_OBJECT (self), p_props[P_PROP_HEX]);
  }
  if (!self->from_field && g_strcmp0 (gtk_editable_get_text (GTK_EDITABLE (self->field)), self->hex)) {
    self->writing_field = TRUE;
    gtk_editable_set_text (GTK_EDITABLE (self->field), self->hex);
    self->writing_field = FALSE;
  }
  g_object_notify_by_pspec (G_OBJECT (self), p_props[P_PROP_COLOR]);
  picker_redraw_parts (self);
}

static void
picker_set (OmarchyColorPicker *self, double *field, double v, int prop)
{
  v = CLAMP (v, 0, 1);
  if (*field == v)
    return;
  *field = v;
  g_object_notify_by_pspec (G_OBJECT (self), p_props[prop]);
  picker_changed (self);
}

static void
picker_edited (OmarchyColorPicker *self)
{
  GdkRGBA c;
  omarchy_color_picker_get_color (self, &c);
  g_signal_emit (self, p_signals[P_EDITED], 0, &c);
}

/* ---- the theme's named colors (colors.toml, read as Qt's ThemeSource reads it) */

static char *
theme_dir (void)
{
  const char *pinned = g_getenv ("OMARCHY_UI_THEME_DIR");
  if (pinned && *pinned)
    return g_strdup (pinned);
  return g_build_filename (g_get_home_dir (), ".local/state/omarchy/current/theme", NULL);
}

static GHashTable *
named_colors (void)
{
  GHashTable *out = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_autofree char *dir = theme_dir ();
  g_autofree char *path = g_build_filename (dir, "colors.toml", NULL);
  g_autofree char *text = NULL;
  if (g_file_get_contents (path, &text, NULL, NULL)) {
    static GRegex *re;
    if (!re)
      re = g_regex_new ("^\\s*([A-Za-z0-9_-]+)\\s*=\\s*[\"']?(#[0-9A-Fa-f]{6})", 0, 0, NULL);
    g_auto (GStrv) lines = g_strsplit (text, "\n", -1);
    for (int i = 0; lines[i]; i++) {
      g_autoptr (GMatchInfo) m = NULL;
      if (g_regex_match (re, lines[i], 0, &m))
        g_hash_table_insert (out, g_match_info_fetch (m, 1), g_ascii_strdown (g_match_info_fetch (m, 2), -1));
    }
  }
  const OmarchyTokens *t = omarchy_tokens ();
  const struct { const char *key; const GdkRGBA *c; } fallback[] = { { "foreground", &t->foreground },
                                                                     { "background", &t->background } };
  for (guint i = 0; i < G_N_ELEMENTS (fallback); i++)
    if (!g_hash_table_contains (out, fallback[i].key)) {
      const GdkRGBA *c = fallback[i].c;
      g_hash_table_insert (out, g_strdup (fallback[i].key),
                           g_strdup_printf ("#%02x%02x%02x", (int) lround (c->red * 255), (int) lround (c->green * 255),
                                            (int) lround (c->blue * 255)));
    }
  return out;
}

/* =============================================================================== Parts ======= */

/* A focusable part of the picker: the square, a strip, or a swatch. */
typedef struct {
  GtkWidget parent_instance;
  OmarchyColorPicker *picker;
  PartKind kind;
  int index;
  gboolean dragging;
} OmarchyColorPickerPart;

typedef struct {
  GtkWidgetClass parent_class;
} OmarchyColorPickerPartClass;

static GType color_part_get_type (void);
G_DEFINE_FINAL_TYPE (OmarchyColorPickerPart, color_part, GTK_TYPE_WIDGET)
#define COLOR_PART(o) ((OmarchyColorPickerPart *) (o))

static const char *
swatch_hex (OmarchyColorPickerPart *part)
{
  GPtrArray *a = part->picker->swatch_hex;
  return part->index < (int) a->len ? g_ptr_array_index (a, part->index) : "#000000";
}

static double
strip_fraction (OmarchyColorPickerPart *part)
{
  return part->kind == PART_HUE ? part->picker->hue : 1 - part->picker->alpha;
}

static gboolean
part_focus (GtkWidget *w)
{
  return omarchy_widget_focus_shown (w);
}

static void
part_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyColorPickerPart *part = COLOR_PART (w);
  OmarchyColorPicker *self = part->picker;
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  float small = MIN (4, p->radius);
  graphene_rect_t r = GRAPHENE_RECT_INIT (0, 0, W, H);
  GskRoundedRect rr;
  gsk_rounded_rect_init_from_rect (&rr, &r, MIN (small, MIN (W, H) / 2));
  int k = omarchy_editing_knob ();
  int rgb[3];
  picker_rgb (self, rgb);

  if (part->kind == PART_SQUARE) {
    int pure[3];
    hsv_to_rgb (self->hue, 1, 1, pure);
    GdkRGBA hue = rgb_color (pure, 1);
    omarchy_draw_fill (s, &r, small, &hue);
    gtk_snapshot_push_rounded_clip (s, &rr);
    const GskColorStop white[] = { { 0, { 1, 1, 1, 1 } }, { 1, { 1, 1, 1, 0 } } };
    gtk_snapshot_append_linear_gradient (s, &r, &GRAPHENE_POINT_INIT (0, 0), &GRAPHENE_POINT_INIT (W, 0), white, 2);
    const GskColorStop black[] = { { 0, { 0, 0, 0, 0 } }, { 1, { 0, 0, 0, 1 } } };
    gtk_snapshot_append_linear_gradient (s, &r, &GRAPHENE_POINT_INIT (0, 0), &GRAPHENE_POINT_INIT (0, H), black, 2);
    gtk_snapshot_pop (s);
    GdkRGBA fill = rgb_color (rgb, 1);
    float kx0 = CLAMP ((float) omarchy_editing_round (self->saturation * W - k / 2.0), 0.0f, MAX (0.0f, W - k));
    float ky0 = CLAMP ((float) omarchy_editing_round ((1 - self->value) * H - k / 2.0), 0.0f, MAX (0.0f, H - k));
    gboolean hot = self->cursor || part->dragging || part_focus (w);
    gtk_snapshot_save (s);
    if (hot) {
      float cx = kx0 + k / 2.0f, cy = ky0 + k / 2.0f;
      gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (cx, cy));
      gtk_snapshot_scale (s, 1.15f, 1.15f);
      gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (-cx, -cy));
    }
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (kx0, ky0, k, k), k / 2.0f, &p->background);
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (kx0 + 1, ky0 + 1, k - 2, k - 2), (k - 2) / 2.0f, &p->foreground);
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (kx0 + 2, ky0 + 2, k - 4, k - 4), (k - 4) / 2.0f, &fill);
    gtk_snapshot_restore (s);
    return;
  }

  if (part->kind == PART_HUE || part->kind == PART_ALPHA) {
    gtk_snapshot_push_rounded_clip (s, &rr);
    if (part->kind == PART_HUE) {
      const GskColorStop stops[] = {
        { 0 / 6.0f, { 1, 0, 0, 1 } }, { 1 / 6.0f, { 1, 1, 0, 1 } }, { 2 / 6.0f, { 0, 1, 0, 1 } },
        { 3 / 6.0f, { 0, 1, 1, 1 } }, { 4 / 6.0f, { 0, 0, 1, 1 } }, { 5 / 6.0f, { 1, 0, 1, 1 } },
        { 1, { 1, 0, 0, 1 } },
      };
      gtk_snapshot_append_linear_gradient (s, &r, &GRAPHENE_POINT_INIT (0, 0), &GRAPHENE_POINT_INIT (0, H), stops, 7);
    } else {
      gtk_snapshot_append_color (s, &p->track, &r);
      GdkRGBA top = rgb_color (rgb, 1), bottom = rgb_color (rgb, 0);
      const GskColorStop stops[] = { { 0, top }, { 1, bottom } };
      gtk_snapshot_append_linear_gradient (s, &r, &GRAPHENE_POINT_INIT (0, 0), &GRAPHENE_POINT_INIT (0, H), stops, 2);
    }
    gtk_snapshot_pop (s);
    float pad = omarchy_space (2);
    float sk = MAX (8.0f, W - pad * 2);
    float kx0 = (float) omarchy_editing_round ((W - sk) / 2.0);
    float ky0 = pad + (float) omarchy_editing_round (strip_fraction (part) * MAX (0.0f, H - sk - pad * 2));
    gboolean hot = part->dragging || part_focus (w);
    gtk_snapshot_save (s);
    if (hot) {
      float cx = kx0 + sk / 2.0f, cy = ky0 + sk / 2.0f;
      gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (cx, cy));
      gtk_snapshot_scale (s, 1.15f, 1.15f);
      gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (-cx, -cy));
    }
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (kx0, ky0, sk, sk), sk / 2.0f, &p->background);
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (kx0 + 1, ky0 + 1, sk - 2, sk - 2), (sk - 2) / 2.0f, &p->foreground);
    gtk_snapshot_restore (s);
    return;
  }

  /* A swatch: the current color (rgb only) takes a foreground ring. */
  GdkRGBA c;
  gdk_rgba_parse (&c, swatch_hex (part));
  gboolean chosen = self->hex && !g_ascii_strncasecmp (swatch_hex (part), self->hex, 7);
  gboolean hot = omarchy_widget_hovered (w) || part_focus (w);
  OmarchyBorder b = chosen ? omarchy_border_flat (&p->foreground, MAX (1, omarchy_space (2)))
                           : omarchy_border_flat (hot ? &p->hover.border.color : &p->normal.border.color, 1);
  omarchy_draw_surface (s, &r, small, &c, &b);
}

static void
part_pick (OmarchyColorPickerPart *part, double x, double y)
{
  OmarchyColorPicker *self = part->picker;
  GtkWidget *w = GTK_WIDGET (part);
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  if (part->kind == PART_SQUARE) {
    double s = CLAMP (x / MAX (1, W), 0, 1), v = 1 - CLAMP (y / MAX (1, H), 0, 1);
    g_object_freeze_notify (G_OBJECT (self));
    picker_set (self, &self->saturation, s, P_PROP_SATURATION);
    picker_set (self, &self->value, v, P_PROP_VALUE);
    g_object_thaw_notify (G_OBJECT (self));
  } else {
    double pad = omarchy_space (2);
    double sk = MAX (8.0, W - pad * 2);
    double span = MAX (1.0, H - sk - pad * 2);
    double f = CLAMP ((y - pad - sk / 2.0) / span, 0, 1);
    if (part->kind == PART_HUE)
      picker_set (self, &self->hue, MIN (f, 0.9999), P_PROP_HUE);
    else
      picker_set (self, &self->alpha, 1 - f, P_PROP_ALPHA);
  }
  picker_edited (self);
}

static void
swatch_choose (OmarchyColorPickerPart *part)
{
  OmarchyColorPicker *self = part->picker;
  double a = self->alpha;
  g_object_freeze_notify (G_OBJECT (self));
  omarchy_color_picker_set_color (self, swatch_hex (part));
  omarchy_color_picker_set_alpha (self, a);
  g_object_thaw_notify (G_OBJECT (self));
  picker_edited (self);
}

static void
part_drag_begin (GtkWidget *w, double x, double y)
{
  OmarchyColorPickerPart *part = COLOR_PART (w);
  if (part->kind == PART_SWATCH)
    return;
  gtk_widget_grab_focus (w);
  part->dragging = TRUE;
  /* Keep the press for the updates. */
  double *start = g_object_get_data (G_OBJECT (w), "press");
  start[0] = x;
  start[1] = y;
  part_pick (part, x, y);
}

static void
part_drag_update (GtkWidget *w, double dx, double dy)
{
  OmarchyColorPickerPart *part = COLOR_PART (w);
  if (!part->dragging)
    return;
  double *start = g_object_get_data (G_OBJECT (w), "press");
  part_pick (part, start[0] + dx, start[1] + dy);
}

static void
part_drag_end (GtkWidget *w, double dx, double dy)
{
  OmarchyColorPickerPart *part = COLOR_PART (w);
  part->dragging = FALSE;
  gtk_widget_queue_draw (w);
}

static void
part_clicked (GtkWidget *w, guint button, double x, double y)
{
  swatch_choose (COLOR_PART (w));
}

static gboolean
part_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyColorPickerPart *part = COLOR_PART (w);
  OmarchyColorPicker *self = part->picker;
  double step = (state & GDK_SHIFT_MASK) ? 0.1 : 0.01;
  gboolean up = keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up;
  gboolean down = keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down;
  switch (part->kind) {
  case PART_SQUARE:
    if (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left)
      picker_set (self, &self->saturation, MAX (0, self->saturation - step), P_PROP_SATURATION);
    else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right)
      picker_set (self, &self->saturation, MIN (1, self->saturation + step), P_PROP_SATURATION);
    else if (up)
      picker_set (self, &self->value, MIN (1, self->value + step), P_PROP_VALUE);
    else if (down)
      picker_set (self, &self->value, MAX (0, self->value - step), P_PROP_VALUE);
    else
      return FALSE;
    break;
  case PART_HUE:
  case PART_ALPHA: {
    double f = strip_fraction (part);
    if (up)
      f = MAX (0, f - step);
    else if (down)
      f = MIN (1, f + step);
    else
      return FALSE;
    if (part->kind == PART_HUE)
      picker_set (self, &self->hue, MIN (f, 0.9999), P_PROP_HUE);
    else
      picker_set (self, &self->alpha, 1 - f, P_PROP_ALPHA);
    break;
  }
  case PART_SWATCH:
    if (!omarchy_key_is_activate (keyval))
      return FALSE;
    swatch_choose (part);
    return TRUE;
  }
  picker_edited (self);
  return TRUE;
}

static void
color_part_init (OmarchyColorPickerPart *part)
{
  GtkWidget *w = GTK_WIDGET (part);
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  g_object_set_data_full (G_OBJECT (w), "press", g_new0 (double, 2), g_free);
  omarchy_widget_add_keys (w, part_key);
}

static void
color_part_class_init (OmarchyColorPickerPartClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  wc->snapshot = part_snapshot;
  gtk_widget_class_set_css_name (wc, "omarchy-color-picker-part");
}

static GtkWidget *
part_new (OmarchyColorPicker *picker, PartKind kind, int index)
{
  OmarchyColorPickerPart *part = g_object_new (color_part_get_type (), NULL);
  GtkWidget *w = GTK_WIDGET (part);
  part->picker = picker;
  part->kind = kind;
  part->index = index;
  if (kind == PART_SWATCH) {
    omarchy_widget_add_click (w, part_clicked);
  } else {
    omarchy_editing_add_drag (w, part_drag_begin, part_drag_update, part_drag_end);
    gtk_widget_set_cursor_from_name (w, kind == PART_SQUARE ? "crosshair" : "pointer");
  }
  return w;
}

/* ======================================================================== Picker body ======= */

static void
rebuild_swatches (OmarchyColorPicker *self)
{
  for (guint i = 0; i < self->swatches->len; i++)
    gtk_widget_unparent (g_ptr_array_index (self->swatches, i));
  g_ptr_array_set_size (self->swatches, 0);
  g_ptr_array_set_size (self->swatch_hex, 0);
  g_autoptr (GHashTable) named = named_colors ();
  const char *const *names = self->palette_names ? (const char *const *) self->palette_names : default_palette;
  for (int i = 0; names[i]; i++) {
    const char *hex = g_hash_table_lookup (named, names[i]);
    if (!hex)
      continue;
    g_ptr_array_add (self->swatch_hex, g_strdup (hex));
    GtkWidget *sw = part_new (self, PART_SWATCH, self->swatch_hex->len - 1);
    gtk_widget_set_parent (sw, GTK_WIDGET (self));
    g_ptr_array_add (self->swatches, sw);
  }
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
field_changed (GtkEditable *e, OmarchyColorPicker *self)
{
  if (self->writing_field)
    return;
  g_autofree char *text = g_strstrip (g_strdup (gtk_editable_get_text (e)));
  self->from_field = TRUE;
  gboolean ok = omarchy_color_picker_set_color (self, text);
  self->from_field = FALSE;
  if (ok)
    picker_edited (self);
}

static int
field_height (OmarchyColorPicker *self)
{
  int h;
  gtk_widget_measure (self->field, GTK_ORIENTATION_VERTICAL, -1, NULL, &h, NULL, NULL);
  return h;
}

static void
picker_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyColorPicker *self = OMARCHY_COLOR_PICKER (w);
  const OmarchyTokens *t = omarchy_tokens ();
  int k = omarchy_editing_knob ();
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = k + (omarchy_space (14) + t->md) * 2 + omarchy_space (40);
    *nat = MAX (*min, omarchy_space (300));
  } else {
    *min = k + field_height (self) + omarchy_space (20) + t->lg * 2;
    *nat = MAX (*min, omarchy_space (260));
  }
}

typedef struct {
  int strip, sw, field_h, top_h, square_w;
} PickerGeo;

static PickerGeo
picker_geo (OmarchyColorPicker *self, int width, int height)
{
  const OmarchyTokens *t = omarchy_tokens ();
  PickerGeo g;
  g.strip = omarchy_space (14);
  g.sw = omarchy_space (20);
  g.field_h = field_height (self);
  g.top_h = MAX (0, height - t->lg * 2 - g.field_h - (self->swatches->len ? g.sw : 0));
  g.square_w = MAX (0, width - (g.strip + t->md) * (self->show_alpha ? 2 : 1));
  return g;
}

static void
picker_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyColorPicker *self = OMARCHY_COLOR_PICKER (w);
  const OmarchyTokens *t = omarchy_tokens ();
  PickerGeo g = picker_geo (self, width, height);
  gtk_widget_size_allocate (self->square, &(GtkAllocation) { 0, 0, g.square_w, g.top_h }, -1);
  int x = g.square_w + t->md;
  gtk_widget_size_allocate (self->hue_strip, &(GtkAllocation) { x, 0, g.strip, g.top_h }, -1);
  gtk_widget_set_child_visible (self->alpha_strip, self->show_alpha);
  if (self->show_alpha)
    gtk_widget_size_allocate (self->alpha_strip, &(GtkAllocation) { x + g.strip + t->md, 0, g.strip, g.top_h }, -1);
  int y = g.top_h + t->lg;
  int fx = g.field_h + t->md;
  gtk_widget_size_allocate (self->field, &(GtkAllocation) { fx, y, MAX (0, width - fx), g.field_h }, -1);
  y += g.field_h + t->lg;
  for (guint i = 0; i < self->swatches->len; i++)
    gtk_widget_size_allocate (g_ptr_array_index (self->swatches, i),
                              &(GtkAllocation) { i * (g.sw + t->md), y, g.sw, g.sw }, -1);
}

static void
picker_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyColorPicker *self = OMARCHY_COLOR_PICKER (w);
  const OmarchyTokens *p = omarchy_paint ();
  const OmarchyTokens *t = omarchy_tokens ();
  PickerGeo g = picker_geo (self, gtk_widget_get_width (w), gtk_widget_get_height (w));
  gtk_widget_snapshot_child (w, self->square, s);
  gtk_widget_snapshot_child (w, self->hue_strip, s);
  if (self->show_alpha)
    gtk_widget_snapshot_child (w, self->alpha_strip, s);
  /* The preview: the color with its alpha, a 1px normal border. */
  GdkRGBA c;
  omarchy_color_picker_get_color (self, &c);
  OmarchyBorder b = omarchy_border_flat (&p->normal.border.color, 1);
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, g.top_h + t->lg, g.field_h, g.field_h), p->radius, &c, &b);
  gtk_widget_snapshot_child (w, self->field, s);
  for (guint i = 0; i < self->swatches->len; i++)
    gtk_widget_snapshot_child (w, g_ptr_array_index (self->swatches, i), s);
}

static void
omarchy_color_picker_init (OmarchyColorPicker *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->value = 1;
  self->alpha = 1;
  self->show_alpha = TRUE;
  self->surface = g_strdup ("popups");
  self->palette_names = g_strdupv ((char **) default_palette);
  self->swatches = g_ptr_array_new ();
  self->swatch_hex = g_ptr_array_new_with_free_func (g_free);
  omarchy_widget_follow (w);
  self->square = part_new (self, PART_SQUARE, 0);
  self->hue_strip = part_new (self, PART_HUE, 0);
  self->alpha_strip = part_new (self, PART_ALPHA, 0);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self->hue_strip), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  omarchy_t ("color.hue", "Hue"), -1);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self->alpha_strip), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  omarchy_t ("color.alpha", "Alpha"), -1);
  gtk_widget_set_parent (self->square, w);
  gtk_widget_set_parent (self->hue_strip, w);
  gtk_widget_set_parent (self->alpha_strip, w);
  self->field = omarchy_text_field_new ();
  gtk_widget_set_parent (self->field, w);
  g_signal_connect (self->field, "changed", G_CALLBACK (field_changed), self);
  self->hex = make_hex (self);
  self->writing_field = TRUE;
  gtk_editable_set_text (GTK_EDITABLE (self->field), self->hex);
  self->writing_field = FALSE;
  rebuild_swatches (self);
  g_signal_connect_object (omarchy_theme_get_default (), "changed", G_CALLBACK (rebuild_swatches), self,
                           G_CONNECT_SWAPPED);
}

static void
picker_dispose (GObject *o)
{
  OmarchyColorPicker *self = OMARCHY_COLOR_PICKER (o);
  if (self->swatches) {
    for (guint i = 0; i < self->swatches->len; i++)
      gtk_widget_unparent (g_ptr_array_index (self->swatches, i));
    g_ptr_array_set_size (self->swatches, 0);
  }
  g_clear_pointer (&self->square, gtk_widget_unparent);
  g_clear_pointer (&self->hue_strip, gtk_widget_unparent);
  g_clear_pointer (&self->alpha_strip, gtk_widget_unparent);
  g_clear_pointer (&self->field, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_color_picker_parent_class)->dispose (o);
}

static void
picker_finalize (GObject *o)
{
  OmarchyColorPicker *self = OMARCHY_COLOR_PICKER (o);
  g_free (self->surface);
  g_free (self->hex);
  g_strfreev (self->palette_names);
  g_ptr_array_unref (self->swatches);
  g_ptr_array_unref (self->swatch_hex);
  G_OBJECT_CLASS (omarchy_color_picker_parent_class)->finalize (o);
}

static void
picker_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyColorPicker *self = OMARCHY_COLOR_PICKER (o);
  switch (id) {
  case P_PROP_HUE: omarchy_color_picker_set_hue (self, g_value_get_double (v)); break;
  case P_PROP_SATURATION: omarchy_color_picker_set_saturation (self, g_value_get_double (v)); break;
  case P_PROP_VALUE: omarchy_color_picker_set_value (self, g_value_get_double (v)); break;
  case P_PROP_ALPHA: omarchy_color_picker_set_alpha (self, g_value_get_double (v)); break;
  case P_PROP_SHOW_ALPHA: omarchy_color_picker_set_show_alpha (self, g_value_get_boolean (v)); break;
  case P_PROP_CURSOR: omarchy_color_picker_set_cursor (self, g_value_get_boolean (v)); break;
  case P_PROP_PALETTE_NAMES: omarchy_color_picker_set_palette_names (self, g_value_get_boxed (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
picker_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyColorPicker *self = OMARCHY_COLOR_PICKER (o);
  switch (id) {
  case P_PROP_HUE: g_value_set_double (v, self->hue); break;
  case P_PROP_SATURATION: g_value_set_double (v, self->saturation); break;
  case P_PROP_VALUE: g_value_set_double (v, self->value); break;
  case P_PROP_ALPHA: g_value_set_double (v, self->alpha); break;
  case P_PROP_SHOW_ALPHA: g_value_set_boolean (v, self->show_alpha); break;
  case P_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case P_PROP_PALETTE_NAMES: g_value_set_boxed (v, self->palette_names); break;
  case P_PROP_COLOR: {
    GdkRGBA c;
    omarchy_color_picker_get_color (self, &c);
    g_value_set_boxed (v, &c);
    break;
  }
  case P_PROP_HEX: g_value_set_string (v, self->hex); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_color_picker_class_init (OmarchyColorPickerClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = picker_dispose;
  oc->finalize = picker_finalize;
  oc->set_property = picker_set_property;
  oc->get_property = picker_get_property;
  wc->measure = picker_measure;
  wc->size_allocate = picker_allocate;
  wc->snapshot = picker_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  const GParamFlags ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyColorPicker:hue:
   *
   * The hue, 0–1 (red at 0).
   */
  p_props[P_PROP_HUE] = g_param_spec_double ("hue", NULL, NULL, 0, 1, 0, f);
  /**
   * OmarchyColorPicker:saturation:
   *
   * The saturation, 0–1 (the square, left to right).
   */
  p_props[P_PROP_SATURATION] = g_param_spec_double ("saturation", NULL, NULL, 0, 1, 0, f);
  /**
   * OmarchyColorPicker:value:
   *
   * The value, 0–1 (the square, bottom to top).
   */
  p_props[P_PROP_VALUE] = g_param_spec_double ("value", NULL, NULL, 0, 1, 1, f);
  /**
   * OmarchyColorPicker:alpha:
   *
   * The opacity, 0–1.
   */
  p_props[P_PROP_ALPHA] = g_param_spec_double ("alpha", NULL, NULL, 0, 1, 1, f);
  /**
   * OmarchyColorPicker:show-alpha:
   *
   * Whether the alpha strip shows.
   */
  p_props[P_PROP_SHOW_ALPHA] = g_param_spec_boolean ("show-alpha", NULL, NULL, TRUE, f);
  /**
   * OmarchyColorPicker:cursor:
   *
   * Draws the square's knob hot (Qt's `hasCursor`).
   */
  p_props[P_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyColorPicker:palette-names:
   *
   * The theme's colors.toml keys the swatches show, in order; missing ones are skipped.
   * `red orange yellow green cyan blue magenta foreground` by default.
   */
  p_props[P_PROP_PALETTE_NAMES] = g_param_spec_boxed ("palette-names", NULL, NULL, G_TYPE_STRV, f);
  /**
   * OmarchyColorPicker:color:
   *
   * The picked color, with its alpha.
   */
  p_props[P_PROP_COLOR] = g_param_spec_boxed ("color", NULL, NULL, GDK_TYPE_RGBA, ro);
  /**
   * OmarchyColorPicker:hex:
   *
   * The picked color as `#rrggbb`, or `#rrggbbaa` below full alpha, lowercase.
   */
  p_props[P_PROP_HEX] = g_param_spec_string ("hex", NULL, NULL, NULL, ro);
  g_object_class_install_properties (oc, P_N_PROPS, p_props);
  /**
   * OmarchyColorPicker::edited:
   * @self: the picker
   * @color: the new color
   *
   * The user changed the color: the square, a strip, the hex field or a swatch.
   */
  p_signals[P_EDITED] = g_signal_new ("edited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                      G_TYPE_NONE, 1, GDK_TYPE_RGBA | G_SIGNAL_TYPE_STATIC_SCOPE);
  gtk_widget_class_set_css_name (wc, "omarchy-color-picker");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_color_picker_new:
 *
 * Returns: (transfer floating): a color picker on white
 */
GtkWidget *
omarchy_color_picker_new (void)
{
  return g_object_new (OMARCHY_TYPE_COLOR_PICKER, NULL);
}

double
omarchy_color_picker_get_hue (OmarchyColorPicker *self)
{
  return self->hue;
}

void
omarchy_color_picker_set_hue (OmarchyColorPicker *self, double hue)
{
  picker_set (self, &self->hue, hue, P_PROP_HUE);
}

double
omarchy_color_picker_get_saturation (OmarchyColorPicker *self)
{
  return self->saturation;
}

void
omarchy_color_picker_set_saturation (OmarchyColorPicker *self, double saturation)
{
  picker_set (self, &self->saturation, saturation, P_PROP_SATURATION);
}

double
omarchy_color_picker_get_value (OmarchyColorPicker *self)
{
  return self->value;
}

void
omarchy_color_picker_set_value (OmarchyColorPicker *self, double value)
{
  picker_set (self, &self->value, value, P_PROP_VALUE);
}

double
omarchy_color_picker_get_alpha (OmarchyColorPicker *self)
{
  return self->alpha;
}

void
omarchy_color_picker_set_alpha (OmarchyColorPicker *self, double alpha)
{
  picker_set (self, &self->alpha, alpha, P_PROP_ALPHA);
}

gboolean
omarchy_color_picker_get_show_alpha (OmarchyColorPicker *self)
{
  return self->show_alpha;
}

void
omarchy_color_picker_set_show_alpha (OmarchyColorPicker *self, gboolean show_alpha)
{
  if (self->show_alpha == !!show_alpha)
    return;
  self->show_alpha = !!show_alpha;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), p_props[P_PROP_SHOW_ALPHA]);
}

gboolean
omarchy_color_picker_get_cursor (OmarchyColorPicker *self)
{
  return self->cursor;
}

void
omarchy_color_picker_set_cursor (OmarchyColorPicker *self, gboolean cursor)
{
  if (self->cursor == !!cursor)
    return;
  self->cursor = !!cursor;
  gtk_widget_queue_draw (self->square);
  g_object_notify_by_pspec (G_OBJECT (self), p_props[P_PROP_CURSOR]);
}

/**
 * omarchy_color_picker_set_surface:
 * @self: the picker
 * @surface: `popups` (the default), `background`, `tooltip` or `menu`
 *
 * The surface it sits on: the knobs' rings are drawn in its background (Qt's `surfaceColor`).
 */
void
omarchy_color_picker_set_surface (OmarchyColorPicker *self, const char *surface)
{
  g_free (self->surface);
  self->surface = g_strdup (surface ? surface : "popups");
  picker_redraw_parts (self);
}

/**
 * omarchy_color_picker_get_palette_names:
 * @self: the picker
 *
 * Returns: (transfer none) (array zero-terminated=1): the colors.toml keys the swatches show
 */
const char *const *
omarchy_color_picker_get_palette_names (OmarchyColorPicker *self)
{
  return (const char *const *) self->palette_names;
}

/**
 * omarchy_color_picker_set_palette_names:
 * @self: the picker
 * @names: (array zero-terminated=1) (nullable): colors.toml keys, in order; %NULL for the default
 *   `red orange yellow green cyan blue magenta foreground`
 */
void
omarchy_color_picker_set_palette_names (OmarchyColorPicker *self, const char *const *names)
{
  g_strfreev (self->palette_names);
  self->palette_names = g_strdupv ((char **) (names ? names : default_palette));
  rebuild_swatches (self);
  g_object_notify_by_pspec (G_OBJECT (self), p_props[P_PROP_PALETTE_NAMES]);
}

/**
 * omarchy_color_picker_get_color:
 * @self: the picker
 * @color: (out caller-allocates): the picked color, with its alpha
 */
void
omarchy_color_picker_get_color (OmarchyColorPicker *self, GdkRGBA *color)
{
  int rgb[3];
  picker_rgb (self, rgb);
  *color = rgb_color (rgb, self->alpha);
}

/**
 * omarchy_color_picker_get_hex:
 * @self: the picker
 *
 * Returns: the picked color as `#rrggbb`, or `#rrggbbaa` below full alpha
 */
const char *
omarchy_color_picker_get_hex (OmarchyColorPicker *self)
{
  return self->hex;
}

/**
 * omarchy_color_picker_set_color:
 * @self: the picker
 * @hex: `#rrggbb` or `#rrggbbaa` (the `#` optional)
 *
 * Sets hue, saturation, value and alpha from a hex color. A gray keeps the hue.
 *
 * Returns: %FALSE (and nothing changes) when @hex isn't one
 */
gboolean
omarchy_color_picker_set_color (OmarchyColorPicker *self, const char *hex)
{
  if (!hex)
    return FALSE;
  if (*hex == '#')
    hex++;
  size_t n = strlen (hex);
  if (n != 6 && n != 8)
    return FALSE;
  for (size_t i = 0; i < n; i++)
    if (!g_ascii_isxdigit (hex[i]))
      return FALSE;
  int ch[4] = { 0, 0, 0, 255 };
  for (size_t i = 0; i < n / 2; i++)
    ch[i] = g_ascii_xdigit_value (hex[i * 2]) * 16 + g_ascii_xdigit_value (hex[i * 2 + 1]);
  double r = ch[0] / 255.0, g = ch[1] / 255.0, b = ch[2] / 255.0;
  double max = MAX (r, MAX (g, b)), min = MIN (r, MIN (g, b)), d = max - min;
  double h = self->hue;
  if (d > 0) {
    if (max == r)
      h = ((g - b) / d) / 6;
    else if (max == g)
      h = ((b - r) / d + 2) / 6;
    else
      h = ((r - g) / d + 4) / 6;
    if (h < 0)
      h += 1;
  }
  g_object_freeze_notify (G_OBJECT (self));
  gboolean changed = FALSE;
  double vals[4] = { h, max > 0 ? d / max : 0, max, ch[3] / 255.0 };
  double *fields[4] = { &self->hue, &self->saturation, &self->value, &self->alpha };
  int props[4] = { P_PROP_HUE, P_PROP_SATURATION, P_PROP_VALUE, P_PROP_ALPHA };
  for (int i = 0; i < 4; i++)
    if (*fields[i] != vals[i]) {
      *fields[i] = vals[i];
      g_object_notify_by_pspec (G_OBJECT (self), p_props[props[i]]);
      changed = TRUE;
    }
  if (changed)
    picker_changed (self);
  g_object_thaw_notify (G_OBJECT (self));
  return TRUE;
}

/**
 * omarchy_color_picker_set_rgba:
 * @self: the picker
 * @color: a color
 *
 * Sets hue, saturation, value and alpha from @color (8-bit channels, as a hex would).
 */
void
omarchy_color_picker_set_rgba (OmarchyColorPicker *self, const GdkRGBA *color)
{
  g_autofree char *hex = g_strdup_printf ("#%02x%02x%02x%02x", (int) lround (CLAMP (color->red, 0, 1) * 255),
                                          (int) lround (CLAMP (color->green, 0, 1) * 255),
                                          (int) lround (CLAMP (color->blue, 0, 1) * 255),
                                          (int) lround (CLAMP (color->alpha, 0, 1) * 255));
  omarchy_color_picker_set_color (self, hex);
}

/**
 * omarchy_color_picker_get_n_swatches:
 * @self: the picker
 *
 * Returns: how many palette swatches show (the names the theme has)
 */
int
omarchy_color_picker_get_n_swatches (OmarchyColorPicker *self)
{
  return self->swatches->len;
}
