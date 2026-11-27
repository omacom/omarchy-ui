/* omarchy-lists-views.c: ScrollArea (ScrollArea.qml), KeyValue (KeyValue.qml) and Hero (Hero.qml),
 * drawn as the Qt kit draws them. */
#include "omarchy-lists-private.h"

#include <math.h>
#include <string.h>

/* ============================================================================ ScrollArea === */

/**
 * OmarchyScrollArea:
 *
 * A vertical scroll view capped at #OmarchyScrollArea:max-height, its children stacked
 * #OmarchyScrollArea:spacing apart and as wide as the area, with Omarchy's thin scrollbar over
 * its right edge while the content overflows: a `track` groove `scrollbarThickness` wide and a
 * thumb in the normal border color (`textDim` under the pointer or while dragged), corners
 * `min(4, radius)`. Nothing is reserved for the bar, so nothing reflows when it appears; rows
 * keep clear of it with their own padding. Scrolling stops at the ends; the thumb drags.
 * Page Up/Down, Home/End and Up/Down scroll it while it has focus (unless a
 * #OmarchyCursorModel is attached to it, which takes the keys).
 */
struct _OmarchyScrollArea {
  GtkWidget parent_instance;
  GtkWidget *box;
  GtkAdjustment *adj;
  int max_height; /* -1: none */
  int spacing;
  char *spacing_token;
  gboolean bar_hovered, dragging, overflowing;
  double drag_value;
};

enum { S_PROP_0, S_PROP_MAX_HEIGHT, S_PROP_SPACING, S_PROP_OVERFLOWING, S_PROP_VADJUSTMENT, S_N_PROPS };
static GParamSpec *s_props[S_N_PROPS];

static void scroll_buildable_init (GtkBuildableIface *iface);
G_DEFINE_FINAL_TYPE_WITH_CODE (OmarchyScrollArea, omarchy_scroll_area, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, scroll_buildable_init))

static int
bar_thickness (void)
{
  return omarchy_tokens ()->scrollbar_thickness;
}

static void
scroll_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyScrollArea *self = OMARCHY_SCROLL_AREA (w);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    gtk_widget_measure (self->box, o, -1, min, nat, NULL, NULL);
    return;
  }
  int bmin, bnat;
  gtk_widget_measure (self->box, o, for_size, &bmin, &bnat, NULL, NULL);
  int cap = self->max_height >= 0 ? self->max_height : G_MAXINT;
  *nat = MIN (bnat, cap);
  *min = MIN (bmin, *nat);
}

static void
scroll_set_overflowing (OmarchyScrollArea *self, gboolean on)
{
  if (self->overflowing == on)
    return;
  self->overflowing = on;
  g_object_notify_by_pspec (G_OBJECT (self), s_props[S_PROP_OVERFLOWING]);
}

static void
scroll_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyScrollArea *self = OMARCHY_SCROLL_AREA (w);
  int content;
  gtk_widget_measure (self->box, GTK_ORIENTATION_VERTICAL, width, NULL, &content, NULL, NULL);
  content = MAX (content, height);
  double value = CLAMP (gtk_adjustment_get_value (self->adj), 0, MAX (0, content - height));
  gtk_adjustment_configure (self->adj, value, 0, content, omarchy_line_height (omarchy_tokens ()->body) * 3,
                            height * 0.9, height);
  scroll_set_overflowing (self, content > height + 0.5);
  GtkAllocation a = { 0, (int) -round (value), width, content };
  gtk_widget_size_allocate (self->box, &a, -1);
}

/* QQuickScrollBar's visual area: the thumb's position and size as shares of the bar. */
static void
scroll_thumb (OmarchyScrollArea *self, double *pos, double *size)
{
  double upper = gtk_adjustment_get_upper (self->adj), page = gtk_adjustment_get_page_size (self->adj);
  double h = gtk_widget_get_height (GTK_WIDGET (self));
  double s = upper > 0 ? MIN (1, page / upper) : 1;
  double position = upper > 0 ? gtk_adjustment_get_value (self->adj) / upper : 0;
  double minimum = h > 0 ? omarchy_space (24) / MAX (1.0, h) : 0;
  double vp = position;
  if (minimum > s)
    vp = position / (1.0 - s) * (1.0 - minimum);
  double vs = CLAMP (MAX (s, minimum) + MIN (0, vp), 0, MAX (0.0, 1.0 - vp));
  vp = CLAMP (vp, 0, 1.0 - vs);
  *pos = vp;
  *size = vs;
}

static void
scroll_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyScrollArea *self = OMARCHY_SCROLL_AREA (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gtk_snapshot_push_clip (s, &GRAPHENE_RECT_INIT (0, 0, W, H));
  gtk_widget_snapshot_child (w, self->box, s);
  gtk_snapshot_pop (s);
  if (!self->overflowing)
    return;
  float th = bar_thickness ();
  float r = MIN (4, p->radius);
  omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (W - th, 0, th, H), r, &p->track);
  double pos, size;
  scroll_thumb (self, &pos, &size);
  const GdkRGBA *c = self->dragging || self->bar_hovered ? &p->text_dim : &p->normal.border.color;
  omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (W - th, pos * H, th, size * H), r, c);
}

static gboolean
scroll_in_bar (OmarchyScrollArea *self, double x, double y)
{
  float W = gtk_widget_get_width (GTK_WIDGET (self));
  return self->overflowing && x >= W - bar_thickness () && x <= W && y >= 0;
}

static void
scroll_motion (GtkEventControllerMotion *c, double x, double y, OmarchyScrollArea *self)
{
  gboolean on = scroll_in_bar (self, x, y);
  if (on != self->bar_hovered) {
    self->bar_hovered = on;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
scroll_leave (GtkEventControllerMotion *c, OmarchyScrollArea *self)
{
  if (self->bar_hovered) {
    self->bar_hovered = FALSE;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
scroll_to (OmarchyScrollArea *self, double value)
{
  double upper = gtk_adjustment_get_upper (self->adj), page = gtk_adjustment_get_page_size (self->adj);
  gtk_adjustment_set_value (self->adj, CLAMP (value, 0, MAX (0, upper - page)));
}

static void
drag_begin (GtkGestureDrag *g, double x, double y, OmarchyScrollArea *self)
{
  if (!scroll_in_bar (self, x, y)) {
    gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_DENIED);
    return;
  }
  gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_CLAIMED);
  double pos, size, H = gtk_widget_get_height (GTK_WIDGET (self));
  scroll_thumb (self, &pos, &size);
  /* On the groove: a page toward the press; on the thumb: drag it. */
  if (y < pos * H)
    scroll_to (self, gtk_adjustment_get_value (self->adj) - gtk_adjustment_get_page_size (self->adj));
  else if (y > (pos + size) * H)
    scroll_to (self, gtk_adjustment_get_value (self->adj) + gtk_adjustment_get_page_size (self->adj));
  self->dragging = TRUE;
  self->drag_value = gtk_adjustment_get_value (self->adj);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
drag_update (GtkGestureDrag *g, double dx, double dy, OmarchyScrollArea *self)
{
  if (!self->dragging)
    return;
  double H = gtk_widget_get_height (GTK_WIDGET (self));
  double upper = gtk_adjustment_get_upper (self->adj), page = gtk_adjustment_get_page_size (self->adj);
  double pos, size;
  scroll_thumb (self, &pos, &size);
  double s = upper > 0 ? MIN (1, page / upper) : 1;
  /* The thumb's travel maps onto the content's. */
  double travel = (1.0 - size) * H, range = (1.0 - s) * upper;
  if (travel > 0)
    scroll_to (self, self->drag_value + dy / travel * range);
}

static void
drag_end (GtkGestureDrag *g, double dx, double dy, OmarchyScrollArea *self)
{
  self->dragging = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static gboolean
scroll_wheel (GtkEventControllerScroll *c, double dx, double dy, OmarchyScrollArea *self)
{
  if (!self->overflowing)
    return FALSE;
  double step = gtk_event_controller_scroll_get_unit (c) == GDK_SCROLL_UNIT_WHEEL ? gtk_adjustment_get_step_increment (self->adj)
                                                                                    : 1.0;
  double before = gtk_adjustment_get_value (self->adj);
  scroll_to (self, before + dy * step);
  return gtk_adjustment_get_value (self->adj) != before || dy == 0;
}

static gboolean
scroll_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyScrollArea *self = OMARCHY_SCROLL_AREA (w);
  if (g_object_get_data (G_OBJECT (w), "omarchy-cursor-model"))
    return FALSE;
  double v = gtk_adjustment_get_value (self->adj), page = gtk_adjustment_get_page_size (self->adj);
  double step = gtk_adjustment_get_step_increment (self->adj);
  switch (keyval) {
  case GDK_KEY_Page_Down: scroll_to (self, v + page); return TRUE;
  case GDK_KEY_Page_Up: scroll_to (self, v - page); return TRUE;
  case GDK_KEY_Down: case GDK_KEY_j: scroll_to (self, v + step); return TRUE;
  case GDK_KEY_Up: case GDK_KEY_k: scroll_to (self, v - step); return TRUE;
  case GDK_KEY_Home: scroll_to (self, 0); return TRUE;
  case GDK_KEY_End: scroll_to (self, gtk_adjustment_get_upper (self->adj)); return TRUE;
  default: return FALSE;
  }
}

static void
scroll_changed (OmarchyScrollArea *self)
{
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
scroll_restyle (OmarchyScrollArea *self)
{
  if (self->spacing_token)
    gtk_box_set_spacing (GTK_BOX (self->box), omarchy_theme_get_spacing (omarchy_theme_get_default (), self->spacing_token));
}

static void
omarchy_scroll_area_init (OmarchyScrollArea *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->max_height = -1;
  self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_parent (self->box, w);
  self->adj = g_object_ref_sink (gtk_adjustment_new (0, 0, 0, 1, 1, 0));
  g_signal_connect_object (self->adj, "value-changed", G_CALLBACK (scroll_changed), self, G_CONNECT_SWAPPED);
  omarchy_widget_follow (w);
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (scroll_restyle), self, G_CONNECT_SWAPPED);
  gtk_widget_set_overflow (w, GTK_OVERFLOW_HIDDEN);

  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "motion", G_CALLBACK (scroll_motion), self);
  g_signal_connect (m, "enter", G_CALLBACK (scroll_motion), self);
  g_signal_connect (m, "leave", G_CALLBACK (scroll_leave), self);
  gtk_widget_add_controller (w, m);

  GtkGesture *drag = gtk_gesture_drag_new ();
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (drag), GTK_PHASE_CAPTURE);
  g_signal_connect (drag, "drag-begin", G_CALLBACK (drag_begin), self);
  g_signal_connect (drag, "drag-update", G_CALLBACK (drag_update), self);
  g_signal_connect (drag, "drag-end", G_CALLBACK (drag_end), self);
  gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (drag));

  GtkEventController *wheel = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect (wheel, "scroll", G_CALLBACK (scroll_wheel), self);
  gtk_widget_add_controller (w, wheel);

  omarchy_widget_add_keys (w, scroll_key);
}

static void
scroll_dispose (GObject *o)
{
  OmarchyScrollArea *self = OMARCHY_SCROLL_AREA (o);
  g_clear_pointer (&self->box, gtk_widget_unparent);
  g_clear_object (&self->adj);
  G_OBJECT_CLASS (omarchy_scroll_area_parent_class)->dispose (o);
}

static void
scroll_finalize (GObject *o)
{
  g_free (OMARCHY_SCROLL_AREA (o)->spacing_token);
  G_OBJECT_CLASS (omarchy_scroll_area_parent_class)->finalize (o);
}

static void
scroll_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyScrollArea *self = OMARCHY_SCROLL_AREA (o);
  switch (id) {
  case S_PROP_MAX_HEIGHT: omarchy_scroll_area_set_max_height (self, g_value_get_int (v)); break;
  case S_PROP_SPACING: omarchy_scroll_area_set_spacing (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
scroll_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyScrollArea *self = OMARCHY_SCROLL_AREA (o);
  switch (id) {
  case S_PROP_MAX_HEIGHT: g_value_set_int (v, self->max_height); break;
  case S_PROP_SPACING: g_value_set_int (v, omarchy_scroll_area_get_spacing (self)); break;
  case S_PROP_OVERFLOWING: g_value_set_boolean (v, self->overflowing); break;
  case S_PROP_VADJUSTMENT: g_value_set_object (v, self->adj); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static GtkBuildableIface *scroll_parent_buildable;

static void
scroll_add_child (GtkBuildable *b, GtkBuilder *builder, GObject *child, const char *type)
{
  if (GTK_IS_WIDGET (child))
    omarchy_scroll_area_append (OMARCHY_SCROLL_AREA (b), GTK_WIDGET (child));
  else
    scroll_parent_buildable->add_child (b, builder, child, type);
}

static void
scroll_buildable_init (GtkBuildableIface *iface)
{
  scroll_parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = scroll_add_child;
}

static void
omarchy_scroll_area_class_init (OmarchyScrollAreaClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = scroll_dispose;
  oc->finalize = scroll_finalize;
  oc->set_property = scroll_set_property;
  oc->get_property = scroll_get_property;
  wc->measure = scroll_measure;
  wc->size_allocate = scroll_allocate;
  wc->snapshot = scroll_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyScrollArea:max-height:
   *
   * The most the area grows to before it scrolls, or -1 for no cap.
   */
  s_props[S_PROP_MAX_HEIGHT] = g_param_spec_int ("max-height", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyScrollArea:spacing:
   *
   * Between children.
   */
  s_props[S_PROP_SPACING] = g_param_spec_int ("spacing", NULL, NULL, 0, G_MAXINT, 0, f);
  /**
   * OmarchyScrollArea:overflowing:
   *
   * The content is taller than the area (the bar shows).
   */
  s_props[S_PROP_OVERFLOWING] = g_param_spec_boolean ("overflowing", NULL, NULL, FALSE,
                                                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * OmarchyScrollArea:vadjustment:
   *
   * The scroll offset.
   */
  s_props[S_PROP_VADJUSTMENT] = g_param_spec_object ("vadjustment", NULL, NULL, GTK_TYPE_ADJUSTMENT,
                                                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (oc, S_N_PROPS, s_props);
  gtk_widget_class_set_css_name (wc, "omarchy-scroll-area");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_scroll_area_new:
 *
 * Returns: (transfer floating): a scroll area
 */
GtkWidget *
omarchy_scroll_area_new (void)
{
  return g_object_new (OMARCHY_TYPE_SCROLL_AREA, NULL);
}

/**
 * omarchy_scroll_area_append:
 * @self: the area
 * @child: (transfer floating): a child, stacked below the others
 */
void
omarchy_scroll_area_append (OmarchyScrollArea *self, GtkWidget *child)
{
  gtk_box_append (GTK_BOX (self->box), child);
}

void
omarchy_scroll_area_remove (OmarchyScrollArea *self, GtkWidget *child)
{
  gtk_box_remove (GTK_BOX (self->box), child);
}

/**
 * omarchy_scroll_area_get_content:
 * @self: the area
 *
 * Returns: (transfer none): the column the children are in (Qt `contentColumn`)
 */
GtkWidget *
omarchy_scroll_area_get_content (OmarchyScrollArea *self)
{
  return self->box;
}

int
omarchy_scroll_area_get_max_height (OmarchyScrollArea *self)
{
  return self->max_height;
}

void
omarchy_scroll_area_set_max_height (OmarchyScrollArea *self, int max_height)
{
  max_height = MAX (-1, max_height);
  if (self->max_height == max_height)
    return;
  self->max_height = max_height;
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), s_props[S_PROP_MAX_HEIGHT]);
}

int
omarchy_scroll_area_get_spacing (OmarchyScrollArea *self)
{
  return gtk_box_get_spacing (GTK_BOX (self->box));
}

void
omarchy_scroll_area_set_spacing (OmarchyScrollArea *self, int spacing)
{
  g_clear_pointer (&self->spacing_token, g_free);
  gtk_box_set_spacing (GTK_BOX (self->box), MAX (0, spacing));
  g_object_notify_by_pspec (G_OBJECT (self), s_props[S_PROP_SPACING]);
}

/**
 * omarchy_scroll_area_set_spacing_token:
 * @self: the area
 * @token: a spacing token (`xxs`, `rowGap`) the spacing follows across theme switches
 */
void
omarchy_scroll_area_set_spacing_token (OmarchyScrollArea *self, const char *token)
{
  g_free (self->spacing_token);
  self->spacing_token = g_strdup (token);
  scroll_restyle (self);
  g_object_notify_by_pspec (G_OBJECT (self), s_props[S_PROP_SPACING]);
}

/**
 * omarchy_scroll_area_get_vadjustment:
 * @self: the area
 *
 * Returns: (transfer none): the scroll offset (Qt `flickable.contentY`)
 */
GtkAdjustment *
omarchy_scroll_area_get_vadjustment (OmarchyScrollArea *self)
{
  return self->adj;
}

gboolean
omarchy_scroll_area_get_overflowing (OmarchyScrollArea *self)
{
  return self->overflowing;
}

/**
 * omarchy_scroll_area_ensure_visible_range:
 * @self: the area
 * @y: the top of a range of the content
 * @height: its height
 *
 * Scrolls just far enough to show the range.
 */
void
omarchy_scroll_area_ensure_visible_range (OmarchyScrollArea *self, double y, double height)
{
  double top = gtk_adjustment_get_value (self->adj);
  double page = gtk_adjustment_get_page_size (self->adj);
  double max = MAX (0, gtk_adjustment_get_upper (self->adj) - page);
  if (y < top)
    gtk_adjustment_set_value (self->adj, MAX (0, y));
  else if (y + height > top + page)
    gtk_adjustment_set_value (self->adj, MIN (max, y + height - page));
}

/**
 * omarchy_scroll_area_ensure_visible:
 * @self: the area
 * @child: a widget in the content (the cursor's row after a key)
 *
 * Scrolls just far enough to show @child.
 */
void
omarchy_scroll_area_ensure_visible (OmarchyScrollArea *self, GtkWidget *child)
{
  if (!child)
    return;
  graphene_point_t p;
  if (!gtk_widget_compute_point (child, self->box, &GRAPHENE_POINT_INIT (0, 0), &p))
    return;
  omarchy_scroll_area_ensure_visible_range (self, p.y, gtk_widget_get_height (child));
}

/* ============================================================================== KeyValue === */

/**
 * OmarchyKeyValue:
 *
 * Dim labels beside their values in a grid, #OmarchyKeyValue:columns pairs to a line: a
 * panel's details block. Text `bodySmall`; labels `textDim`, values the foreground or their
 * tone. Each grid column as wide as its widest cell, `space(20)` apart; lines `labelGap` apart.
 */
typedef struct {
  char *label, *value, *tone;
} Pair;

struct _OmarchyKeyValue {
  GtkWidget parent_instance;
  GArray *pairs;
  int columns;
};

enum { K_PROP_0, K_PROP_COLUMNS, K_N_PROPS };
static GParamSpec *k_props[K_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyKeyValue, omarchy_key_value, GTK_TYPE_WIDGET)

typedef struct {
  int n_cols, n_rows, *col_w, line, gap_x, gap_y, width, height;
} KvGeom;

static void
kv_geom (OmarchyKeyValue *self, KvGeom *g)
{
  GtkWidget *w = GTK_WIDGET (self);
  const OmarchyTokens *t = omarchy_tokens ();
  int cells = self->pairs->len * 2;
  g->n_cols = MAX (1, self->columns) * 2;
  g->n_rows = (cells + g->n_cols - 1) / g->n_cols;
  /* Qt's Grid sizes only the columns that hold cells. */
  int used = MIN (g->n_cols, cells);
  g->col_w = g_new0 (int, g->n_cols);
  for (int i = 0; i < cells; i++) {
    Pair *pr = &g_array_index (self->pairs, Pair, i / 2);
    g_autoptr (PangoLayout) l = omarchy_layout (w, i % 2 ? pr->value : pr->label, t->body_small, FALSE);
    int tw;
    omarchy_layout_size (l, &tw, NULL);
    g->col_w[i % g->n_cols] = MAX (g->col_w[i % g->n_cols], tw);
  }
  g->line = omarchy_line_height (t->body_small);
  g->gap_x = omarchy_space (20);
  g->gap_y = t->label_gap;
  g->width = 0;
  for (int c = 0; c < used; c++)
    g->width += g->col_w[c] + (c ? g->gap_x : 0);
  g->height = g->n_rows ? g->n_rows * g->line + (g->n_rows - 1) * g->gap_y : 0;
}

static void
kv_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  KvGeom g;
  kv_geom (OMARCHY_KEY_VALUE (w), &g);
  *min = *nat = o == GTK_ORIENTATION_HORIZONTAL ? g.width : g.height;
  g_free (g.col_w);
}

static void
kv_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyKeyValue *self = OMARCHY_KEY_VALUE (w);
  const OmarchyTokens *p = omarchy_paint ();
  KvGeom g;
  kv_geom (self, &g);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  float W = gtk_widget_get_width (w);
  for (guint i = 0; i < self->pairs->len * 2; i++) {
    Pair *pr = &g_array_index (self->pairs, Pair, i / 2);
    int col = i % g.n_cols, row = i / g.n_cols;
    float x = 0;
    for (int c = 0; c < col; c++)
      x += g.col_w[c] + g.gap_x;
    gboolean label = i % 2 == 0;
    g_autoptr (PangoLayout) l = omarchy_layout (w, label ? pr->label : pr->value, p->body_small, FALSE);
    int tw;
    omarchy_layout_size (l, &tw, NULL);
    if (rtl)
      x = W - x - tw;
    GdkRGBA c = label ? p->text_dim : omarchy_lists_tone (pr->tone, &p->foreground);
    omarchy_draw_layout (s, l, x, row * (g.line + g.gap_y), &c);
  }
  g_free (g.col_w);
}

static void
pair_clear (gpointer data)
{
  Pair *p = data;
  g_free (p->label);
  g_free (p->value);
  g_free (p->tone);
}

static void
omarchy_key_value_init (OmarchyKeyValue *self)
{
  self->pairs = g_array_new (FALSE, TRUE, sizeof (Pair));
  g_array_set_clear_func (self->pairs, pair_clear);
  self->columns = 1;
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
kv_finalize (GObject *o)
{
  g_array_unref (OMARCHY_KEY_VALUE (o)->pairs);
  G_OBJECT_CLASS (omarchy_key_value_parent_class)->finalize (o);
}

static void
kv_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  if (id == K_PROP_COLUMNS)
    omarchy_key_value_set_columns (OMARCHY_KEY_VALUE (o), g_value_get_int (v));
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
kv_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  if (id == K_PROP_COLUMNS)
    g_value_set_int (v, OMARCHY_KEY_VALUE (o)->columns);
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
omarchy_key_value_class_init (OmarchyKeyValueClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = kv_finalize;
  oc->set_property = kv_set_property;
  oc->get_property = kv_get_property;
  wc->measure = kv_measure;
  wc->snapshot = kv_snapshot;
  /**
   * OmarchyKeyValue:columns:
   *
   * Pairs to a line (Qt `pairColumns`).
   */
  k_props[K_PROP_COLUMNS] = g_param_spec_int ("columns", NULL, NULL, 1, 64, 1,
                                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, K_N_PROPS, k_props);
  gtk_widget_class_set_css_name (wc, "omarchy-key-value");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_TABLE);
}

/**
 * omarchy_key_value_new:
 *
 * Returns: (transfer floating): an empty details grid
 */
GtkWidget *
omarchy_key_value_new (void)
{
  return g_object_new (OMARCHY_TYPE_KEY_VALUE, NULL);
}

/**
 * omarchy_key_value_append:
 * @self: the grid
 * @label: the dim label
 * @value: the value
 */
void
omarchy_key_value_append (OmarchyKeyValue *self, const char *label, const char *value)
{
  omarchy_key_value_append_toned (self, label, value, NULL);
}

/**
 * omarchy_key_value_append_toned:
 * @self: the grid
 * @label: the dim label
 * @value: the value
 * @tone: (nullable): `accent`, `success`, `warning` or `urgent` tints the value
 */
void
omarchy_key_value_append_toned (OmarchyKeyValue *self, const char *label, const char *value, const char *tone)
{
  Pair p = { g_strdup (label ? label : ""), g_strdup (value ? value : ""), g_strdup (tone ? tone : "") };
  g_array_append_val (self->pairs, p);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
omarchy_key_value_remove_all (OmarchyKeyValue *self)
{
  g_array_set_size (self->pairs, 0);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

guint
omarchy_key_value_get_n_pairs (OmarchyKeyValue *self)
{
  return self->pairs->len;
}

int
omarchy_key_value_get_columns (OmarchyKeyValue *self)
{
  return self->columns;
}

void
omarchy_key_value_set_columns (OmarchyKeyValue *self, int columns)
{
  columns = MAX (1, columns);
  if (self->columns == columns)
    return;
  self->columns = columns;
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), k_props[K_PROP_COLUMNS]);
}

/* ================================================================================== Hero === */

/**
 * OmarchyHero:
 *
 * The head of a panel: a big glyph (`display`), then `space(14)`, a bold title (`title`) over a
 * status line (`caption` bold, uppercased, letter spacing 1.2, `textDim`, `urgent` with
 * #OmarchyHero:error), `space(2)` apart, both eliding; and a trailing control `space(12)` after
 * the labels, centered against them.
 */
struct _OmarchyHero {
  GtkWidget parent_instance;
  char *glyph, *title, *status;
  gboolean error;
  double glyph_opacity;
  GtkWidget *trailing;
};

enum { H_PROP_0, H_PROP_GLYPH, H_PROP_TITLE, H_PROP_STATUS, H_PROP_ERROR, H_PROP_GLYPH_OPACITY, H_PROP_TRAILING, H_N_PROPS };
static GParamSpec *h_props[H_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyHero, omarchy_hero, GTK_TYPE_WIDGET)

static PangoLayout *
hero_status_layout (OmarchyHero *self, int width)
{
  const OmarchyTokens *t = omarchy_tokens ();
  g_autofree char *upper = g_utf8_strup (self->status, -1);
  PangoLayout *l = omarchy_lists_line (GTK_WIDGET (self), upper, t->caption, TRUE, width);
  PangoAttrList *attrs = pango_attr_list_new ();
  pango_attr_list_insert (attrs, pango_attr_letter_spacing_new ((int) lround (1.2 * PANGO_SCALE)));
  pango_layout_set_attributes (l, attrs);
  pango_attr_list_unref (attrs);
  return l;
}

typedef struct {
  float labels_x;
  int glyph_w, glyph_h, gap, title_h, status_h, labels_h, trail_w, trail_h, inset, height;
} HeroGeom;

static void
hero_geom (OmarchyHero *self, HeroGeom *g)
{
  const OmarchyTokens *t = omarchy_tokens ();
  memset (g, 0, sizeof *g);
  g->glyph_w = self->glyph[0] ? t->display : 0;
  g->glyph_h = MAX (t->display_large, (int) ceil (t->display * 1.35));
  g->gap = self->glyph[0] ? omarchy_space (14) : 0;
  g->labels_x = g->glyph_w + g->gap;
  g->title_h = omarchy_line_height (t->title);
  g->status_h = self->status[0] ? omarchy_line_height (t->caption) : 0;
  g->labels_h = g->title_h + (g->status_h ? omarchy_space (2) + g->status_h : 0);
  if (child_shown (self->trailing)) {
    gtk_widget_measure (self->trailing, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &g->trail_w, NULL, NULL);
    gtk_widget_measure (self->trailing, GTK_ORIENTATION_VERTICAL, g->trail_w, NULL, &g->trail_h, NULL, NULL);
    g->inset = g->trail_w + omarchy_space (12);
  }
  g->height = MAX (g->glyph_h, MAX (g->labels_h, g->trail_h));
}

static void
hero_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyHero *self = OMARCHY_HERO (w);
  HeroGeom g;
  hero_geom (self, &g);
  if (o == GTK_ORIENTATION_VERTICAL) {
    *min = *nat = g.height;
    return;
  }
  const OmarchyTokens *t = omarchy_tokens ();
  g_autoptr (PangoLayout) a = omarchy_layout (w, self->title, t->title, TRUE);
  g_autoptr (PangoLayout) b = hero_status_layout (self, -1);
  int tw, sw;
  omarchy_layout_size (a, &tw, NULL);
  omarchy_layout_size (b, &sw, NULL);
  *min = (int) ceilf (g.labels_x) + g.inset;
  *nat = (int) ceilf (g.labels_x) + MAX (tw, self->status[0] ? sw : 0) + g.inset;
}

static float
hero_x (GtkWidget *w, float x, float span)
{
  return gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL ? gtk_widget_get_width (w) - x - span : x;
}

static void
hero_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyHero *self = OMARCHY_HERO (w);
  if (!child_shown (self->trailing))
    return;
  HeroGeom g;
  hero_geom (self, &g);
  GtkAllocation a = { (int) hero_x (w, width - g.trail_w, g.trail_w), (int) omarchy_lists_center (height, g.trail_h), g.trail_w,
                      g.trail_h };
  gtk_widget_size_allocate (self->trailing, &a, -1);
}

static void
hero_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyHero *self = OMARCHY_HERO (w);
  const OmarchyTokens *p = omarchy_paint ();
  int W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  HeroGeom g;
  hero_geom (self, &g);
  if (self->glyph[0]) {
    gtk_snapshot_push_opacity (s, CLAMP (self->glyph_opacity, 0, 1));
    omarchy_draw_optical_glyph (w, s, self->glyph, p->display, hero_x (w, 0, g.glyph_w),
                                omarchy_lists_center (H, g.glyph_h), g.glyph_w, g.glyph_h, &p->foreground);
    gtk_snapshot_pop (s);
  }
  int lw = MAX (0, (int) floorf (W - g.labels_x - g.inset));
  float ly = omarchy_lists_center (H, g.labels_h);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  g_autoptr (PangoLayout) a = omarchy_lists_line (w, self->title, p->title, TRUE, lw);
  if (rtl)
    pango_layout_set_alignment (a, PANGO_ALIGN_RIGHT);
  omarchy_draw_layout (s, a, hero_x (w, g.labels_x, lw), ly, &p->foreground);
  if (self->status[0]) {
    g_autoptr (PangoLayout) b = hero_status_layout (self, lw);
    if (rtl)
      pango_layout_set_alignment (b, PANGO_ALIGN_RIGHT);
    omarchy_draw_layout (s, b, hero_x (w, g.labels_x, lw), ly + g.title_h + omarchy_space (2),
                         self->error ? &p->urgent : &p->text_dim);
  }
  if (child_shown (self->trailing))
    gtk_widget_snapshot_child (w, self->trailing, s);
}

static void
omarchy_hero_init (OmarchyHero *self)
{
  self->glyph = g_strdup ("");
  self->title = g_strdup ("");
  self->status = g_strdup ("");
  self->glyph_opacity = 1.0;
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
hero_dispose (GObject *o)
{
  g_clear_pointer (&OMARCHY_HERO (o)->trailing, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_hero_parent_class)->dispose (o);
}

static void
hero_finalize (GObject *o)
{
  OmarchyHero *self = OMARCHY_HERO (o);
  g_free (self->glyph);
  g_free (self->title);
  g_free (self->status);
  G_OBJECT_CLASS (omarchy_hero_parent_class)->finalize (o);
}

static void
hero_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyHero *self = OMARCHY_HERO (o);
  switch (id) {
  case H_PROP_GLYPH: omarchy_hero_set_glyph (self, g_value_get_string (v)); break;
  case H_PROP_TITLE: omarchy_hero_set_title (self, g_value_get_string (v)); break;
  case H_PROP_STATUS: omarchy_hero_set_status (self, g_value_get_string (v)); break;
  case H_PROP_ERROR: omarchy_hero_set_error (self, g_value_get_boolean (v)); break;
  case H_PROP_GLYPH_OPACITY: omarchy_hero_set_glyph_opacity (self, g_value_get_double (v)); break;
  case H_PROP_TRAILING: omarchy_hero_set_trailing (self, g_value_get_object (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
hero_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyHero *self = OMARCHY_HERO (o);
  switch (id) {
  case H_PROP_GLYPH: g_value_set_string (v, self->glyph); break;
  case H_PROP_TITLE: g_value_set_string (v, self->title); break;
  case H_PROP_STATUS: g_value_set_string (v, self->status); break;
  case H_PROP_ERROR: g_value_set_boolean (v, self->error); break;
  case H_PROP_GLYPH_OPACITY: g_value_set_double (v, self->glyph_opacity); break;
  case H_PROP_TRAILING: g_value_set_object (v, self->trailing); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_hero_class_init (OmarchyHeroClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = hero_dispose;
  oc->finalize = hero_finalize;
  oc->set_property = hero_set_property;
  oc->get_property = hero_get_property;
  wc->measure = hero_measure;
  wc->size_allocate = hero_allocate;
  wc->snapshot = hero_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyHero:glyph:
   *
   * The big Nerd Font glyph (`display`).
   */
  h_props[H_PROP_GLYPH] = g_param_spec_string ("glyph", NULL, NULL, "", f);
  /**
   * OmarchyHero:title:
   *
   * The bold title (`title`).
   */
  h_props[H_PROP_TITLE] = g_param_spec_string ("title", NULL, NULL, "", f);
  /**
   * OmarchyHero:status:
   *
   * The status line, shown uppercased.
   */
  h_props[H_PROP_STATUS] = g_param_spec_string ("status", NULL, NULL, "", f);
  /**
   * OmarchyHero:error:
   *
   * The status in `urgent`.
   */
  h_props[H_PROP_ERROR] = g_param_spec_boolean ("error", NULL, NULL, FALSE, f);
  /**
   * OmarchyHero:glyph-opacity:
   *
   * The glyph's opacity (half for a missing device).
   */
  h_props[H_PROP_GLYPH_OPACITY] = g_param_spec_double ("glyph-opacity", NULL, NULL, 0, 1, 1, f);
  /**
   * OmarchyHero:trailing:
   *
   * A control on the trailing edge (a switch).
   */
  h_props[H_PROP_TRAILING] = g_param_spec_object ("trailing", NULL, NULL, GTK_TYPE_WIDGET, f);
  g_object_class_install_properties (oc, H_N_PROPS, h_props);
  gtk_widget_class_set_css_name (wc, "omarchy-hero");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_hero_new:
 * @glyph: (nullable): the big glyph
 * @title: (nullable): the title
 *
 * Returns: (transfer floating): a hero
 */
GtkWidget *
omarchy_hero_new (const char *glyph, const char *title)
{
  return g_object_new (OMARCHY_TYPE_HERO, "glyph", glyph ? glyph : "", "title", title ? title : "", NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyHero, hero, glyph, h_props, H_PROP_GLYPH, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyHero, hero, title, h_props, H_PROP_TITLE, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyHero, hero, status, h_props, H_PROP_STATUS, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyHero, hero, error, h_props, H_PROP_ERROR, FALSE)

double
omarchy_hero_get_glyph_opacity (OmarchyHero *self)
{
  return self->glyph_opacity;
}

void
omarchy_hero_set_glyph_opacity (OmarchyHero *self, double opacity)
{
  opacity = CLAMP (opacity, 0, 1);
  if (self->glyph_opacity == opacity)
    return;
  self->glyph_opacity = opacity;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), h_props[H_PROP_GLYPH_OPACITY]);
}

/**
 * omarchy_hero_set_trailing:
 * @self: the hero
 * @trailing: (nullable) (transfer floating): a control on the trailing edge
 */
void
omarchy_hero_set_trailing (OmarchyHero *self, GtkWidget *trailing)
{
  if (self->trailing == trailing)
    return;
  if (self->trailing)
    gtk_widget_unparent (self->trailing);
  self->trailing = trailing;
  if (trailing)
    gtk_widget_set_parent (trailing, GTK_WIDGET (self));
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), h_props[H_PROP_TRAILING]);
}

/**
 * omarchy_hero_get_trailing:
 * @self: the hero
 *
 * Returns: (transfer none) (nullable): the trailing control
 */
GtkWidget *
omarchy_hero_get_trailing (OmarchyHero *self)
{
  return self->trailing;
}
