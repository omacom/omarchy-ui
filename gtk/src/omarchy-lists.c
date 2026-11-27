/* omarchy-lists.c: ListRow (ListRow.qml on CursorSurface.qml), drawn as the Qt kit draws it, and
 * the helpers the Lists files share. */
#include "omarchy-lists-private.h"

#include <math.h>
#include <string.h>

/* ================================================================================ shared === */

gboolean
omarchy_lists_toned (const char *tone)
{
  return tone && *tone && strcmp (tone, "neutral") != 0;
}

GdkRGBA
omarchy_lists_tone (const char *tone, const GdkRGBA *neutral)
{
  const OmarchyTokens *p = omarchy_paint ();
  if (!tone || !*tone)
    return *neutral;
  if (!strcmp (tone, "accent") || !strcmp (tone, "info"))
    return p->accent;
  if (!strcmp (tone, "success"))
    return p->success;
  if (!strcmp (tone, "warning"))
    return p->warning;
  if (!strcmp (tone, "urgent") || !strcmp (tone, "error"))
    return p->urgent;
  if (!strcmp (tone, "muted"))
    return p->muted;
  return *neutral;
}

PangoLayout *
omarchy_lists_line (GtkWidget *w, const char *text, int px, gboolean bold, int width)
{
  PangoLayout *l = omarchy_layout (w, text, px, bold);
  pango_layout_set_single_paragraph_mode (l, TRUE);
  if (width >= 0) {
    pango_layout_set_width (l, MAX (0, width) * PANGO_SCALE);
    pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
  }
  return l;
}

void
omarchy_lists_glyph_size (GtkWidget *w, const char *glyph, int px, int *width, int *height)
{
  g_autoptr (PangoLayout) l = omarchy_layout (w, glyph, px, FALSE);
  omarchy_glyph_box (l, width, NULL);
  if (height)
    *height = omarchy_line_height (px);
}

float
omarchy_lists_glyph_width_f (GtkWidget *w, const char *glyph, int px)
{
  g_autoptr (PangoLayout) l = omarchy_layout (w, glyph, px, FALSE);
  PangoRectangle ink, logical;
  pango_layout_get_extents (l, &ink, &logical);
  double advance = logical.width / (double) PANGO_SCALE;
  double x = ink.x / (double) PANGO_SCALE, iw = ink.width / (double) PANGO_SCALE;
  return advance + MAX (0, -x) + MAX (0, x + iw - advance);
}

float
omarchy_lists_center (float parent, float child)
{
  /* QQuickAnchors' hcenter()/vcenter() with alignWhenCentered: an odd (truncated) size centers
   * on its upper half pixel. */
  float pc = ((int) parent % 2) ? (parent + 1) / 2 : parent / 2;
  float cc = ((int) child % 2) ? (child + 1) / 2 : child / 2;
  return pc - cc;
}

void
omarchy_lists_draw_glyph (GtkSnapshot *s, GtkWidget *w, const char *glyph, int px, float x, float y, float bw, float bh,
                          const GdkRGBA *color)
{
  if (!glyph || !*glyph)
    return;
  g_autoptr (PangoLayout) l = omarchy_layout (w, glyph, px, FALSE);
  PangoRectangle ink, logical;
  pango_layout_get_extents (l, &ink, &logical);
  float left = MAX (0, -ink.x / (float) PANGO_SCALE);
  float gw = omarchy_lists_glyph_width_f (w, glyph, px);
  int gh = omarchy_line_height (px);
  omarchy_draw_layout (s, l, x + omarchy_lists_center (bw, gw) + left, y + omarchy_lists_center (bh, gh), color);
}

void
omarchy_lists_draw_glyph_at (GtkSnapshot *s, GtkWidget *w, const char *glyph, int px, float x, float y, const GdkRGBA *color)
{
  if (!glyph || !*glyph)
    return;
  g_autoptr (PangoLayout) l = omarchy_layout (w, glyph, px, FALSE);
  int dx = omarchy_glyph_box (l, NULL, NULL);
  omarchy_draw_layout (s, l, x + dx, y, color);
}

/* =============================================================================== ListRow === */

/**
 * OmarchyListRow:
 *
 * One row of a keyboard-and-mouse list: a leading glyph (or any widget, such as a small
 * #OmarchyAvatar), a bold title over a dim subtitle and a status line, a dim value (or any
 * trailing widget, such as a #OmarchyBadge), and an end slot holding #OmarchyListRow:trailing-glyph
 * at rest and the action in its place while the action shows.
 *
 * The look follows #OmarchyListRow:has-cursor (the hover fill and border) and
 * #OmarchyListRow:current (the selected fill), never the pointer: hand ::pointer-moved to a
 * #OmarchyCursorModel (omarchy_cursor_model_bind()) so there is one highlight on screen. Keyboard
 * focus looks like the cursor. Return and Space emit ::clicked, Delete ::action-clicked.
 */
struct _OmarchyListRow {
  GtkWidget parent_instance;
  char *glyph, *title, *subtitle, *status, *status_tone, *value;
  char *trailing_glyph, *trailing_tone, *action_glyph, *action_tip;
  int glyph_size; /* 0: font.title */
  gboolean has_glyph_color;
  GdkRGBA glyph_color;
  gboolean action_urgent, action_cursor, has_cursor, current, disabled;
  gboolean pointer_inside;
  GtkWidget *leading, *trailing, *expansion;
};

enum {
  R_PROP_0,
  R_PROP_GLYPH,
  R_PROP_GLYPH_SIZE,
  R_PROP_TITLE,
  R_PROP_SUBTITLE,
  R_PROP_STATUS,
  R_PROP_STATUS_TONE,
  R_PROP_VALUE,
  R_PROP_TRAILING_GLYPH,
  R_PROP_TRAILING_TONE,
  R_PROP_ACTION_GLYPH,
  R_PROP_ACTION_URGENT,
  R_PROP_ACTION_CURSOR,
  R_PROP_ACTION_TIP,
  R_PROP_HAS_CURSOR,
  R_PROP_CURRENT,
  R_PROP_DISABLED,
  R_PROP_LEADING,
  R_PROP_TRAILING,
  R_PROP_EXPANSION,
  R_N_PROPS
};
static GParamSpec *r_props[R_N_PROPS];

enum { R_CLICKED, R_ACTION_CLICKED, R_POINTER_MOVED, R_HOVERED, R_N_SIGNALS };
static guint r_signals[R_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyListRow, omarchy_list_row, GTK_TYPE_WIDGET)

static gboolean
row_cursor_look (OmarchyListRow *self)
{
  return self->has_cursor || (!self->disabled && omarchy_widget_focus_shown (GTK_WIDGET (self)));
}

/**
 * omarchy_list_row_get_action_shown:
 * @self: the row
 *
 * Returns: whether the action glyph shows (the cursor, the pointer or the action cursor is on
 *   the row, and it isn't disabled)
 */
gboolean
omarchy_list_row_get_action_shown (OmarchyListRow *self)
{
  return self->action_glyph[0] && !self->disabled && (row_cursor_look (self) || self->action_cursor || self->pointer_inside);
}

/* The row's geometry at a width (the body's parts, left to right, before mirroring). */
typedef struct {
  int pad, lead_w, info_x, info_w, trail_w;
  int slot, gap8;
  int title_h, sub_h, status_h, line_gap, info_h;
  int lead_h, trail_h, body_h;
  int value_w, value_h, trailing_w, trailing_h, leading_w, leading_h;
  gboolean has_slot, has_value;
  int exp_h;
} RowGeom;

static void
row_geom (OmarchyListRow *self, int width, RowGeom *g)
{
  GtkWidget *w = GTK_WIDGET (self);
  const OmarchyTokens *t = omarchy_tokens ();
  memset (g, 0, sizeof *g);
  g->pad = t->control_padding_x;
  g->slot = omarchy_space (22);
  g->gap8 = omarchy_space (8);

  gboolean lead_shown = child_shown (self->leading) || self->glyph[0];
  g->lead_w = lead_shown ? t->icon_large + omarchy_space (10) : 0;
  if (child_shown (self->leading)) {
    gtk_widget_measure (self->leading, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &g->leading_w, NULL, NULL);
    gtk_widget_measure (self->leading, GTK_ORIENTATION_VERTICAL, g->leading_w, NULL, &g->leading_h, NULL, NULL);
    g->lead_h = g->leading_h;
  } else if (self->glyph[0]) {
    g->lead_h = omarchy_line_height (self->glyph_size > 0 ? self->glyph_size : t->title);
  }

  g->line_gap = omarchy_space (1);
  g->title_h = omarchy_line_height (t->body);
  g->sub_h = self->subtitle[0] ? omarchy_line_height (t->caption) : 0;
  g->status_h = self->status[0] ? omarchy_line_height (t->caption) : 0;
  g->info_h = g->title_h + (g->sub_h ? g->line_gap + g->sub_h : 0) + (g->status_h ? g->line_gap + g->status_h : 0);

  /* Trailing: the end slot, then the trailing widget or the value, space(8) apart. */
  g->has_slot = self->action_glyph[0] || self->trailing_glyph[0];
  g->has_value = self->value[0] && !child_shown (self->trailing);
  int items = 0, tw = 0, th = 0;
  if (g->has_slot) {
    tw += g->slot;
    th = MAX (th, g->slot);
    items++;
  }
  if (child_shown (self->trailing)) {
    gtk_widget_measure (self->trailing, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &g->trailing_w, NULL, NULL);
    gtk_widget_measure (self->trailing, GTK_ORIENTATION_VERTICAL, g->trailing_w, NULL, &g->trailing_h, NULL, NULL);
    if (g->trailing_w > 0 && g->trailing_h > 0) {
      tw += (items ? g->gap8 : 0) + g->trailing_w;
      th = MAX (th, g->trailing_h);
      items++;
    }
  }
  if (g->has_value) {
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->value, t->body_small, FALSE);
    omarchy_layout_size (l, &g->value_w, NULL);
    g->value_h = omarchy_line_height (t->body_small);
    tw += (items ? g->gap8 : 0) + g->value_w;
    th = MAX (th, g->value_h);
    items++;
  }
  g->trail_w = items ? tw + g->gap8 : 0;
  g->trail_h = th;
  g->body_h = MAX (g->lead_h, MAX (g->info_h, g->trail_h));

  int body_w = MAX (0, width - g->pad * 2);
  g->info_x = g->lead_w;
  g->info_w = MAX (0, body_w - g->lead_w - g->trail_w);

  if (child_shown (self->expansion) && width > 0) {
    int emin;
    gtk_widget_measure (self->expansion, GTK_ORIENTATION_HORIZONTAL, -1, &emin, NULL, NULL, NULL);
    int ew = MAX (emin, width - g->pad * 2);
    gtk_widget_measure (self->expansion, GTK_ORIENTATION_VERTICAL, ew, NULL, &g->exp_h, NULL, NULL);
  }
}

static int
row_info_natural (OmarchyListRow *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  const OmarchyTokens *t = omarchy_tokens ();
  int best = 0, tw;
  g_autoptr (PangoLayout) a = omarchy_layout (w, self->title, t->body, TRUE);
  omarchy_layout_size (a, &tw, NULL);
  best = tw;
  if (self->subtitle[0]) {
    g_autoptr (PangoLayout) b = omarchy_layout (w, self->subtitle, t->caption, FALSE);
    omarchy_layout_size (b, &tw, NULL);
    best = MAX (best, tw);
  }
  if (self->status[0]) {
    g_autoptr (PangoLayout) c = omarchy_layout (w, self->status, t->caption, FALSE);
    omarchy_layout_size (c, &tw, NULL);
    best = MAX (best, tw);
  }
  return best;
}

static void
row_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (w);
  RowGeom g;
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    row_geom (self, 0, &g);
    int fixed = g.pad * 2 + g.lead_w + g.trail_w;
    int exp_min = 0, exp_nat = 0;
    if (child_shown (self->expansion))
      gtk_widget_measure (self->expansion, GTK_ORIENTATION_HORIZONTAL, -1, &exp_min, &exp_nat, NULL, NULL);
    *min = MAX (fixed, exp_min + g.pad * 2);
    *nat = MAX (fixed + row_info_natural (self), exp_nat + g.pad * 2);
    return;
  }
  int width = for_size, wmin;
  if (width < 0)
    row_measure (w, GTK_ORIENTATION_HORIZONTAL, -1, &wmin, &width, NULL, NULL);
  row_geom (self, width, &g);
  const OmarchyTokens *t = omarchy_tokens ();
  int h = g.body_h + t->row_padding_x + (child_shown (self->expansion) ? g.exp_h + omarchy_space (4) : 0);
  *min = *nat = h;
}

static float
row_body_y (OmarchyListRow *self, const RowGeom *g, int height)
{
  if (child_shown (self->expansion))
    return omarchy_tokens ()->row_padding_x / 2.0f;
  return roundf ((height - g->body_h) / 2.0f);
}

/* x of a span [x, x + w) of the body, mirrored in RTL. */
static float
row_x (GtkWidget *w, const RowGeom *g, float x, float span)
{
  float body_x = g->pad + x;
  if (gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL)
    return gtk_widget_get_width (w) - body_x - span;
  return body_x;
}

static void
row_slot_rect (OmarchyListRow *self, const RowGeom *g, graphene_rect_t *r)
{
  GtkWidget *w = GTK_WIDGET (self);
  int W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  float by = row_body_y (self, g, H);
  float body_w = W - g->pad * 2;
  float x = body_w - g->slot;
  *r = GRAPHENE_RECT_INIT (row_x (w, g, x, g->slot), by + omarchy_lists_center (g->body_h, g->trail_h)
                                                          + omarchy_lists_center (g->trail_h, g->slot),
                           g->slot, g->slot);
}

static void
row_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (w);
  const OmarchyTokens *t = omarchy_tokens ();
  RowGeom g;
  row_geom (self, width, &g);
  float by = row_body_y (self, &g, height);
  if (child_shown (self->leading)) {
    float lead_y = by + omarchy_lists_center (g.body_h, g.lead_h);
    float lx = roundf ((t->icon_large - g.leading_w) / 2.0f);
    GtkAllocation a = { (int) row_x (w, &g, lx, g.leading_w), (int) (lead_y + omarchy_lists_center (g.lead_h, g.leading_h)),
                        g.leading_w, g.leading_h };
    gtk_widget_size_allocate (self->leading, &a, -1);
  }
  if (child_shown (self->trailing)) {
    float body_w = width - g.pad * 2;
    float trail_y = by + omarchy_lists_center (g.body_h, g.trail_h);
    /* Right to left: the slot, then the trailing widget. */
    float right = body_w - (g.has_slot ? g.slot + g.gap8 : 0);
    float tx = right - g.trailing_w;
    GtkAllocation a = { (int) row_x (w, &g, tx, g.trailing_w), (int) (trail_y + omarchy_lists_center (g.trail_h, g.trailing_h)),
                        g.trailing_w, g.trailing_h };
    gtk_widget_size_allocate (self->trailing, &a, -1);
  }
  if (child_shown (self->expansion)) {
    GtkAllocation a = { g.pad, (int) (by + g.body_h + omarchy_space (4)), MAX (0, width - g.pad * 2), g.exp_h };
    gtk_widget_size_allocate (self->expansion, &a, -1);
  }
}

static void
row_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (w);
  const OmarchyTokens *p = omarchy_paint ();
  int W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  graphene_rect_t all = GRAPHENE_RECT_INIT (0, 0, W, H);

  /* CursorSurface: the cursor wins over current. */
  if (row_cursor_look (self))
    omarchy_draw_surface (s, &all, p->radius, &p->hover.fill, &p->hover.border);
  else if (self->current)
    omarchy_draw_surface (s, &all, p->radius, &p->selected.fill, &p->selected.border);

  RowGeom g;
  row_geom (self, W, &g);
  float by = row_body_y (self, &g, H);
  GdkRGBA faint = p->text_faint;
  const GdkRGBA *title_c = self->disabled ? &faint : &p->foreground;
  const GdkRGBA *dim_c = self->disabled ? &faint : &p->text_dim;

  /* Leading glyph, centered in its slot by its box. */
  if (!child_shown (self->leading) && self->glyph[0]) {
    int px = self->glyph_size > 0 ? self->glyph_size : p->title;
    GdkRGBA c = self->disabled ? faint : (self->has_glyph_color ? self->glyph_color : p->foreground);
    int gw;
    omarchy_lists_glyph_size (w, self->glyph, px, &gw, NULL);
    float lead_y = by + omarchy_lists_center (g.body_h, g.lead_h);
    float gx = roundf ((p->icon_large - gw) / 2.0f);
    omarchy_lists_draw_glyph_at (s, w, self->glyph, px, row_x (w, &g, gx, gw), lead_y + omarchy_lists_center (g.lead_h, g.lead_h), &c);
  }

  /* Info column. */
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  float iy = by + omarchy_lists_center (g.body_h, g.info_h);
  struct {
    const char *text;
    int px;
    gboolean bold;
    GdkRGBA color;
    int h;
  } lines[3] = {
    { self->title, p->body, TRUE, *title_c, g.title_h },
    { self->subtitle, p->caption, FALSE, *dim_c, g.sub_h },
    { self->status, p->caption, FALSE,
      self->disabled ? faint : omarchy_lists_tone (self->status_tone, &p->text_dim), g.status_h },
  };
  for (int i = 0; i < 3; i++) {
    if (i > 0 && !lines[i].text[0])
      continue;
    g_autoptr (PangoLayout) l = omarchy_lists_line (w, lines[i].text, lines[i].px, lines[i].bold, g.info_w);
    if (rtl)
      pango_layout_set_alignment (l, PANGO_ALIGN_RIGHT);
    omarchy_draw_layout (s, l, row_x (w, &g, g.info_x, g.info_w), iy, &lines[i].color);
    iy += lines[i].h + g.line_gap;
  }

  /* Value: right to left after the slot (and no trailing widget). */
  float body_w = W - g.pad * 2;
  float trail_y = by + omarchy_lists_center (g.body_h, g.trail_h);
  if (g.has_value) {
    float right = body_w - (g.has_slot ? g.slot + g.gap8 : 0);
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->value, p->body_small, FALSE);
    omarchy_draw_layout (s, l, row_x (w, &g, right - g.value_w, g.value_w), trail_y + omarchy_lists_center (g.trail_h, g.value_h),
                         dim_c);
  }

  /* The end slot: the action's cursor look, then the action or the trailing glyph. */
  if (g.has_slot) {
    graphene_rect_t r;
    row_slot_rect (self, &g, &r);
    if (self->action_cursor && !self->disabled) {
      GdkRGBA ink = self->action_urgent ? p->urgent : p->foreground;
      GdkRGBA fill = omarchy_rgba_alpha (&ink, p->hover.fill.alpha);
      GdkRGBA bc = omarchy_rgba_alpha (&ink, p->hover.border.color.alpha);
      OmarchyBorder b = omarchy_border_flat (&bc, omarchy_border_max (&p->hover.border));
      omarchy_draw_surface (s, &r, omarchy_radius_small (), &fill, &b);
    }
    if (omarchy_list_row_get_action_shown (self)) {
      GdkRGBA c = self->action_urgent ? p->urgent : p->text_dim;
      omarchy_lists_draw_glyph (s, w, self->action_glyph, p->subtitle, r.origin.x, r.origin.y, r.size.width, r.size.height, &c);
    } else if (self->trailing_glyph[0]) {
      GdkRGBA c = self->disabled ? faint : omarchy_lists_tone (self->trailing_tone, &p->text_dim);
      omarchy_lists_draw_glyph (s, w, self->trailing_glyph, p->subtitle, r.origin.x, r.origin.y, r.size.width, r.size.height,
                                &c);
    }
  }

  for (GtkWidget *c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c))
    gtk_widget_snapshot_child (w, c, s);
}

static gboolean
row_in_slot (OmarchyListRow *self, double x, double y)
{
  if (!self->action_glyph[0])
    return FALSE;
  RowGeom g;
  row_geom (self, gtk_widget_get_width (GTK_WIDGET (self)), &g);
  graphene_rect_t r;
  row_slot_rect (self, &g, &r);
  return graphene_rect_contains_point (&r, &GRAPHENE_POINT_INIT (x, y));
}

static void
row_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (w);
  if (button != GDK_BUTTON_PRIMARY || self->disabled)
    return;
  if (row_in_slot (self, x, y))
    g_signal_emit (self, r_signals[R_ACTION_CLICKED], 0);
  else
    g_signal_emit (self, r_signals[R_CLICKED], 0);
}

static gboolean
row_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (w);
  if (self->disabled)
    return FALSE;
  if (omarchy_key_is_activate (keyval)) {
    g_signal_emit (self, r_signals[R_CLICKED], 0);
    return TRUE;
  }
  if ((keyval == GDK_KEY_Delete || keyval == GDK_KEY_KP_Delete) && self->action_glyph[0]) {
    g_signal_emit (self, r_signals[R_ACTION_CLICKED], 0);
    return TRUE;
  }
  return FALSE;
}

static void
row_set_inside (OmarchyListRow *self, gboolean inside)
{
  if (self->pointer_inside == inside)
    return;
  self->pointer_inside = inside;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
row_enter (GtkEventControllerMotion *c, double x, double y, OmarchyListRow *self)
{
  if (self->disabled)
    return;
  row_set_inside (self, TRUE);
  g_signal_emit (self, r_signals[R_HOVERED], 0);
}

static void
row_leave (GtkEventControllerMotion *c, OmarchyListRow *self)
{
  row_set_inside (self, FALSE);
}

static void
row_motion (GtkEventControllerMotion *c, double x, double y, OmarchyListRow *self)
{
  if (self->disabled)
    return;
  row_set_inside (self, TRUE);
  g_signal_emit (self, r_signals[R_POINTER_MOVED], 0, x, y);
}

static gboolean
row_query_tooltip (GtkWidget *w, int x, int y, gboolean keyboard, GtkTooltip *tip, gpointer data)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (w);
  if (!self->action_tip[0] || self->disabled)
    return FALSE;
  if (!keyboard && !row_in_slot (self, x, y))
    return FALSE;
  if (keyboard && !self->action_cursor)
    return FALSE;
  gtk_tooltip_set_text (tip, self->action_tip);
  return TRUE;
}

static void
row_set_child (OmarchyListRow *self, GtkWidget **slot, GtkWidget *child, int prop)
{
  if (*slot == child)
    return;
  if (*slot)
    gtk_widget_unparent (*slot);
  *slot = child;
  if (child)
    gtk_widget_set_parent (child, GTK_WIDGET (self));
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), r_props[prop]);
}

static void
omarchy_list_row_init (OmarchyListRow *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->glyph = g_strdup ("");
  self->title = g_strdup ("");
  self->subtitle = g_strdup ("");
  self->status = g_strdup ("");
  self->status_tone = g_strdup ("");
  self->value = g_strdup ("");
  self->trailing_glyph = g_strdup ("");
  self->trailing_tone = g_strdup ("");
  self->action_glyph = g_strdup ("");
  self->action_tip = g_strdup ("");
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, row_clicked);
  omarchy_widget_add_keys (w, row_key);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "enter", G_CALLBACK (row_enter), self);
  g_signal_connect (m, "leave", G_CALLBACK (row_leave), self);
  g_signal_connect (m, "motion", G_CALLBACK (row_motion), self);
  gtk_widget_add_controller (w, m);
  g_signal_connect (w, "query-tooltip", G_CALLBACK (row_query_tooltip), NULL);
}

static void
row_dispose (GObject *o)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (o);
  g_clear_pointer (&self->leading, gtk_widget_unparent);
  g_clear_pointer (&self->trailing, gtk_widget_unparent);
  g_clear_pointer (&self->expansion, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_list_row_parent_class)->dispose (o);
}

static void
row_finalize (GObject *o)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (o);
  g_free (self->glyph);
  g_free (self->title);
  g_free (self->subtitle);
  g_free (self->status);
  g_free (self->status_tone);
  g_free (self->value);
  g_free (self->trailing_glyph);
  g_free (self->trailing_tone);
  g_free (self->action_glyph);
  g_free (self->action_tip);
  G_OBJECT_CLASS (omarchy_list_row_parent_class)->finalize (o);
}

static void
row_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (o);
  switch (id) {
  case R_PROP_GLYPH: omarchy_list_row_set_glyph (self, g_value_get_string (v)); break;
  case R_PROP_GLYPH_SIZE: omarchy_list_row_set_glyph_size (self, g_value_get_int (v)); break;
  case R_PROP_TITLE: omarchy_list_row_set_title (self, g_value_get_string (v)); break;
  case R_PROP_SUBTITLE: omarchy_list_row_set_subtitle (self, g_value_get_string (v)); break;
  case R_PROP_STATUS: omarchy_list_row_set_status (self, g_value_get_string (v), self->status_tone); break;
  case R_PROP_STATUS_TONE: omarchy_list_row_set_status_tone (self, g_value_get_string (v)); break;
  case R_PROP_VALUE: omarchy_list_row_set_value (self, g_value_get_string (v)); break;
  case R_PROP_TRAILING_GLYPH: omarchy_list_row_set_trailing_glyph (self, g_value_get_string (v)); break;
  case R_PROP_TRAILING_TONE: omarchy_list_row_set_trailing_tone (self, g_value_get_string (v)); break;
  case R_PROP_ACTION_GLYPH: omarchy_list_row_set_action_glyph (self, g_value_get_string (v)); break;
  case R_PROP_ACTION_URGENT: omarchy_list_row_set_action_urgent (self, g_value_get_boolean (v)); break;
  case R_PROP_ACTION_CURSOR: omarchy_list_row_set_action_cursor (self, g_value_get_boolean (v)); break;
  case R_PROP_ACTION_TIP: omarchy_list_row_set_action_tip (self, g_value_get_string (v)); break;
  case R_PROP_HAS_CURSOR: omarchy_list_row_set_has_cursor (self, g_value_get_boolean (v)); break;
  case R_PROP_CURRENT: omarchy_list_row_set_current (self, g_value_get_boolean (v)); break;
  case R_PROP_DISABLED: omarchy_list_row_set_disabled (self, g_value_get_boolean (v)); break;
  case R_PROP_LEADING: omarchy_list_row_set_leading (self, g_value_get_object (v)); break;
  case R_PROP_TRAILING: omarchy_list_row_set_trailing (self, g_value_get_object (v)); break;
  case R_PROP_EXPANSION: omarchy_list_row_set_expansion (self, g_value_get_object (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
row_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyListRow *self = OMARCHY_LIST_ROW (o);
  switch (id) {
  case R_PROP_GLYPH: g_value_set_string (v, self->glyph); break;
  case R_PROP_GLYPH_SIZE: g_value_set_int (v, self->glyph_size); break;
  case R_PROP_TITLE: g_value_set_string (v, self->title); break;
  case R_PROP_SUBTITLE: g_value_set_string (v, self->subtitle); break;
  case R_PROP_STATUS: g_value_set_string (v, self->status); break;
  case R_PROP_STATUS_TONE: g_value_set_string (v, self->status_tone); break;
  case R_PROP_VALUE: g_value_set_string (v, self->value); break;
  case R_PROP_TRAILING_GLYPH: g_value_set_string (v, self->trailing_glyph); break;
  case R_PROP_TRAILING_TONE: g_value_set_string (v, self->trailing_tone); break;
  case R_PROP_ACTION_GLYPH: g_value_set_string (v, self->action_glyph); break;
  case R_PROP_ACTION_URGENT: g_value_set_boolean (v, self->action_urgent); break;
  case R_PROP_ACTION_CURSOR: g_value_set_boolean (v, self->action_cursor); break;
  case R_PROP_ACTION_TIP: g_value_set_string (v, self->action_tip); break;
  case R_PROP_HAS_CURSOR: g_value_set_boolean (v, self->has_cursor); break;
  case R_PROP_CURRENT: g_value_set_boolean (v, self->current); break;
  case R_PROP_DISABLED: g_value_set_boolean (v, self->disabled); break;
  case R_PROP_LEADING: g_value_set_object (v, self->leading); break;
  case R_PROP_TRAILING: g_value_set_object (v, self->trailing); break;
  case R_PROP_EXPANSION: g_value_set_object (v, self->expansion); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_list_row_class_init (OmarchyListRowClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = row_dispose;
  oc->finalize = row_finalize;
  oc->set_property = row_set_property;
  oc->get_property = row_get_property;
  wc->measure = row_measure;
  wc->size_allocate = row_allocate;
  wc->snapshot = row_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyListRow:glyph:
   *
   * The leading Nerd Font glyph (`font.title`), centered in an `iconLarge` slot.
   */
  r_props[R_PROP_GLYPH] = g_param_spec_string ("glyph", NULL, NULL, "", f);
  /**
   * OmarchyListRow:glyph-size:
   *
   * The leading glyph's pixel size, or 0 for `font.title`.
   */
  r_props[R_PROP_GLYPH_SIZE] = g_param_spec_int ("glyph-size", NULL, NULL, 0, 1000, 0, f);
  /**
   * OmarchyListRow:title:
   *
   * The bold title (`body`).
   */
  r_props[R_PROP_TITLE] = g_param_spec_string ("title", NULL, NULL, "", f);
  /**
   * OmarchyListRow:subtitle:
   *
   * The dim line under the title (`caption`).
   */
  r_props[R_PROP_SUBTITLE] = g_param_spec_string ("subtitle", NULL, NULL, "", f);
  /**
   * OmarchyListRow:status:
   *
   * The status line (`caption`), dim unless #OmarchyListRow:status-tone tints it.
   */
  r_props[R_PROP_STATUS] = g_param_spec_string ("status", NULL, NULL, "", f);
  /**
   * OmarchyListRow:status-tone:
   *
   * `""` (dim), `accent`, `success`, `warning`, `urgent` or `muted`.
   */
  r_props[R_PROP_STATUS_TONE] = g_param_spec_string ("status-tone", NULL, NULL, "", f);
  /**
   * OmarchyListRow:value:
   *
   * Dim trailing text (`bodySmall`), unless a trailing widget is set.
   */
  r_props[R_PROP_VALUE] = g_param_spec_string ("value", NULL, NULL, "", f);
  /**
   * OmarchyListRow:trailing-glyph:
   *
   * A glyph that stays in the end slot (a lock, a chevron, a check), replaced by the action
   * while the action shows.
   */
  r_props[R_PROP_TRAILING_GLYPH] = g_param_spec_string ("trailing-glyph", NULL, NULL, "", f);
  /**
   * OmarchyListRow:trailing-tone:
   *
   * The trailing glyph's tone; dim when `""`.
   */
  r_props[R_PROP_TRAILING_TONE] = g_param_spec_string ("trailing-tone", NULL, NULL, "", f);
  /**
   * OmarchyListRow:action-glyph:
   *
   * The action (forget), revealed in the end slot under the cursor or the pointer.
   */
  r_props[R_PROP_ACTION_GLYPH] = g_param_spec_string ("action-glyph", NULL, NULL, "", f);
  /**
   * OmarchyListRow:action-urgent:
   *
   * The action in `urgent`.
   */
  r_props[R_PROP_ACTION_URGENT] = g_param_spec_boolean ("action-urgent", NULL, NULL, FALSE, f);
  /**
   * OmarchyListRow:action-cursor:
   *
   * The cursor is on the action rather than the row: its slot takes the cursor look in its
   * own ink.
   */
  r_props[R_PROP_ACTION_CURSOR] = g_param_spec_boolean ("action-cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyListRow:action-tip:
   *
   * The action's tooltip.
   */
  r_props[R_PROP_ACTION_TIP] = g_param_spec_string ("action-tip", NULL, NULL, "", f);
  /**
   * OmarchyListRow:has-cursor:
   *
   * The keyboard cursor is on the row (the hover fill and border).
   */
  r_props[R_PROP_HAS_CURSOR] = g_param_spec_boolean ("has-cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyListRow:current:
   *
   * The row is the current one (the selected fill), as the connected network.
   */
  r_props[R_PROP_CURRENT] = g_param_spec_boolean ("current", NULL, NULL, FALSE, f);
  /**
   * OmarchyListRow:disabled:
   *
   * Everything `textFaint`; no clicks, no pointer, no action.
   */
  r_props[R_PROP_DISABLED] = g_param_spec_boolean ("disabled", NULL, NULL, FALSE, f);
  /**
   * OmarchyListRow:leading:
   *
   * Any widget leading the row instead of the glyph (a small #OmarchyAvatar).
   */
  r_props[R_PROP_LEADING] = g_param_spec_object ("leading", NULL, NULL, GTK_TYPE_WIDGET, f);
  /**
   * OmarchyListRow:trailing:
   *
   * Any widget instead of the value (a #OmarchyBadge, a non-interactive switch).
   */
  r_props[R_PROP_TRAILING] = g_param_spec_object ("trailing", NULL, NULL, GTK_TYPE_WIDGET, f);
  /**
   * OmarchyListRow:expansion:
   *
   * Opens the row: any widget across it under the main line (a password field).
   */
  r_props[R_PROP_EXPANSION] = g_param_spec_object ("expansion", NULL, NULL, GTK_TYPE_WIDGET, f);
  g_object_class_install_properties (oc, R_N_PROPS, r_props);

  /**
   * OmarchyListRow::clicked:
   * @self: the row
   *
   * A click on the row (not the action), or Return/Space while it has focus.
   */
  r_signals[R_CLICKED] = g_signal_new ("clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL,
                                       NULL, NULL, G_TYPE_NONE, 0);
  /**
   * OmarchyListRow::action-clicked:
   * @self: the row
   *
   * A click on the action, or Delete while the row has focus.
   */
  r_signals[R_ACTION_CLICKED] = g_signal_new ("action-clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                              NULL, NULL, G_TYPE_NONE, 0);
  /**
   * OmarchyListRow::pointer-moved:
   * @self: the row
   * @x: the pointer in the row's coordinates
   * @y: the pointer in the row's coordinates
   *
   * The pointer moved over the row: hand it to omarchy_cursor_model_pointer_moved().
   */
  r_signals[R_POINTER_MOVED] = g_signal_new ("pointer-moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                             NULL, NULL, G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  /**
   * OmarchyListRow::hovered:
   * @self: the row
   *
   * The pointer entered the row.
   */
  r_signals[R_HOVERED] = g_signal_new ("hovered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 0);
  gtk_widget_class_set_activate_signal (wc, r_signals[R_CLICKED]);
  gtk_widget_class_set_css_name (wc, "omarchy-list-row");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_LIST_ITEM);
}

/**
 * omarchy_list_row_new:
 * @title: (nullable): the title
 *
 * Returns: (transfer floating): a list row
 */
GtkWidget *
omarchy_list_row_new (const char *title)
{
  return g_object_new (OMARCHY_TYPE_LIST_ROW, "title", title ? title : "", NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyListRow, list_row, glyph, r_props, R_PROP_GLYPH, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyListRow, list_row, subtitle, r_props, R_PROP_SUBTITLE, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyListRow, list_row, status_tone, r_props, R_PROP_STATUS_TONE, FALSE)
OMARCHY_STRING_ACCESSORS (OmarchyListRow, list_row, value, r_props, R_PROP_VALUE, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyListRow, list_row, trailing_glyph, r_props, R_PROP_TRAILING_GLYPH, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyListRow, list_row, trailing_tone, r_props, R_PROP_TRAILING_TONE, FALSE)
OMARCHY_STRING_ACCESSORS (OmarchyListRow, list_row, action_glyph, r_props, R_PROP_ACTION_GLYPH, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyListRow, list_row, action_tip, r_props, R_PROP_ACTION_TIP, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyListRow, list_row, action_urgent, r_props, R_PROP_ACTION_URGENT, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyListRow, list_row, action_cursor, r_props, R_PROP_ACTION_CURSOR, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyListRow, list_row, has_cursor, r_props, R_PROP_HAS_CURSOR, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyListRow, list_row, current, r_props, R_PROP_CURRENT, FALSE)

const char *
omarchy_list_row_get_title (OmarchyListRow *self)
{
  return self->title;
}

void
omarchy_list_row_set_title (OmarchyListRow *self, const char *title)
{
  if (!g_strcmp0 (self->title, title))
    return;
  g_free (self->title);
  self->title = g_strdup (title ? title : "");
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_LABEL, self->title, -1);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), r_props[R_PROP_TITLE]);
}

const char *
omarchy_list_row_get_status (OmarchyListRow *self)
{
  return self->status;
}

/**
 * omarchy_list_row_set_status:
 * @self: the row
 * @text: (nullable): the status line, or %NULL for none
 * @tone: (nullable): its tone: `accent`, `success`, `warning`, `urgent`, `muted` (also
 *   feedback's `info` and `error`), or %NULL/`""`/`neutral` for dim
 */
void
omarchy_list_row_set_status (OmarchyListRow *self, const char *text, const char *tone)
{
  omarchy_list_row_set_status_tone (self, tone && strcmp (tone, "neutral") ? tone : "");
  if (!g_strcmp0 (self->status, text ? text : ""))
    return;
  g_free (self->status);
  self->status = g_strdup (text ? text : "");
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), r_props[R_PROP_STATUS]);
}

int
omarchy_list_row_get_glyph_size (OmarchyListRow *self)
{
  return self->glyph_size;
}

/**
 * omarchy_list_row_set_glyph_size:
 * @self: the row
 * @px: the leading glyph's pixel size, or 0 for `font.title`
 */
void
omarchy_list_row_set_glyph_size (OmarchyListRow *self, int px)
{
  if (self->glyph_size == px)
    return;
  self->glyph_size = MAX (0, px);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), r_props[R_PROP_GLYPH_SIZE]);
}

/**
 * omarchy_list_row_set_glyph_color:
 * @self: the row
 * @color: (nullable): the leading glyph's color, or %NULL for the foreground
 */
void
omarchy_list_row_set_glyph_color (OmarchyListRow *self, const GdkRGBA *color)
{
  self->has_glyph_color = color != NULL;
  if (color)
    self->glyph_color = *color;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * omarchy_list_row_set_leading:
 * @self: the row
 * @leading: (nullable) (transfer floating): a widget leading the row instead of the glyph
 */
void
omarchy_list_row_set_leading (OmarchyListRow *self, GtkWidget *leading)
{
  row_set_child (self, &self->leading, leading, R_PROP_LEADING);
}

/**
 * omarchy_list_row_get_leading:
 * @self: the row
 *
 * Returns: (transfer none) (nullable): the leading widget
 */
GtkWidget *
omarchy_list_row_get_leading (OmarchyListRow *self)
{
  return self->leading;
}

/**
 * omarchy_list_row_set_trailing:
 * @self: the row
 * @trailing: (nullable) (transfer floating): a widget instead of the value
 */
void
omarchy_list_row_set_trailing (OmarchyListRow *self, GtkWidget *trailing)
{
  row_set_child (self, &self->trailing, trailing, R_PROP_TRAILING);
}

/**
 * omarchy_list_row_get_trailing:
 * @self: the row
 *
 * Returns: (transfer none) (nullable): the trailing widget
 */
GtkWidget *
omarchy_list_row_get_trailing (OmarchyListRow *self)
{
  return self->trailing;
}

/**
 * omarchy_list_row_set_expansion:
 * @self: the row
 * @expansion: (nullable) (transfer floating): a widget across the row under its main line
 */
void
omarchy_list_row_set_expansion (OmarchyListRow *self, GtkWidget *expansion)
{
  row_set_child (self, &self->expansion, expansion, R_PROP_EXPANSION);
}

/**
 * omarchy_list_row_get_expansion:
 * @self: the row
 *
 * Returns: (transfer none) (nullable): the expansion
 */
GtkWidget *
omarchy_list_row_get_expansion (OmarchyListRow *self)
{
  return self->expansion;
}

gboolean
omarchy_list_row_get_disabled (OmarchyListRow *self)
{
  return self->disabled;
}

void
omarchy_list_row_set_disabled (OmarchyListRow *self, gboolean disabled)
{
  if (self->disabled == !!disabled)
    return;
  self->disabled = !!disabled;
  gtk_widget_set_focusable (GTK_WIDGET (self), !disabled);
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), disabled ? NULL : "pointer");
  if (disabled)
    self->pointer_inside = FALSE;
  gtk_accessible_update_state (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_STATE_DISABLED, self->disabled, -1);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), r_props[R_PROP_DISABLED]);
}
