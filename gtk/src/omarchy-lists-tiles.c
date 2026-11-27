/* omarchy-lists-tiles.c: Badge (Badge.qml), StatusDot (StatusDot.qml), Avatar (Avatar.qml) and
 * TileGrid (TileGrid.qml), drawn as the Qt kit draws them. */
#include "omarchy-lists-private.h"

#include <math.h>
#include <string.h>

/* A ground: "" for the popups background, "background", or any role (omarchy_role_color). */
static GdkRGBA
ground_color (const char *ground)
{
  const OmarchyTokens *p = omarchy_paint ();
  if (!ground || !*ground || !strcmp (ground, "popups"))
    return p->popups.background;
  if (!strcmp (ground, "background"))
    return p->background;
  return omarchy_role_color (ground);
}

static void
draw_dot (GtkSnapshot *s, float x, float y, int size, int ring, const char *tone, const GdkRGBA *ground)
{
  const OmarchyTokens *p = omarchy_paint ();
  float full = size + ring * 2;
  if (ring > 0)
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x, y, full, full), full / 2, ground);
  GdkRGBA c = omarchy_lists_tone (tone, &p->foreground);
  omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x + ring, y + ring, size, size), size / 2.0f, &c);
}

static int
dot_ring (void)
{
  return MAX (1, omarchy_space (2));
}

/* ================================================================================= Badge === */

/**
 * OmarchyBadge:
 *
 * A count or short label in a small pill. Neutral: foreground glass (the normal control fill
 * and border). Toned: the tone at the selected fill's alpha, text in the tone. Filled: the tone
 * solid, text in the background. Text `caption` bold; padding `space(6)` across and `space(1)`
 * down, each plus the normal border width; never narrower than tall; corners `min(4, radius)`.
 */
struct _OmarchyBadge {
  GtkWidget parent_instance;
  char *text, *tone;
  gboolean filled;
};

enum { B_PROP_0, B_PROP_TEXT, B_PROP_TONE, B_PROP_FILLED, B_N_PROPS };
static GParamSpec *b_props[B_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyBadge, omarchy_badge, GTK_TYPE_WIDGET)

static int
badge_line (void)
{
  return (int) omarchy_tokens ()->normal.border.widths[0];
}

static void
badge_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyBadge *self = OMARCHY_BADGE (w);
  const OmarchyTokens *t = omarchy_tokens ();
  int line = badge_line ();
  int h = omarchy_line_height (t->caption) + (omarchy_space (1) + line) * 2;
  if (o == GTK_ORIENTATION_VERTICAL) {
    *min = *nat = h;
    return;
  }
  g_autoptr (PangoLayout) l = omarchy_layout (w, self->text, t->caption, TRUE);
  int tw;
  omarchy_layout_size (l, &tw, NULL);
  *min = *nat = MAX (h, tw + (omarchy_space (6) + line) * 2);
}

static void
badge_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyBadge *self = OMARCHY_BADGE (w);
  const OmarchyTokens *p = omarchy_paint ();
  int W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gboolean neutral = !omarchy_lists_toned (self->tone) && !self->filled;
  GdkRGBA tone = omarchy_lists_tone (self->tone, &p->foreground);
  GdkRGBA fill = self->filled ? tone : (neutral ? p->normal.fill : omarchy_rgba_alpha (&tone, p->selected.fill.alpha));
  OmarchyBorder none = omarchy_border_none ();
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), MIN (4, p->radius), &fill, neutral ? &p->normal.border : &none);
  g_autoptr (PangoLayout) l = omarchy_layout (w, self->text, p->caption, TRUE);
  int tw;
  omarchy_layout_size (l, &tw, NULL);
  int th = omarchy_line_height (p->caption);
  omarchy_draw_layout (s, l, omarchy_lists_center (W, tw), omarchy_lists_center (H, th), self->filled ? &p->background : &tone);
}

static void
omarchy_badge_init (OmarchyBadge *self)
{
  self->text = g_strdup ("");
  self->tone = g_strdup ("");
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
badge_finalize (GObject *o)
{
  OmarchyBadge *self = OMARCHY_BADGE (o);
  g_free (self->text);
  g_free (self->tone);
  G_OBJECT_CLASS (omarchy_badge_parent_class)->finalize (o);
}

static void
badge_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyBadge *self = OMARCHY_BADGE (o);
  switch (id) {
  case B_PROP_TEXT: omarchy_badge_set_text (self, g_value_get_string (v)); break;
  case B_PROP_TONE: omarchy_badge_set_tone (self, g_value_get_string (v)); break;
  case B_PROP_FILLED: omarchy_badge_set_filled (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
badge_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyBadge *self = OMARCHY_BADGE (o);
  switch (id) {
  case B_PROP_TEXT: g_value_set_string (v, self->text); break;
  case B_PROP_TONE: g_value_set_string (v, self->tone); break;
  case B_PROP_FILLED: g_value_set_boolean (v, self->filled); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_badge_class_init (OmarchyBadgeClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = badge_finalize;
  oc->set_property = badge_set_property;
  oc->get_property = badge_get_property;
  wc->measure = badge_measure;
  wc->snapshot = badge_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyBadge:text:
   *
   * The count or label.
   */
  b_props[B_PROP_TEXT] = g_param_spec_string ("text", NULL, NULL, "", f);
  /**
   * OmarchyBadge:tone:
   *
   * `""` (neutral glass), `accent`, `success`, `warning`, `urgent` or `muted`.
   */
  b_props[B_PROP_TONE] = g_param_spec_string ("tone", NULL, NULL, "", f);
  /**
   * OmarchyBadge:filled:
   *
   * The tone solid, the text in the background.
   */
  b_props[B_PROP_FILLED] = g_param_spec_boolean ("filled", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, B_N_PROPS, b_props);
  gtk_widget_class_set_css_name (wc, "omarchy-badge");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_STATUS);
}

/**
 * omarchy_badge_new:
 * @text: (nullable): the count or label
 *
 * Returns: (transfer floating): a badge
 */
GtkWidget *
omarchy_badge_new (const char *text)
{
  return g_object_new (OMARCHY_TYPE_BADGE, "text", text ? text : "", NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyBadge, badge, text, b_props, B_PROP_TEXT, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyBadge, badge, tone, b_props, B_PROP_TONE, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyBadge, badge, filled, b_props, B_PROP_FILLED, FALSE)

/* ============================================================================= StatusDot === */

/**
 * OmarchyStatusDot:
 *
 * A dot (`space(8)`) in a tone, ringed (`max(1, space(2))`) in the surface it sits on, so it
 * reads as separate from it. Always a circle.
 */
struct _OmarchyStatusDot {
  GtkWidget parent_instance;
  char *tone, *ground;
  int size, ring; /* -1: the token default */
  gboolean has_ground_color;
  GdkRGBA ground_color;
};

enum { D_PROP_0, D_PROP_TONE, D_PROP_SIZE, D_PROP_RING, D_PROP_GROUND, D_N_PROPS };
static GParamSpec *d_props[D_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyStatusDot, omarchy_status_dot, GTK_TYPE_WIDGET)

static int
dot_size (OmarchyStatusDot *self)
{
  return self->size >= 0 ? self->size : omarchy_space (8);
}

static int
dot_ring_of (OmarchyStatusDot *self)
{
  return self->ring >= 0 ? self->ring : dot_ring ();
}

static void
dot_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyStatusDot *self = OMARCHY_STATUS_DOT (w);
  *min = *nat = dot_size (self) + dot_ring_of (self) * 2;
}

static void
dot_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyStatusDot *self = OMARCHY_STATUS_DOT (w);
  GdkRGBA g = self->has_ground_color ? self->ground_color : ground_color (self->ground);
  draw_dot (s, 0, 0, dot_size (self), dot_ring_of (self), self->tone, &g);
}

static void
omarchy_status_dot_init (OmarchyStatusDot *self)
{
  self->tone = g_strdup ("success");
  self->ground = g_strdup ("");
  self->size = self->ring = -1;
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
dot_finalize (GObject *o)
{
  OmarchyStatusDot *self = OMARCHY_STATUS_DOT (o);
  g_free (self->tone);
  g_free (self->ground);
  G_OBJECT_CLASS (omarchy_status_dot_parent_class)->finalize (o);
}

static void
dot_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyStatusDot *self = OMARCHY_STATUS_DOT (o);
  switch (id) {
  case D_PROP_TONE: omarchy_status_dot_set_tone (self, g_value_get_string (v)); break;
  case D_PROP_SIZE: omarchy_status_dot_set_size (self, g_value_get_int (v)); break;
  case D_PROP_RING: omarchy_status_dot_set_ring (self, g_value_get_int (v)); break;
  case D_PROP_GROUND: omarchy_status_dot_set_ground (self, g_value_get_string (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
dot_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyStatusDot *self = OMARCHY_STATUS_DOT (o);
  switch (id) {
  case D_PROP_TONE: g_value_set_string (v, self->tone); break;
  case D_PROP_SIZE: g_value_set_int (v, self->size); break;
  case D_PROP_RING: g_value_set_int (v, self->ring); break;
  case D_PROP_GROUND: g_value_set_string (v, self->ground); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_status_dot_class_init (OmarchyStatusDotClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = dot_finalize;
  oc->set_property = dot_set_property;
  oc->get_property = dot_get_property;
  wc->measure = dot_measure;
  wc->snapshot = dot_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyStatusDot:tone:
   *
   * `success`, `warning`, `urgent`, `muted`, `accent`, or anything else for the foreground.
   */
  d_props[D_PROP_TONE] = g_param_spec_string ("tone", NULL, NULL, "success", f);
  /**
   * OmarchyStatusDot:size:
   *
   * The dot's diameter without the ring, or -1 for `space(8)`.
   */
  d_props[D_PROP_SIZE] = g_param_spec_int ("size", NULL, NULL, -1, 1000, -1, f);
  /**
   * OmarchyStatusDot:ring:
   *
   * The ring's width, or -1 for `max(1, space(2))`.
   */
  d_props[D_PROP_RING] = g_param_spec_int ("ring", NULL, NULL, -1, 1000, -1, f);
  /**
   * OmarchyStatusDot:ground:
   *
   * The surface the dot sits on: `""` for the popups background, `background`, or a role.
   */
  d_props[D_PROP_GROUND] = g_param_spec_string ("ground", NULL, NULL, "", f);
  g_object_class_install_properties (oc, D_N_PROPS, d_props);
  gtk_widget_class_set_css_name (wc, "omarchy-status-dot");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_IMG);
}

/**
 * omarchy_status_dot_new:
 * @tone: (nullable): the tone, or %NULL for `success`
 *
 * Returns: (transfer floating): a status dot
 */
GtkWidget *
omarchy_status_dot_new (const char *tone)
{
  return g_object_new (OMARCHY_TYPE_STATUS_DOT, "tone", tone ? tone : "success", NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyStatusDot, status_dot, tone, d_props, D_PROP_TONE, FALSE)
OMARCHY_STRING_ACCESSORS (OmarchyStatusDot, status_dot, ground, d_props, D_PROP_GROUND, FALSE)

int
omarchy_status_dot_get_size (OmarchyStatusDot *self)
{
  return self->size;
}

void
omarchy_status_dot_set_size (OmarchyStatusDot *self, int size)
{
  if (self->size == size)
    return;
  self->size = MAX (-1, size);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), d_props[D_PROP_SIZE]);
}

int
omarchy_status_dot_get_ring (OmarchyStatusDot *self)
{
  return self->ring;
}

void
omarchy_status_dot_set_ring (OmarchyStatusDot *self, int ring)
{
  if (self->ring == ring)
    return;
  self->ring = MAX (-1, ring);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), d_props[D_PROP_RING]);
}

/**
 * omarchy_status_dot_set_ground_color:
 * @self: the dot
 * @color: (nullable): the ring's color, or %NULL to follow #OmarchyStatusDot:ground
 */
void
omarchy_status_dot_set_ground_color (OmarchyStatusDot *self, const GdkRGBA *color)
{
  self->has_ground_color = color != NULL;
  if (color)
    self->ground_color = *color;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/* ================================================================================ Avatar === */

/**
 * OmarchyAvatar:
 *
 * A person's or an app's picture with fallbacks, tried in order: #OmarchyAvatar:source (an
 * image file), #OmarchyAvatar:icon-name (looked up in the Omarchy theme's icon theme by
 * omarchy_theme_lookup_icon()), #OmarchyAvatar:glyph, then the first letter of
 * #OmarchyAvatar:name; the last two on a `controls.selected.fill` tile. A source that fails to
 * load gives way to the next.
 *
 * Sizes: `small` (`iconLarge`, fills a #OmarchyListRow's leading slot), `medium`
 * (`displayLarge`) or `large` (`display` × 2), or #OmarchyAvatar:pixel-size. Corners
 * `min(radius, size / 2)`; #OmarchyAvatar:round makes a disc. A picture covers the square,
 * clipped to the corners; an icon is drawn whole, fitted, rasterized at the device size.
 * #OmarchyAvatar:status docks a #OmarchyStatusDot on the bottom-right corner, ringed in
 * #OmarchyAvatar:ground.
 */
struct _OmarchyAvatar {
  GtkWidget parent_instance;
  char *name, *source, *icon_name, *glyph, *size, *status, *ground;
  int pixel_size; /* 0: from size */
  gboolean round;
  GdkTexture *picture; /* NULL: none or failed */
  gboolean picture_loaded;
  GdkPaintable *icon;
  char *icon_key; /* name|size|icon theme the icon was resolved for */
};

enum {
  A_PROP_0,
  A_PROP_NAME,
  A_PROP_SOURCE,
  A_PROP_ICON_NAME,
  A_PROP_GLYPH,
  A_PROP_SIZE,
  A_PROP_PIXEL_SIZE,
  A_PROP_ROUND,
  A_PROP_STATUS,
  A_PROP_GROUND,
  A_N_PROPS
};
static GParamSpec *a_props[A_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyAvatar, omarchy_avatar, GTK_TYPE_WIDGET)

static int
avatar_px (OmarchyAvatar *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  if (self->pixel_size > 0)
    return self->pixel_size;
  if (!g_strcmp0 (self->size, "small"))
    return t->icon_large;
  if (!g_strcmp0 (self->size, "large"))
    return t->display * 2;
  return t->display_large;
}

static GFile *
file_for (const char *source)
{
  if (!source || !*source)
    return NULL;
  if (g_path_is_absolute (source))
    return g_file_new_for_path (source);
  g_autofree char *scheme = g_uri_parse_scheme (source);
  if (scheme)
    return g_file_new_for_uri (source);
  return g_file_new_for_path (source);
}

static GdkTexture *
avatar_picture (OmarchyAvatar *self)
{
  if (!self->picture_loaded) {
    self->picture_loaded = TRUE;
    g_autoptr (GFile) f = file_for (self->source);
    if (f)
      self->picture = gdk_texture_new_from_file (f, NULL);
  }
  return self->picture;
}

static int
avatar_scale (OmarchyAvatar *self)
{
  return MAX (1, gtk_widget_get_scale_factor (GTK_WIDGET (self)));
}

static GdkPaintable *
avatar_icon (OmarchyAvatar *self)
{
  if (!self->icon_name[0])
    return NULL;
  OmarchyTheme *theme = omarchy_theme_get_default ();
  int px = avatar_px (self), scale = avatar_scale (self);
  int pixels = px * scale;
  g_autofree char *key = g_strdup_printf ("%s|%d|%d|%s", self->icon_name, px, scale, omarchy_theme_get_icon_theme (theme));
  if (self->icon_key && !strcmp (self->icon_key, key))
    return self->icon;
  g_free (self->icon_key);
  self->icon_key = g_steal_pointer (&key);
  g_clear_object (&self->icon);
  g_autofree char *path = omarchy_theme_lookup_icon (theme, self->icon_name, pixels);
  if (path) {
    g_autoptr (GFile) f = g_file_new_for_path (path);
    self->icon = GDK_PAINTABLE (gtk_icon_paintable_new_for_file (f, px, scale));
  }
  return self->icon;
}

/**
 * omarchy_avatar_get_pictured:
 * @self: the avatar
 *
 * Returns: whether it shows #OmarchyAvatar:source
 */
gboolean
omarchy_avatar_get_pictured (OmarchyAvatar *self)
{
  return self->source[0] && avatar_picture (self) != NULL;
}

/**
 * omarchy_avatar_get_iconed:
 * @self: the avatar
 *
 * Returns: whether it shows the icon #OmarchyAvatar:icon-name names
 */
gboolean
omarchy_avatar_get_iconed (OmarchyAvatar *self)
{
  return !omarchy_avatar_get_pictured (self) && avatar_icon (self) != NULL;
}

/**
 * omarchy_avatar_initial:
 * @name: (nullable): a name
 *
 * Returns: (transfer full): its first letter, uppercased (`""` for none)
 */
char *
omarchy_avatar_initial (const char *name)
{
  if (!name)
    return g_strdup ("");
  g_autofree char *trimmed = g_strstrip (g_strdup (name));
  if (!*trimmed)
    return g_strdup ("");
  const char *next = g_utf8_next_char (trimmed);
  g_autofree char *first = g_strndup (trimmed, next - trimmed);
  return g_utf8_strup (first, -1);
}

static void
avatar_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  *min = *nat = avatar_px (OMARCHY_AVATAR (w));
}

static void
avatar_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyAvatar *self = OMARCHY_AVATAR (w);
  const OmarchyTokens *p = omarchy_paint ();
  int px = avatar_px (self);
  float corner = self->round ? px / 2.0f : MIN ((float) p->radius, px / 2.0f);
  graphene_rect_t box = GRAPHENE_RECT_INIT (0, 0, px, px);
  GdkTexture *pic = self->source[0] ? avatar_picture (self) : NULL;
  GdkPaintable *icon = pic ? NULL : avatar_icon (self);
  if (pic) {
    /* Cover the square, cropping the long side, clipped to the corners. */
    float iw = gdk_texture_get_width (pic), ih = gdk_texture_get_height (pic);
    float k = MAX (px / iw, px / ih);
    float dw = iw * k, dh = ih * k;
    GskRoundedRect rr;
    gsk_rounded_rect_init_from_rect (&rr, &box, corner);
    gtk_snapshot_push_rounded_clip (s, &rr);
    GskScalingFilter filter = k < 1 ? GSK_SCALING_FILTER_TRILINEAR : GSK_SCALING_FILTER_LINEAR;
    gtk_snapshot_append_scaled_texture (s, pic, filter, &GRAPHENE_RECT_INIT ((px - dw) / 2, (px - dh) / 2, dw, dh));
    gtk_snapshot_pop (s);
  } else if (icon) {
    /* Whole, fitted into the square, its aspect kept; no tile, no clipping. */
    double cw, ch;
    gdk_paintable_compute_concrete_size (icon, 0, 0, px, px, &cw, &ch);
    double k = MIN (px / MAX (1.0, cw), px / MAX (1.0, ch));
    cw *= k;
    ch *= k;
    gtk_snapshot_save (s);
    gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT ((px - cw) / 2, (px - ch) / 2));
    gdk_paintable_snapshot (icon, s, cw, ch);
    gtk_snapshot_restore (s);
  } else {
    omarchy_draw_fill (s, &box, corner, &p->selected.fill);
    int mpx = (int) lround (px * 0.5);
    if (self->glyph[0]) {
      omarchy_draw_optical_glyph (w, s, self->glyph, mpx, 0, 0, px, px, &p->foreground);
    } else {
      g_autofree char *initial = omarchy_avatar_initial (self->name);
      if (*initial) {
        g_autoptr (PangoLayout) l = omarchy_layout (w, initial, mpx, TRUE);
        int mw;
        omarchy_layout_size (l, &mw, NULL);
        int mh = omarchy_line_height (mpx);
        omarchy_draw_layout (s, l, omarchy_lists_center (px, mw), omarchy_lists_center (px, mh), &p->foreground);
      }
    }
  }
  if (self->status[0]) {
    int ring = dot_ring ();
    int size = MAX (omarchy_space (6), (int) lround (px * 0.28));
    int full = size + ring * 2;
    GdkRGBA g = ground_color (self->ground);
    draw_dot (s, px - full + ring, px - full + ring, size, ring, self->status, &g);
  }
}

static void
avatar_theme_changed (OmarchyAvatar *self)
{
  /* Another icon theme or size re-resolves on the next draw (the key changes). */
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
avatar_scale_changed (OmarchyAvatar *self)
{
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
omarchy_avatar_init (OmarchyAvatar *self)
{
  self->name = g_strdup ("");
  self->source = g_strdup ("");
  self->icon_name = g_strdup ("");
  self->glyph = g_strdup ("");
  self->size = g_strdup ("medium");
  self->status = g_strdup ("");
  self->ground = g_strdup ("");
  omarchy_widget_follow (GTK_WIDGET (self));
  g_signal_connect_object (omarchy_theme_get_default (), "changed", G_CALLBACK (avatar_theme_changed), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect (self, "notify::scale-factor", G_CALLBACK (avatar_scale_changed), NULL);
}

static void
avatar_dispose (GObject *o)
{
  OmarchyAvatar *self = OMARCHY_AVATAR (o);
  g_clear_object (&self->picture);
  g_clear_object (&self->icon);
  G_OBJECT_CLASS (omarchy_avatar_parent_class)->dispose (o);
}

static void
avatar_finalize (GObject *o)
{
  OmarchyAvatar *self = OMARCHY_AVATAR (o);
  g_free (self->name);
  g_free (self->source);
  g_free (self->icon_name);
  g_free (self->glyph);
  g_free (self->size);
  g_free (self->status);
  g_free (self->ground);
  g_free (self->icon_key);
  G_OBJECT_CLASS (omarchy_avatar_parent_class)->finalize (o);
}

static void
avatar_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyAvatar *self = OMARCHY_AVATAR (o);
  switch (id) {
  case A_PROP_NAME: omarchy_avatar_set_name (self, g_value_get_string (v)); break;
  case A_PROP_SOURCE: omarchy_avatar_set_source (self, g_value_get_string (v)); break;
  case A_PROP_ICON_NAME: omarchy_avatar_set_icon_name (self, g_value_get_string (v)); break;
  case A_PROP_GLYPH: omarchy_avatar_set_glyph (self, g_value_get_string (v)); break;
  case A_PROP_SIZE: omarchy_avatar_set_size (self, g_value_get_string (v)); break;
  case A_PROP_PIXEL_SIZE: omarchy_avatar_set_pixel_size (self, g_value_get_int (v)); break;
  case A_PROP_ROUND: omarchy_avatar_set_round (self, g_value_get_boolean (v)); break;
  case A_PROP_STATUS: omarchy_avatar_set_status (self, g_value_get_string (v)); break;
  case A_PROP_GROUND: omarchy_avatar_set_ground (self, g_value_get_string (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
avatar_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyAvatar *self = OMARCHY_AVATAR (o);
  switch (id) {
  case A_PROP_NAME: g_value_set_string (v, self->name); break;
  case A_PROP_SOURCE: g_value_set_string (v, self->source); break;
  case A_PROP_ICON_NAME: g_value_set_string (v, self->icon_name); break;
  case A_PROP_GLYPH: g_value_set_string (v, self->glyph); break;
  case A_PROP_SIZE: g_value_set_string (v, self->size); break;
  case A_PROP_PIXEL_SIZE: g_value_set_int (v, self->pixel_size); break;
  case A_PROP_ROUND: g_value_set_boolean (v, self->round); break;
  case A_PROP_STATUS: g_value_set_string (v, self->status); break;
  case A_PROP_GROUND: g_value_set_string (v, self->ground); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_avatar_class_init (OmarchyAvatarClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = avatar_dispose;
  oc->finalize = avatar_finalize;
  oc->set_property = avatar_set_property;
  oc->get_property = avatar_get_property;
  wc->measure = avatar_measure;
  wc->snapshot = avatar_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyAvatar:name:
   *
   * Whose or what's picture; its first letter is the last fallback.
   */
  a_props[A_PROP_NAME] = g_param_spec_string ("name", NULL, NULL, "", f);
  /**
   * OmarchyAvatar:source:
   *
   * An image file (a path or a URI).
   */
  a_props[A_PROP_SOURCE] = g_param_spec_string ("source", NULL, NULL, "", f);
  /**
   * OmarchyAvatar:icon-name:
   *
   * A freedesktop icon name (`firefox`, `org.gnome.Nautilus`) or an absolute path.
   */
  a_props[A_PROP_ICON_NAME] = g_param_spec_string ("icon-name", NULL, NULL, "", f);
  /**
   * OmarchyAvatar:glyph:
   *
   * A Nerd Font glyph on the tile, before the initial.
   */
  a_props[A_PROP_GLYPH] = g_param_spec_string ("glyph", NULL, NULL, "", f);
  /**
   * OmarchyAvatar:size:
   *
   * `small`, `medium` or `large`.
   */
  a_props[A_PROP_SIZE] = g_param_spec_string ("size", NULL, NULL, "medium", f);
  /**
   * OmarchyAvatar:pixel-size:
   *
   * The side in points, or 0 to follow #OmarchyAvatar:size.
   */
  a_props[A_PROP_PIXEL_SIZE] = g_param_spec_int ("pixel-size", NULL, NULL, 0, 4096, 0, f);
  /**
   * OmarchyAvatar:round:
   *
   * A disc, for people.
   */
  a_props[A_PROP_ROUND] = g_param_spec_boolean ("round", NULL, NULL, FALSE, f);
  /**
   * OmarchyAvatar:status:
   *
   * A tone docks a #OmarchyStatusDot on the bottom-right corner; `""` none.
   */
  a_props[A_PROP_STATUS] = g_param_spec_string ("status", NULL, NULL, "", f);
  /**
   * OmarchyAvatar:ground:
   *
   * The surface under the avatar, for the dot's ring: `""` for the popups background,
   * `background`, or a role.
   */
  a_props[A_PROP_GROUND] = g_param_spec_string ("ground", NULL, NULL, "", f);
  g_object_class_install_properties (oc, A_N_PROPS, a_props);
  gtk_widget_class_set_css_name (wc, "omarchy-avatar");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_IMG);
}

/**
 * omarchy_avatar_new:
 * @name: (nullable): the name
 *
 * Returns: (transfer floating): an avatar
 */
GtkWidget *
omarchy_avatar_new (const char *name)
{
  return g_object_new (OMARCHY_TYPE_AVATAR, "name", name ? name : "", NULL);
}

const char *
omarchy_avatar_get_name (OmarchyAvatar *self)
{
  return self->name;
}

void
omarchy_avatar_set_name (OmarchyAvatar *self, const char *name)
{
  if (!g_strcmp0 (self->name, name))
    return;
  g_free (self->name);
  self->name = g_strdup (name ? name : "");
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_LABEL, self->name, -1);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), a_props[A_PROP_NAME]);
}

const char *
omarchy_avatar_get_source (OmarchyAvatar *self)
{
  return self->source;
}

void
omarchy_avatar_set_source (OmarchyAvatar *self, const char *source)
{
  if (!g_strcmp0 (self->source, source))
    return;
  g_free (self->source);
  self->source = g_strdup (source ? source : "");
  g_clear_object (&self->picture);
  self->picture_loaded = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), a_props[A_PROP_SOURCE]);
}

const char *
omarchy_avatar_get_icon_name (OmarchyAvatar *self)
{
  return self->icon_name;
}

void
omarchy_avatar_set_icon_name (OmarchyAvatar *self, const char *icon_name)
{
  if (!g_strcmp0 (self->icon_name, icon_name))
    return;
  g_free (self->icon_name);
  self->icon_name = g_strdup (icon_name ? icon_name : "");
  g_clear_pointer (&self->icon_key, g_free);
  g_clear_object (&self->icon);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), a_props[A_PROP_ICON_NAME]);
}

OMARCHY_STRING_ACCESSORS (OmarchyAvatar, avatar, glyph, a_props, A_PROP_GLYPH, FALSE)
OMARCHY_STRING_ACCESSORS (OmarchyAvatar, avatar, size, a_props, A_PROP_SIZE, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyAvatar, avatar, status, a_props, A_PROP_STATUS, FALSE)
OMARCHY_STRING_ACCESSORS (OmarchyAvatar, avatar, ground, a_props, A_PROP_GROUND, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyAvatar, avatar, round, a_props, A_PROP_ROUND, FALSE)

int
omarchy_avatar_get_pixel_size (OmarchyAvatar *self)
{
  return self->pixel_size;
}

void
omarchy_avatar_set_pixel_size (OmarchyAvatar *self, int pixel_size)
{
  if (self->pixel_size == pixel_size)
    return;
  self->pixel_size = MAX (0, pixel_size);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), a_props[A_PROP_PIXEL_SIZE]);
}

/* ============================================================================== TileGrid === */

/**
 * OmarchyTileGrid:
 *
 * A grid of tiles, each a glyph (`display`) or a medium #OmarchyAvatar above a label of up to
 * two lines (`bodySmall`, centered, elided): an app grid or an emoji picker. Tiles are
 * `space(88)` wide with no gap; padding `md`; every tile reserves two label lines. The cursor
 * tile takes the hover fill and border, the current one the selected fill.
 *
 * It keeps its own #OmarchyCursorModel (omarchy_tile_grid_get_cursor_model()) with
 * #OmarchyCursorModel:columns set: the arrows and h/j/k/l move the cursor while it has focus,
 * the pointer moves it, and Return/Space or a click emit ::activated.
 */
typedef struct {
  char *label, *glyph;
  GtkWidget *avatar;
} Tile;

struct _OmarchyTileGrid {
  GtkWidget parent_instance;
  GArray *tiles; /* Tile */
  int columns, cell_width; /* cell_width -1: space(88) */
  int cursor_index, current_index;
  OmarchyCursorModel *model;
};

enum { G_PROP_0, G_PROP_COLUMNS, G_PROP_CELL_WIDTH, G_PROP_CURSOR_INDEX, G_PROP_CURRENT_INDEX, G_N_PROPS };
static GParamSpec *g_props[G_N_PROPS];
enum { G_ACTIVATED, G_POINTER_MOVED, G_N_SIGNALS };
static guint g_signals[G_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyTileGrid, omarchy_tile_grid, GTK_TYPE_WIDGET)

typedef struct {
  int cell_w, cell_h, pad, icon_h, gap, label_h, cols, rows;
} GridGeom;

static void
grid_geom (OmarchyTileGrid *self, GridGeom *g)
{
  const OmarchyTokens *t = omarchy_tokens ();
  g->cell_w = self->cell_width >= 0 ? self->cell_width : omarchy_space (88);
  g->pad = t->md;
  g->icon_h = MAX (omarchy_line_height (t->display), t->display_large);
  g->gap = t->md;
  g->label_h = omarchy_line_height (t->body_small) * 2;
  g->cell_h = g->pad * 2 + g->icon_h + g->gap + g->label_h;
  int n = self->tiles->len;
  g->cols = MAX (1, MIN (self->columns, MAX (1, n)));
  g->rows = n ? (n + self->columns - 1) / self->columns : 0;
}

static graphene_rect_t
grid_cell (OmarchyTileGrid *self, const GridGeom *g, int i)
{
  int col = i % self->columns, row = i / self->columns;
  float x = col * g->cell_w;
  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    x = g->cols * g->cell_w - x - g->cell_w;
  return GRAPHENE_RECT_INIT (x, row * g->cell_h, g->cell_w, g->cell_h);
}

static int
grid_index_at (OmarchyTileGrid *self, double x, double y)
{
  GridGeom g;
  grid_geom (self, &g);
  for (guint i = 0; i < self->tiles->len; i++) {
    graphene_rect_t r = grid_cell (self, &g, i);
    if (graphene_rect_contains_point (&r, &GRAPHENE_POINT_INIT (x, y)))
      return i;
  }
  return -1;
}

static void
grid_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  GridGeom g;
  grid_geom (OMARCHY_TILE_GRID (w), &g);
  *min = *nat = o == GTK_ORIENTATION_HORIZONTAL ? (OMARCHY_TILE_GRID (w)->tiles->len ? g.cols * g.cell_w : 0) : g.rows * g.cell_h;
}

/* Build a tile's label layout and return the y offset that vertically centers (iconBox + md + label)
 * inside `cell_h`. */
static float
tile_content_y (OmarchyTileGrid *self, const GridGeom *g, const Tile *tile, PangoLayout **layout_out)
{
  const OmarchyTokens *t = omarchy_tokens ();
  int lw = MAX (0, g->cell_w - g->pad * 2);
  PangoLayout *l = omarchy_layout (GTK_WIDGET (self), tile->label, t->body_small, FALSE);
  pango_layout_set_width (l, lw * PANGO_SCALE);
  pango_layout_set_wrap (l, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_height (l, -2);
  pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
  pango_layout_set_alignment (l, PANGO_ALIGN_CENTER);
  int lines = pango_layout_get_line_count (l);
  int lh;
  if (lines <= 1) {
    lh = omarchy_line_height (t->body_small);
  } else {
    PangoRectangle logical;
    pango_layout_get_extents (l, NULL, &logical);
    lh = (int) ceil (logical.height / (double) PANGO_SCALE);
  }
  int content_h = g->icon_h + g->gap + lh;
  if (layout_out)
    *layout_out = l;
  else
    g_object_unref (l);
  return roundf ((g->cell_h - content_h) / 2.0f);
}

static void
grid_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyTileGrid *self = OMARCHY_TILE_GRID (w);
  GridGeom g;
  grid_geom (self, &g);
  for (guint i = 0; i < self->tiles->len; i++) {
    Tile *tile = &g_array_index (self->tiles, Tile, i);
    if (!tile->avatar)
      continue;
    graphene_rect_t r = grid_cell (self, &g, i);
    float cy = tile_content_y (self, &g, tile, NULL);
    int aw, ah;
    gtk_widget_measure (tile->avatar, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &aw, NULL, NULL);
    gtk_widget_measure (tile->avatar, GTK_ORIENTATION_VERTICAL, -1, NULL, &ah, NULL, NULL);
    GtkAllocation a = { (int) (r.origin.x + omarchy_lists_center (g.cell_w, aw)),
                        (int) (r.origin.y + cy + omarchy_lists_center (g.icon_h, ah)), aw, ah };
    gtk_widget_size_allocate (tile->avatar, &a, -1);
  }
}

static void
grid_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyTileGrid *self = OMARCHY_TILE_GRID (w);
  const OmarchyTokens *p = omarchy_paint ();
  GridGeom g;
  grid_geom (self, &g);
  for (guint i = 0; i < self->tiles->len; i++) {
    Tile *tile = &g_array_index (self->tiles, Tile, i);
    graphene_rect_t r = grid_cell (self, &g, i);
    if (self->cursor_index == (int) i)
      omarchy_draw_surface (s, &r, p->radius, &p->hover.fill, &p->hover.border);
    else if (self->current_index == (int) i)
      omarchy_draw_surface (s, &r, p->radius, &p->selected.fill, &p->selected.border);
    g_autoptr (PangoLayout) l = NULL;
    float cy = tile_content_y (self, &g, tile, &l);
    if (!tile->avatar)
      omarchy_draw_optical_glyph (w, s, tile->glyph, p->display, r.origin.x + g.pad, r.origin.y + cy,
                                  MAX (0, g.cell_w - g.pad * 2), g.icon_h, &p->foreground);
    else
      gtk_widget_snapshot_child (w, tile->avatar, s);
    omarchy_draw_layout (s, l, r.origin.x + g.pad, r.origin.y + cy + g.icon_h + g.gap, &p->foreground);
  }
}

static void
grid_sync_model (OmarchyTileGrid *self)
{
  omarchy_cursor_model_set_count (self->model, "", self->tiles->len);
  omarchy_cursor_model_set_columns (self->model, self->columns);
}

static void
grid_model_changed (OmarchyTileGrid *self)
{
  int index = omarchy_cursor_model_get_cursor_active (self->model) ? omarchy_cursor_model_get_selected_index (self->model)
                                                                   : -1;
  if (index >= (int) self->tiles->len)
    index = -1;
  if (self->cursor_index == index)
    return;
  self->cursor_index = index;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), g_props[G_PROP_CURSOR_INDEX]);
}

static void
grid_model_activated (OmarchyTileGrid *self, const char *section, int index)
{
  if (index >= 0 && index < (int) self->tiles->len)
    g_signal_emit (self, g_signals[G_ACTIVATED], 0, index);
}

static void
grid_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyTileGrid *self = OMARCHY_TILE_GRID (w);
  int i = grid_index_at (self, x, y);
  if (button == GDK_BUTTON_PRIMARY && i >= 0)
    g_signal_emit (self, g_signals[G_ACTIVATED], 0, i);
}

static gboolean
grid_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyTileGrid *self = OMARCHY_TILE_GRID (w);
  /* Left and Right (and h/l) mirror in RTL, as the grid does. */
  guint k = keyval;
  if (gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL && !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK))) {
    switch (keyval) {
    case GDK_KEY_Left: k = GDK_KEY_Right; break;
    case GDK_KEY_Right: k = GDK_KEY_Left; break;
    case GDK_KEY_h: k = GDK_KEY_l; break;
    case GDK_KEY_l: k = GDK_KEY_h; break;
    default: break;
    }
  }
  return omarchy_cursor_model_handle_key (self->model, k, state);
}

static void
grid_motion (GtkEventControllerMotion *c, double x, double y, OmarchyTileGrid *self)
{
  int i = grid_index_at (self, x, y);
  if (i < 0)
    return;
  g_signal_emit (self, g_signals[G_POINTER_MOVED], 0, i, x, y);
  omarchy_cursor_model_pointer_moved (self->model, GTK_WIDGET (self), x, y, "", i);
}

static void
tile_clear (gpointer data)
{
  Tile *t = data;
  g_free (t->label);
  g_free (t->glyph);
  if (t->avatar)
    gtk_widget_unparent (t->avatar);
}

static void
omarchy_tile_grid_init (OmarchyTileGrid *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->tiles = g_array_new (FALSE, TRUE, sizeof (Tile));
  g_array_set_clear_func (self->tiles, tile_clear);
  self->columns = 4;
  self->cell_width = -1;
  self->cursor_index = self->current_index = -1;
  self->model = omarchy_cursor_model_new_list (0);
  omarchy_cursor_model_set_columns (self->model, self->columns);
  g_signal_connect_object (self->model, "notify::selected-index", G_CALLBACK (grid_model_changed), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->model, "notify::cursor-active", G_CALLBACK (grid_model_changed), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->model, "activated", G_CALLBACK (grid_model_activated), self, G_CONNECT_SWAPPED);
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, grid_clicked);
  omarchy_widget_add_keys (w, grid_key);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "motion", G_CALLBACK (grid_motion), self);
  gtk_widget_add_controller (w, m);
}

static void
grid_dispose (GObject *o)
{
  OmarchyTileGrid *self = OMARCHY_TILE_GRID (o);
  if (self->tiles)
    g_array_set_size (self->tiles, 0);
  g_clear_object (&self->model);
  G_OBJECT_CLASS (omarchy_tile_grid_parent_class)->dispose (o);
}

static void
grid_finalize (GObject *o)
{
  g_array_unref (OMARCHY_TILE_GRID (o)->tiles);
  G_OBJECT_CLASS (omarchy_tile_grid_parent_class)->finalize (o);
}

static void
grid_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyTileGrid *self = OMARCHY_TILE_GRID (o);
  switch (id) {
  case G_PROP_COLUMNS: omarchy_tile_grid_set_columns (self, g_value_get_int (v)); break;
  case G_PROP_CELL_WIDTH: omarchy_tile_grid_set_cell_width (self, g_value_get_int (v)); break;
  case G_PROP_CURSOR_INDEX: omarchy_tile_grid_set_cursor_index (self, g_value_get_int (v)); break;
  case G_PROP_CURRENT_INDEX: omarchy_tile_grid_set_current_index (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
grid_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyTileGrid *self = OMARCHY_TILE_GRID (o);
  switch (id) {
  case G_PROP_COLUMNS: g_value_set_int (v, self->columns); break;
  case G_PROP_CELL_WIDTH: g_value_set_int (v, self->cell_width); break;
  case G_PROP_CURSOR_INDEX: g_value_set_int (v, self->cursor_index); break;
  case G_PROP_CURRENT_INDEX: g_value_set_int (v, self->current_index); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_tile_grid_class_init (OmarchyTileGridClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = grid_dispose;
  oc->finalize = grid_finalize;
  oc->set_property = grid_set_property;
  oc->get_property = grid_get_property;
  wc->measure = grid_measure;
  wc->size_allocate = grid_allocate;
  wc->snapshot = grid_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyTileGrid:columns:
   *
   * Tiles to a line.
   */
  g_props[G_PROP_COLUMNS] = g_param_spec_int ("columns", NULL, NULL, 1, 1024, 4, f);
  /**
   * OmarchyTileGrid:cell-width:
   *
   * A tile's width, or -1 for `space(88)`.
   */
  g_props[G_PROP_CELL_WIDTH] = g_param_spec_int ("cell-width", NULL, NULL, -1, 4096, -1, f);
  /**
   * OmarchyTileGrid:cursor-index:
   *
   * The tile under the keyboard cursor, or -1.
   */
  g_props[G_PROP_CURSOR_INDEX] = g_param_spec_int ("cursor-index", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyTileGrid:current-index:
   *
   * The current tile (the selected fill), or -1.
   */
  g_props[G_PROP_CURRENT_INDEX] = g_param_spec_int ("current-index", NULL, NULL, -1, G_MAXINT, -1, f);
  g_object_class_install_properties (oc, G_N_PROPS, g_props);
  /**
   * OmarchyTileGrid::activated:
   * @self: the grid
   * @index: the tile
   *
   * A click on a tile, or Return/Space on the cursor's.
   */
  g_signals[G_ACTIVATED] = g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                         G_TYPE_NONE, 1, G_TYPE_INT);
  /**
   * OmarchyTileGrid::pointer-moved:
   * @self: the grid
   * @index: the tile under the pointer
   * @x: the pointer in the grid's coordinates
   * @y: the pointer in the grid's coordinates
   */
  g_signals[G_POINTER_MOVED] = g_signal_new ("pointer-moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                             NULL, G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  gtk_widget_class_set_css_name (wc, "omarchy-tile-grid");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GRID);
}

/**
 * omarchy_tile_grid_new:
 *
 * Returns: (transfer floating): an empty tile grid
 */
GtkWidget *
omarchy_tile_grid_new (void)
{
  return g_object_new (OMARCHY_TYPE_TILE_GRID, NULL);
}

static void
grid_add (OmarchyTileGrid *self, Tile *t)
{
  g_array_append_val (self->tiles, *t);
  if (t->avatar)
    gtk_widget_set_parent (t->avatar, GTK_WIDGET (self));
  grid_sync_model (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

/**
 * omarchy_tile_grid_append:
 * @self: the grid
 * @label: the tile's label
 * @glyph: (nullable): its glyph
 */
void
omarchy_tile_grid_append (OmarchyTileGrid *self, const char *label, const char *glyph)
{
  Tile t = { g_strdup (label ? label : ""), g_strdup (glyph ? glyph : ""), NULL };
  grid_add (self, &t);
}

/**
 * omarchy_tile_grid_append_avatar:
 * @self: the grid
 * @label: the tile's label
 * @name: (nullable): the avatar's name (the label when %NULL)
 * @source: (nullable): the avatar's picture
 * @glyph: (nullable): the avatar's glyph
 *
 * A tile with a medium #OmarchyAvatar.
 */
void
omarchy_tile_grid_append_avatar (OmarchyTileGrid *self, const char *label, const char *name, const char *source,
                                 const char *glyph)
{
  GtkWidget *a = omarchy_avatar_new (name ? name : label);
  omarchy_avatar_set_source (OMARCHY_AVATAR (a), source);
  omarchy_avatar_set_glyph (OMARCHY_AVATAR (a), glyph);
  Tile t = { g_strdup (label ? label : ""), g_strdup (""), a };
  grid_add (self, &t);
}

void
omarchy_tile_grid_remove_all (OmarchyTileGrid *self)
{
  g_array_set_size (self->tiles, 0);
  grid_sync_model (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

guint
omarchy_tile_grid_get_n_tiles (OmarchyTileGrid *self)
{
  return self->tiles->len;
}

int
omarchy_tile_grid_get_columns (OmarchyTileGrid *self)
{
  return self->columns;
}

void
omarchy_tile_grid_set_columns (OmarchyTileGrid *self, int columns)
{
  columns = MAX (1, columns);
  if (self->columns == columns)
    return;
  self->columns = columns;
  grid_sync_model (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), g_props[G_PROP_COLUMNS]);
}

int
omarchy_tile_grid_get_cell_width (OmarchyTileGrid *self)
{
  return self->cell_width;
}

void
omarchy_tile_grid_set_cell_width (OmarchyTileGrid *self, int cell_width)
{
  if (self->cell_width == cell_width)
    return;
  self->cell_width = MAX (-1, cell_width);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), g_props[G_PROP_CELL_WIDTH]);
}

int
omarchy_tile_grid_get_cursor_index (OmarchyTileGrid *self)
{
  return self->cursor_index;
}

/**
 * omarchy_tile_grid_set_cursor_index:
 * @self: the grid
 * @index: the tile under the cursor, or -1 to hide it
 */
void
omarchy_tile_grid_set_cursor_index (OmarchyTileGrid *self, int index)
{
  if (index < 0)
    omarchy_cursor_model_reset (self->model);
  else
    omarchy_cursor_model_set (self->model, "", index);
  grid_model_changed (self);
}

int
omarchy_tile_grid_get_current_index (OmarchyTileGrid *self)
{
  return self->current_index;
}

void
omarchy_tile_grid_set_current_index (OmarchyTileGrid *self, int index)
{
  if (self->current_index == index)
    return;
  self->current_index = MAX (-1, index);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), g_props[G_PROP_CURRENT_INDEX]);
}

/**
 * omarchy_tile_grid_get_cursor_model:
 * @self: the grid
 *
 * Returns: (transfer none): the cursor the grid's keys and pointer move
 */
OmarchyCursorModel *
omarchy_tile_grid_get_cursor_model (OmarchyTileGrid *self)
{
  return self->model;
}
