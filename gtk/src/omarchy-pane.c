/* omarchy-pane.c: Heading (Heading.qml), Pane (Pane.qml) and SettingRow (SettingRow.qml). */
#include "omarchy-private.h"

#include <math.h>

static GtkBuildableIface *pane_parent_buildable, *heading_parent_buildable, *row_parent_buildable;

/* =============================================================================== Heading === */

/**
 * OmarchyHeading:
 *
 * The title of a pane, toolbar, section or page: a bold line at the level's size (`title`,
 * `heading`, `display`), an optional `textDim` subtitle `xxs` under it, and trailing widgets
 * `controlGap` after the lines, `xxs` apart, centered against them. Lines elide.
 */
struct _OmarchyHeading {
  GtkWidget parent_instance;
  char *text, *subtitle, *level;
  gboolean centered;
};

enum { H_PROP_0, H_PROP_TEXT, H_PROP_SUBTITLE, H_PROP_LEVEL, H_PROP_CENTERED, H_N_PROPS };
static GParamSpec *h_props[H_N_PROPS];

static void heading_buildable_init (GtkBuildableIface *iface);
G_DEFINE_FINAL_TYPE_WITH_CODE (OmarchyHeading, omarchy_heading, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, heading_buildable_init))

static void
heading_sizes (OmarchyHeading *self, int *title, int *sub)
{
  const OmarchyTokens *t = omarchy_tokens ();
  if (!g_strcmp0 (self->level, "display")) {
    *title = t->display;
    *sub = t->subtitle;
  } else if (!g_strcmp0 (self->level, "heading")) {
    *title = t->heading;
    *sub = t->body;
  } else {
    *title = t->title;
    *sub = t->body_small;
  }
}

static gboolean
has_subtitle (OmarchyHeading *self)
{
  return self->subtitle && *self->subtitle;
}

static int
labels_height (OmarchyHeading *self)
{
  int ts, ss;
  heading_sizes (self, &ts, &ss);
  return omarchy_line_height (ts) + (has_subtitle (self) ? omarchy_tokens ()->xxs + omarchy_line_height (ss) : 0);
}

static void
trailing_size (OmarchyHeading *self, int *w, int *h)
{
  *w = *h = 0;
  int n = 0;
  for (GtkWidget *c = gtk_widget_get_first_child (GTK_WIDGET (self)); c; c = gtk_widget_get_next_sibling (c)) {
    if (!gtk_widget_get_visible (c))
      continue;
    int cw, ch;
    gtk_widget_measure (c, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &cw, NULL, NULL);
    gtk_widget_measure (c, GTK_ORIENTATION_VERTICAL, -1, NULL, &ch, NULL, NULL);
    *w += cw + (n++ ? omarchy_tokens ()->xxs : 0);
    *h = MAX (*h, ch);
  }
}

static void
heading_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyHeading *self = OMARCHY_HEADING (w);
  int tw, th;
  trailing_size (self, &tw, &th);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    int ts, ss, a = 0, b = 0;
    heading_sizes (self, &ts, &ss);
    /* Whole pixels up, so the lines never elide at their natural width. */
    PangoRectangle r;
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->text, ts, TRUE);
    pango_layout_get_extents (l, NULL, &r);
    a = (int) ceil (r.width / (double) PANGO_SCALE);
    if (has_subtitle (self)) {
      g_autoptr (PangoLayout) s = omarchy_layout (w, self->subtitle, ss, FALSE);
      pango_layout_get_extents (s, NULL, &r);
      b = (int) ceil (r.width / (double) PANGO_SCALE);
    }
    int inset = tw > 0 ? tw + omarchy_tokens ()->control_gap : 0;
    *min = inset;
    *nat = MAX (a, b) + inset;
  } else {
    *min = *nat = MAX (labels_height (self), th);
  }
}

static void
heading_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyHeading *self = OMARCHY_HEADING (w);
  int tw, th;
  trailing_size (self, &tw, &th);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  int x = rtl ? 0 : width - tw;
  for (GtkWidget *c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c)) {
    if (!gtk_widget_get_visible (c))
      continue;
    int cw, ch;
    gtk_widget_measure (c, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &cw, NULL, NULL);
    gtk_widget_measure (c, GTK_ORIENTATION_VERTICAL, -1, NULL, &ch, NULL, NULL);
    GtkAllocation a = { x, (int) lround ((height - ch) / 2.0), cw, ch };
    gtk_widget_size_allocate (c, &a, -1);
    x += cw + omarchy_tokens ()->xxs;
  }
}

static void
heading_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyHeading *self = OMARCHY_HEADING (w);
  const OmarchyTokens *p = omarchy_paint ();
  int tw, th;
  trailing_size (self, &tw, &th);
  int inset = tw > 0 ? tw + p->control_gap : 0;
  int width = MAX (1, gtk_widget_get_width (w) - inset);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  float x0 = rtl ? inset : 0;
  int ts, ss;
  heading_sizes (self, &ts, &ss);
  float y = roundf ((gtk_widget_get_height (w) - labels_height (self)) / 2.0f);
  PangoAlignment align = self->centered ? PANGO_ALIGN_CENTER : rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
  g_autoptr (PangoLayout) l = omarchy_layout (w, self->text, ts, TRUE);
  pango_layout_set_width (l, width * PANGO_SCALE);
  pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
  pango_layout_set_alignment (l, align);
  omarchy_draw_layout (s, l, x0, y, &p->foreground);
  if (has_subtitle (self)) {
    g_autoptr (PangoLayout) sub = omarchy_layout (w, self->subtitle, ss, FALSE);
    pango_layout_set_width (sub, width * PANGO_SCALE);
    pango_layout_set_ellipsize (sub, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment (sub, align);
    omarchy_draw_layout (s, sub, x0, y + omarchy_line_height (ts) + p->xxs, &p->text_dim);
  }
  for (GtkWidget *c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c))
    gtk_widget_snapshot_child (w, c, s);
}

static void
omarchy_heading_init (OmarchyHeading *self)
{
  self->text = g_strdup ("");
  self->subtitle = g_strdup ("");
  self->level = g_strdup ("title");
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
heading_dispose (GObject *o)
{
  omarchy_widget_dispose_children (GTK_WIDGET (o));
  G_OBJECT_CLASS (omarchy_heading_parent_class)->dispose (o);
}

static void
heading_finalize (GObject *o)
{
  OmarchyHeading *self = OMARCHY_HEADING (o);
  g_free (self->text);
  g_free (self->subtitle);
  g_free (self->level);
  G_OBJECT_CLASS (omarchy_heading_parent_class)->finalize (o);
}

static void
heading_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyHeading *self = OMARCHY_HEADING (o);
  switch (id) {
  case H_PROP_TEXT: omarchy_heading_set_text (self, g_value_get_string (v)); break;
  case H_PROP_SUBTITLE: omarchy_heading_set_subtitle (self, g_value_get_string (v)); break;
  case H_PROP_LEVEL: omarchy_heading_set_level (self, g_value_get_string (v)); break;
  case H_PROP_CENTERED: omarchy_heading_set_centered (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
heading_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyHeading *self = OMARCHY_HEADING (o);
  switch (id) {
  case H_PROP_TEXT: g_value_set_string (v, self->text); break;
  case H_PROP_SUBTITLE: g_value_set_string (v, self->subtitle); break;
  case H_PROP_LEVEL: g_value_set_string (v, self->level); break;
  case H_PROP_CENTERED: g_value_set_boolean (v, self->centered); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
heading_add_child (GtkBuildable *b, GtkBuilder *builder, GObject *child, const char *type)
{
  if (GTK_IS_WIDGET (child))
    omarchy_heading_add_trailing (OMARCHY_HEADING (b), GTK_WIDGET (child));
  else
    heading_parent_buildable->add_child (b, builder, child, type);
}

static void
heading_buildable_init (GtkBuildableIface *iface)
{
  heading_parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = heading_add_child;
}

static void
omarchy_heading_class_init (OmarchyHeadingClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = heading_dispose;
  oc->finalize = heading_finalize;
  oc->set_property = heading_set_property;
  oc->get_property = heading_get_property;
  wc->measure = heading_measure;
  wc->size_allocate = heading_allocate;
  wc->snapshot = heading_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  h_props[H_PROP_TEXT] = g_param_spec_string ("text", NULL, NULL, "", f);
  h_props[H_PROP_SUBTITLE] = g_param_spec_string ("subtitle", NULL, NULL, "", f);
  /**
   * OmarchyHeading:level:
   *
   * `title` (panes, toolbars), `heading` (sections) or `display` (pages).
   */
  h_props[H_PROP_LEVEL] = g_param_spec_string ("level", NULL, NULL, "title", f);
  h_props[H_PROP_CENTERED] = g_param_spec_boolean ("centered", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, H_N_PROPS, h_props);
  gtk_widget_class_set_css_name (wc, "omarchy-heading");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_HEADING);
}

/**
 * omarchy_heading_new:
 * @text: (nullable): the title
 *
 * Returns: (transfer floating): a heading at the `title` level
 */
GtkWidget *
omarchy_heading_new (const char *text)
{
  return g_object_new (OMARCHY_TYPE_HEADING, "text", text ? text : "", NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyHeading, heading, text, h_props, H_PROP_TEXT, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyHeading, heading, subtitle, h_props, H_PROP_SUBTITLE, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyHeading, heading, level, h_props, H_PROP_LEVEL, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyHeading, heading, centered, h_props, H_PROP_CENTERED, FALSE)

/**
 * omarchy_heading_add_trailing:
 * @self: the heading
 * @child: (transfer floating): an action at the end (an IconButton, a Button, a switch)
 */
void
omarchy_heading_add_trailing (OmarchyHeading *self, GtkWidget *child)
{
  gtk_widget_set_parent (child, GTK_WIDGET (self));
}

/* ================================================================================== Pane === */

/**
 * OmarchyPane:
 *
 * A flat surface inside an app window (inspector, side panel, settings group): `panelPadding`
 * inside, a Heading with actions, children `panelGap` apart. Plain by default; a `hairline`
 * track rule on one #OmarchyPane:divider side, or the controls' normal glass when
 * #OmarchyPane:bordered. Never the popups border, never a shadow.
 */
struct _OmarchyPane {
  GtkWidget parent_instance;
  GtkWidget *box, *heading;
  gboolean bordered;
  char *divider;
};

enum { P_PROP_0, P_PROP_BORDERED, P_PROP_DIVIDER, P_N_PROPS };
static GParamSpec *p_props[P_N_PROPS];

static void pane_buildable_init (GtkBuildableIface *iface);
G_DEFINE_FINAL_TYPE_WITH_CODE (OmarchyPane, omarchy_pane, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, pane_buildable_init))

static void
pane_insets (OmarchyPane *self, float in[4])
{
  const OmarchyTokens *t = omarchy_tokens ();
  static const char *sides[4] = { "top", "right", "bottom", "left" };
  for (int i = 0; i < 4; i++) {
    in[i] = t->panel_padding + (self->bordered ? t->normal.border.widths[i] : 0);
    if (!g_strcmp0 (self->divider, sides[i]))
      in[i] += t->hairline;
  }
}

static void
pane_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyPane *self = OMARCHY_PANE (w);
  float in[4];
  pane_insets (self, in);
  int extra = o == GTK_ORIENTATION_HORIZONTAL ? (int) (in[1] + in[3]) : (int) (in[0] + in[2]);
  int other = o == GTK_ORIENTATION_HORIZONTAL ? (int) (in[0] + in[2]) : (int) (in[1] + in[3]);
  gtk_widget_measure (self->box, o, for_size >= 0 ? MAX (0, for_size - other) : -1, min, nat, NULL, NULL);
  *min += extra;
  *nat += extra;
}

static void
pane_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyPane *self = OMARCHY_PANE (w);
  float in[4];
  pane_insets (self, in);
  GtkAllocation a = { (int) in[3], (int) in[0], MAX (0, width - (int) (in[1] + in[3])), MAX (0, height - (int) (in[0] + in[2])) };
  gtk_widget_size_allocate (self->box, &a, -1);
}

static void
pane_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyPane *self = OMARCHY_PANE (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  if (self->bordered)
    omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), p->radius, &p->normal.fill, &p->normal.border);
  float r = p->hairline;
  if (!g_strcmp0 (self->divider, "left"))
    gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, 0, r, H));
  else if (!g_strcmp0 (self->divider, "right"))
    gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (W - r, 0, r, H));
  else if (!g_strcmp0 (self->divider, "top"))
    gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, 0, W, r));
  else if (!g_strcmp0 (self->divider, "bottom"))
    gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, H - r, W, r));
  gtk_widget_snapshot_child (w, self->box, s);
}

static void
pane_restyle (OmarchyPane *self)
{
  gtk_box_set_spacing (GTK_BOX (self->box), omarchy_tokens ()->panel_gap);
}

static void
omarchy_pane_init (OmarchyPane *self)
{
  self->divider = g_strdup ("");
  self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_parent (self->box, GTK_WIDGET (self));
  self->heading = omarchy_heading_new ("");
  gtk_widget_set_visible (self->heading, FALSE);
  gtk_box_append (GTK_BOX (self->box), self->heading);
  pane_restyle (self);
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (pane_restyle), self, G_CONNECT_SWAPPED);
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
pane_dispose (GObject *o)
{
  OmarchyPane *self = OMARCHY_PANE (o);
  g_clear_pointer (&self->box, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_pane_parent_class)->dispose (o);
}

static void
pane_finalize (GObject *o)
{
  g_free (OMARCHY_PANE (o)->divider);
  G_OBJECT_CLASS (omarchy_pane_parent_class)->finalize (o);
}

static void
pane_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyPane *self = OMARCHY_PANE (o);
  switch (id) {
  case P_PROP_BORDERED: omarchy_pane_set_bordered (self, g_value_get_boolean (v)); break;
  case P_PROP_DIVIDER: omarchy_pane_set_divider (self, g_value_get_string (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
pane_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyPane *self = OMARCHY_PANE (o);
  switch (id) {
  case P_PROP_BORDERED: g_value_set_boolean (v, self->bordered); break;
  case P_PROP_DIVIDER: g_value_set_string (v, self->divider); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
pane_add_child (GtkBuildable *b, GtkBuilder *builder, GObject *child, const char *type)
{
  if (GTK_IS_WIDGET (child) && !g_strcmp0 (type, "action"))
    omarchy_pane_add_action (OMARCHY_PANE (b), GTK_WIDGET (child));
  else if (GTK_IS_WIDGET (child))
    omarchy_pane_append (OMARCHY_PANE (b), GTK_WIDGET (child));
  else
    pane_parent_buildable->add_child (b, builder, child, type);
}

static void
pane_buildable_init (GtkBuildableIface *iface)
{
  pane_parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = pane_add_child;
}

static void
omarchy_pane_class_init (OmarchyPaneClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = pane_dispose;
  oc->finalize = pane_finalize;
  oc->set_property = pane_set_property;
  oc->get_property = pane_get_property;
  wc->measure = pane_measure;
  wc->size_allocate = pane_allocate;
  wc->snapshot = pane_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  p_props[P_PROP_BORDERED] = g_param_spec_boolean ("bordered", NULL, NULL, FALSE, f);
  /**
   * OmarchyPane:divider:
   *
   * `""` (none), `top`, `right`, `bottom` or `left`: the side the rule is on.
   */
  p_props[P_PROP_DIVIDER] = g_param_spec_string ("divider", NULL, NULL, "", f);
  g_object_class_install_properties (oc, P_N_PROPS, p_props);
  gtk_widget_class_set_css_name (wc, "omarchy-pane");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_pane_new:
 * @title: (nullable): the heading, or %NULL for none
 *
 * Returns: (transfer floating): a plain pane
 */
GtkWidget *
omarchy_pane_new (const char *title)
{
  OmarchyPane *self = g_object_new (OMARCHY_TYPE_PANE, NULL);
  omarchy_pane_set_title (self, title);
  return GTK_WIDGET (self);
}

void
omarchy_pane_set_title (OmarchyPane *self, const char *title)
{
  omarchy_heading_set_text (OMARCHY_HEADING (self->heading), title);
  gtk_widget_set_visible (self->heading, title && *title);
}

void
omarchy_pane_set_subtitle (OmarchyPane *self, const char *subtitle)
{
  omarchy_heading_set_subtitle (OMARCHY_HEADING (self->heading), subtitle);
}

/**
 * omarchy_pane_add_action:
 * @self: the pane
 * @action: (transfer floating): an action at the end of the heading
 */
void
omarchy_pane_add_action (OmarchyPane *self, GtkWidget *action)
{
  omarchy_heading_add_trailing (OMARCHY_HEADING (self->heading), action);
}

/**
 * omarchy_pane_append:
 * @self: the pane
 * @child: (transfer floating): content, stacked below the rest
 */
void
omarchy_pane_append (OmarchyPane *self, GtkWidget *child)
{
  gtk_box_append (GTK_BOX (self->box), child);
}

OMARCHY_BOOL_ACCESSORS (OmarchyPane, pane, bordered, p_props, P_PROP_BORDERED, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyPane, pane, divider, p_props, P_PROP_DIVIDER, TRUE)

/* ============================================================================ SettingRow === */

/**
 * OmarchySettingRow:
 *
 * One row of a settings list: a label (`body`) and an optional `textDim` description on the
 * left, controls on the right `md` apart. `rowPaddingX` × `md` inside, at least
 * `controlHeight` tall. #OmarchySettingRow:cursor paints the hover fill.
 */
struct _OmarchySettingRow {
  GtkWidget parent_instance;
  GtkWidget *trailing;
  char *label, *description;
  gboolean cursor;
};

enum { R_PROP_0, R_PROP_LABEL, R_PROP_DESCRIPTION, R_PROP_CURSOR, R_N_PROPS };
static GParamSpec *r_props[R_N_PROPS];

static void row_buildable_init (GtkBuildableIface *iface);
G_DEFINE_FINAL_TYPE_WITH_CODE (OmarchySettingRow, omarchy_setting_row, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, row_buildable_init))

static int
row_words_width (OmarchySettingRow *self, int width)
{
  const OmarchyTokens *t = omarchy_tokens ();
  int tw;
  gtk_widget_measure (self->trailing, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &tw, NULL, NULL);
  return MAX (1, width - t->row_padding_x * 2 - tw - t->xxl);
}

static int
row_words_height (OmarchySettingRow *self, int words_w)
{
  const OmarchyTokens *t = omarchy_tokens ();
  int h = omarchy_line_height (t->body);
  if (self->description && *self->description) {
    g_autoptr (PangoLayout) l = omarchy_layout (GTK_WIDGET (self), self->description, t->caption, FALSE);
    if (words_w < 100000)
      pango_layout_set_width (l, words_w * PANGO_SCALE);
    pango_layout_set_wrap (l, PANGO_WRAP_WORD_CHAR);
    int dh = omarchy_line_height (t->caption);
    if (pango_layout_get_line_count (l) > 1) {
      PangoRectangle r;
      pango_layout_get_extents (l, NULL, &r);
      dh = (int) ceil (r.height / (double) PANGO_SCALE);
    }
    h += t->xxs + dh;
  }
  return h;
}

static void
row_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchySettingRow *self = OMARCHY_SETTING_ROW (w);
  const OmarchyTokens *t = omarchy_tokens ();
  int tw, th;
  gtk_widget_measure (self->trailing, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &tw, NULL, NULL);
  gtk_widget_measure (self->trailing, GTK_ORIENTATION_VERTICAL, -1, NULL, &th, NULL, NULL);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->label, t->body, FALSE);
    int lw;
    omarchy_layout_size (l, &lw, NULL);
    *min = t->row_padding_x * 2 + tw + t->xxl;
    *nat = MAX (omarchy_space (240), *min + lw);
  } else {
    /* Unconstrained, the description takes one line: a parent laying rows out across measures
     * them at their real width (GTK asks for width-for-height below this). */
    int width = for_size >= 0 ? for_size : G_MAXINT / 4;
    int wh = row_words_height (self, row_words_width (self, width));
    *min = *nat = MAX (MAX (t->control_height + t->md * 2, wh + t->md * 2), th + t->xxs * 2);
  }
}

static GtkSizeRequestMode
row_request_mode (GtkWidget *w)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
row_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchySettingRow *self = OMARCHY_SETTING_ROW (w);
  const OmarchyTokens *t = omarchy_tokens ();
  int tw, th;
  gtk_widget_measure (self->trailing, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &tw, NULL, NULL);
  gtk_widget_measure (self->trailing, GTK_ORIENTATION_VERTICAL, -1, NULL, &th, NULL, NULL);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  GtkAllocation a = { rtl ? t->row_padding_x : width - t->row_padding_x - tw, (int) lround ((height - th) / 2.0), tw, th };
  gtk_widget_size_allocate (self->trailing, &a, -1);
}

static void
row_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchySettingRow *self = OMARCHY_SETTING_ROW (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  if (self->cursor)
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (0, 0, W, H), p->radius, &p->hover.fill);
  gboolean enabled = gtk_widget_is_sensitive (w);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  int ww = row_words_width (self, (int) W);
  float x = rtl ? W - p->row_padding_x - ww : p->row_padding_x;
  float y = roundf ((H - row_words_height (self, ww)) / 2.0f);
  g_autoptr (PangoLayout) l = omarchy_layout (w, self->label, p->body, FALSE);
  pango_layout_set_width (l, ww * PANGO_SCALE);
  pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
  if (rtl)
    pango_layout_set_alignment (l, PANGO_ALIGN_RIGHT);
  omarchy_draw_layout (s, l, x, y, enabled ? &p->foreground : &p->text_faint);
  if (self->description && *self->description) {
    g_autoptr (PangoLayout) d = omarchy_layout (w, self->description, p->caption, FALSE);
    pango_layout_set_width (d, ww * PANGO_SCALE);
    pango_layout_set_wrap (d, PANGO_WRAP_WORD_CHAR);
    if (rtl)
      pango_layout_set_alignment (d, PANGO_ALIGN_RIGHT);
    omarchy_draw_layout (s, d, x, y + omarchy_line_height (p->body) + p->xxs, enabled ? &p->text_dim : &p->text_faint);
  }
  gtk_widget_snapshot_child (w, self->trailing, s);
}

static void
row_restyle (OmarchySettingRow *self)
{
  gtk_box_set_spacing (GTK_BOX (self->trailing), omarchy_tokens ()->md);
}

static void
omarchy_setting_row_init (OmarchySettingRow *self)
{
  self->label = g_strdup ("");
  self->description = g_strdup ("");
  self->trailing = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_parent (self->trailing, GTK_WIDGET (self));
  row_restyle (self);
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (row_restyle), self, G_CONNECT_SWAPPED);
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
row_dispose (GObject *o)
{
  OmarchySettingRow *self = OMARCHY_SETTING_ROW (o);
  g_clear_pointer (&self->trailing, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_setting_row_parent_class)->dispose (o);
}

static void
row_finalize (GObject *o)
{
  OmarchySettingRow *self = OMARCHY_SETTING_ROW (o);
  g_free (self->label);
  g_free (self->description);
  G_OBJECT_CLASS (omarchy_setting_row_parent_class)->finalize (o);
}

static void
row_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchySettingRow *self = OMARCHY_SETTING_ROW (o);
  switch (id) {
  case R_PROP_LABEL: omarchy_setting_row_set_label (self, g_value_get_string (v)); break;
  case R_PROP_DESCRIPTION: omarchy_setting_row_set_description (self, g_value_get_string (v)); break;
  case R_PROP_CURSOR: omarchy_setting_row_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
row_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchySettingRow *self = OMARCHY_SETTING_ROW (o);
  switch (id) {
  case R_PROP_LABEL: g_value_set_string (v, self->label); break;
  case R_PROP_DESCRIPTION: g_value_set_string (v, self->description); break;
  case R_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
row_add_child (GtkBuildable *b, GtkBuilder *builder, GObject *child, const char *type)
{
  if (GTK_IS_WIDGET (child))
    omarchy_setting_row_add_control (OMARCHY_SETTING_ROW (b), GTK_WIDGET (child));
  else
    row_parent_buildable->add_child (b, builder, child, type);
}

static void
row_buildable_init (GtkBuildableIface *iface)
{
  row_parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = row_add_child;
}

static void
omarchy_setting_row_class_init (OmarchySettingRowClass *klass)
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
  wc->get_request_mode = row_request_mode;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  r_props[R_PROP_LABEL] = g_param_spec_string ("label", NULL, NULL, "", f);
  r_props[R_PROP_DESCRIPTION] = g_param_spec_string ("description", NULL, NULL, "", f);
  r_props[R_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, R_N_PROPS, r_props);
  gtk_widget_class_set_css_name (wc, "omarchy-setting-row");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_setting_row_new:
 * @label: (nullable): the setting's name
 * @description: (nullable): a line below it
 * @control: (nullable) (transfer floating): the control at the end
 *
 * Returns: (transfer floating): a setting row
 */
GtkWidget *
omarchy_setting_row_new (const char *label, const char *description, GtkWidget *control)
{
  OmarchySettingRow *self = g_object_new (OMARCHY_TYPE_SETTING_ROW, "label", label ? label : "", "description",
                                          description ? description : "", NULL);
  if (control)
    omarchy_setting_row_add_control (self, control);
  return GTK_WIDGET (self);
}

OMARCHY_STRING_ACCESSORS (OmarchySettingRow, setting_row, label, r_props, R_PROP_LABEL, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchySettingRow, setting_row, description, r_props, R_PROP_DESCRIPTION, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchySettingRow, setting_row, cursor, r_props, R_PROP_CURSOR, FALSE)

/**
 * omarchy_setting_row_add_control:
 * @self: the row
 * @control: (transfer floating): a control at the end, after any others
 */
void
omarchy_setting_row_add_control (OmarchySettingRow *self, GtkWidget *control)
{
  gtk_widget_set_valign (control, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (self->trailing), control);
}
