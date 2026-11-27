/* omarchy-editing.c: TrimHandle (TrimHandle.qml), CropFrame (CropFrame.qml), DragHandle
 * (DragHandle.qml) and DropIndicator (DropIndicator.qml), drawn as the Qt kit draws them, and
 * what the editing components share (omarchy-editing-private.h). */
#include "omarchy-editing-private.h"

#include <math.h>
#include <string.h>

/* ============================================================================ Shared ======= */

double
omarchy_editing_round (double x)
{
  return floor (x + 0.5);
}

GdkRGBA
omarchy_editing_hot (const GdkRGBA *c)
{
  const OmarchyTokens *p = omarchy_paint ();
  GdkRGBA fg = p->foreground;
  GdkRGBA out = omarchy_rgba_mix (c, &fg, 0.25);
  out.alpha = 0.25 + 0.75 * c->alpha;
  return out;
}

int
omarchy_editing_knob (void)
{
  return MAX (14, (int) lround (omarchy_tokens ()->control_height * 0.38));
}

void
omarchy_editing_draw_knob (GtkSnapshot *s, float cx, float cy, gboolean hot, const GdkRGBA *fill, const GdkRGBA *ring)
{
  float k = omarchy_editing_knob () * (hot ? 1.15f : 1.0f);
  OmarchyBorder b = omarchy_border_flat (ring, MAX (1, omarchy_space (2)) * (hot ? 1.15f : 1.0f));
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (cx - k / 2, cy - k / 2, k, k), k / 2, fill, &b);
}

const GdkRGBA *
omarchy_editing_surface (const OmarchyTokens *p, const char *surface)
{
  if (!g_strcmp0 (surface, "background"))
    return &p->background;
  if (!g_strcmp0 (surface, "tooltip"))
    return &p->tooltip.background;
  if (!g_strcmp0 (surface, "menu"))
    return &p->menu.background;
  return &p->popups.background;
}

void
omarchy_editing_drag_begin (OmarchyEditingDrag *d, GtkWidget *w, double x, double y)
{
  d->active = TRUE;
  d->x0 = x;
  d->y0 = y;
  GtkWidget *root = GTK_WIDGET (gtk_widget_get_root (w));
  if (!root || !gtk_widget_compute_point (w, root, &GRAPHENE_POINT_INIT (x, y), &d->origin))
    d->origin = GRAPHENE_POINT_INIT (x, y);
}

void
omarchy_editing_drag_travel (OmarchyEditingDrag *d, GtkWidget *w, double dx, double dy, double *tx, double *ty)
{
  graphene_point_t p = GRAPHENE_POINT_INIT (d->x0 + dx, d->y0 + dy);
  GtkWidget *root = GTK_WIDGET (gtk_widget_get_root (w));
  graphene_point_t q;
  if (root && gtk_widget_compute_point (w, root, &p, &q))
    p = q;
  else
    p = GRAPHENE_POINT_INIT (d->origin.x + dx, d->origin.y + dy);
  *tx = p.x - d->origin.x;
  *ty = p.y - d->origin.y;
}

static void
drag_begin_cb (GtkGestureDrag *g, double x, double y, GtkWidget *w)
{
  OmarchyDragFunc f = g_object_get_data (G_OBJECT (g), "omarchy-begin");
  if (gtk_gesture_is_active (GTK_GESTURE (g)))
    gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_CLAIMED);
  if (f)
    f (w, x, y);
}

static void
drag_update_cb (GtkGestureDrag *g, double dx, double dy, GtkWidget *w)
{
  OmarchyDragFunc f = g_object_get_data (G_OBJECT (g), "omarchy-update");
  if (f)
    f (w, dx, dy);
}

static void
drag_end_cb (GtkGestureDrag *g, double dx, double dy, GtkWidget *w)
{
  OmarchyDragFunc f = g_object_get_data (G_OBJECT (g), "omarchy-end");
  if (f)
    f (w, dx, dy);
}

GtkGesture *
omarchy_editing_add_drag (GtkWidget *w, OmarchyDragFunc begin, OmarchyDragFunc update, OmarchyDragFunc end)
{
  GtkGesture *g = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (g), 0);
  g_object_set_data (G_OBJECT (g), "omarchy-begin", begin);
  g_object_set_data (G_OBJECT (g), "omarchy-update", update);
  g_object_set_data (G_OBJECT (g), "omarchy-end", end);
  g_signal_connect (g, "drag-begin", G_CALLBACK (drag_begin_cb), w);
  g_signal_connect (g, "drag-update", G_CALLBACK (drag_update_cb), w);
  g_signal_connect (g, "drag-end", G_CALLBACK (drag_end_cb), w);
  gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (g));
  return g;
}

/* A little more than the visible handle to grab (Qt: the MouseArea's margins of -sm). */
static gboolean
grab_contains (GtkWidget *w, double x, double y)
{
  int sm = omarchy_tokens ()->sm;
  return x >= -sm && y >= -sm && x < gtk_widget_get_width (w) + sm && y < gtk_widget_get_height (w) + sm;
}

/* ======================================================================== TrimHandle ======= */

/**
 * OmarchyTrimHandle:
 *
 * A draggable in/out handle, accent because it belongs to the chosen clip or region.
 *
 * A trim cap (the default) sits on a clip's `left` or `right` edge: `space(8)` wide, square on
 * the inside and `min(4, radius)` on the outside, with a grip line cut into it that grows from
 * 40% to 60% of the height when hot. A crop handle (#OmarchyTrimHandle:crop) is one of the
 * eight on a crop rectangle: a `space(4)` bar on `left`, `right`, `top` or `bottom`, or an
 * L bracket on `top-left`, `top-right`, `bottom-left` or `bottom-right`.
 *
 * Hot (the pointer, keyboard focus, a drag or #OmarchyTrimHandle:cursor) mixes a quarter of the
 * foreground in. ::moved reports the pointer's travel since the press, ::released the end of the
 * drag; the arrow keys ::nudged it 1px (Shift: 10px) along the axes it moves on. The owner turns
 * those into times or rectangles and places the handle.
 */
struct _OmarchyTrimHandle {
  GtkWidget parent_instance;
  char *edge;
  gboolean crop, cursor, dragging;
  gboolean has_color, has_grip;
  GdkRGBA color, grip;
  OmarchyEditingDrag drag;
};

enum { T_PROP_0, T_PROP_EDGE, T_PROP_CROP, T_PROP_CURSOR, T_PROP_DRAGGING, T_PROP_COLOR, T_PROP_GRIP_COLOR, T_N_PROPS };
static GParamSpec *t_props[T_N_PROPS];
enum { T_MOVED, T_RELEASED, T_NUDGED, T_N_SIGNALS };
static guint t_signals[T_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyTrimHandle, omarchy_trim_handle, GTK_TYPE_WIDGET)

static gboolean
trim_corner (OmarchyTrimHandle *self)
{
  const char *dash = strchr (self->edge, '-');
  return dash && dash > self->edge;
}

static gboolean
trim_horizontal_bar (OmarchyTrimHandle *self)
{
  return !strcmp (self->edge, "top") || !strcmp (self->edge, "bottom");
}

static gboolean
trim_hot (OmarchyTrimHandle *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  return self->cursor || self->dragging || omarchy_widget_hovered (w) || omarchy_widget_focus_shown (w);
}

static void
trim_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyTrimHandle *self = OMARCHY_TRIM_HANDLE (w);
  int thickness = omarchy_space (self->crop ? 4 : 8), length = omarchy_space (18);
  gboolean long_x = trim_corner (self) || trim_horizontal_bar (self);
  gboolean long_y = trim_corner (self) || !trim_horizontal_bar (self);
  if (o == GTK_ORIENTATION_HORIZONTAL)
    *min = *nat = self->crop ? (long_x ? length : thickness) : thickness;
  else
    *min = *nat = self->crop ? (long_y ? length : thickness) : omarchy_space (40);
}

static void
trim_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyTrimHandle *self = OMARCHY_TRIM_HANDLE (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  float small = MIN (4, p->radius);
  gboolean hot = trim_hot (self);
  GdkRGBA base = self->has_color ? self->color : p->accent;
  GdkRGBA c = hot ? omarchy_editing_hot (&base) : base;

  if (!self->crop) {
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (0, 0, W, H), small, &c);
    /* Square off the inner side. */
    float half = W / 2;
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (!strcmp (self->edge, "right") ? 0 : half, 0, half, H), 0, &c);
    float gw = MAX (1, omarchy_space (2));
    float gh = (float) omarchy_editing_round (H * (hot ? 0.6 : 0.4));
    GdkRGBA grip = self->has_grip ? self->grip : p->background;
    omarchy_draw_fill (s,
                       &GRAPHENE_RECT_INIT ((float) omarchy_editing_round ((W - gw) / 2),
                                            (float) omarchy_editing_round ((H - gh) / 2), gw, gh),
                       MIN (small, gw / 2), &grip);
    return;
  }

  float t = omarchy_space (4);
  float radius = MIN (small, t / 2);
  gboolean corner = trim_corner (self), hbar = trim_horizontal_bar (self);
  float y = corner && g_str_has_prefix (self->edge, "bottom") ? H - t : 0;
  const char *e = self->edge;
  /* Outer edges flush against the 1px crop border stay square; only inner/free corners round. */
  float tl = (!strcmp (e, "bottom") || !strcmp (e, "right") || !strcmp (e, "bottom-right")) ? radius : 0;
  float tr = (!strcmp (e, "bottom") || !strcmp (e, "left") || !strcmp (e, "bottom-left")) ? radius : 0;
  float bl = (!strcmp (e, "top") || !strcmp (e, "right") || !strcmp (e, "top-right")) ? radius : 0;
  float br = (!strcmp (e, "top") || !strcmp (e, "left") || !strcmp (e, "top-left")) ? radius : 0;
  omarchy_draw_fill_corners (s,
                             &GRAPHENE_RECT_INIT (0, corner || hbar ? y : 0, corner || hbar ? W : t, corner || hbar ? t : H),
                             tl, tr, br, bl, &c);
  if (corner) {
    float x = strstr (e, "right") ? W - t : 0;
    omarchy_draw_fill_corners (s, &GRAPHENE_RECT_INIT (x, 0, t, H),
                               !strcmp (e, "bottom-right") ? radius : 0,
                               !strcmp (e, "bottom-left") ? radius : 0,
                               !strcmp (e, "top-left") ? radius : 0,
                               !strcmp (e, "top-right") ? radius : 0, &c);
  }
}

static void
trim_update_cursor (OmarchyTrimHandle *self)
{
  const char *e = self->edge, *name = "nesw-resize";
  if (!strcmp (e, "left") || !strcmp (e, "right"))
    name = "ew-resize";
  else if (!strcmp (e, "top") || !strcmp (e, "bottom"))
    name = "ns-resize";
  else if (!strcmp (e, "top-left") || !strcmp (e, "bottom-right"))
    name = "nwse-resize";
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), name);
}

static void
trim_set_dragging (OmarchyTrimHandle *self, gboolean dragging)
{
  if (self->dragging == dragging)
    return;
  self->dragging = dragging;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_DRAGGING]);
}

static void
trim_drag_begin (GtkWidget *w, double x, double y)
{
  OmarchyTrimHandle *self = OMARCHY_TRIM_HANDLE (w);
  if (gtk_widget_get_focusable (w))
    gtk_widget_grab_focus (w);
  omarchy_editing_drag_begin (&self->drag, w, x, y);
  trim_set_dragging (self, TRUE);
}

static void
trim_drag_update (GtkWidget *w, double dx, double dy)
{
  OmarchyTrimHandle *self = OMARCHY_TRIM_HANDLE (w);
  if (!self->dragging)
    return;
  double tx, ty;
  omarchy_editing_drag_travel (&self->drag, w, dx, dy, &tx, &ty);
  g_signal_emit (self, t_signals[T_MOVED], 0, tx, ty);
}

static void
trim_drag_end (GtkWidget *w, double dx, double dy)
{
  OmarchyTrimHandle *self = OMARCHY_TRIM_HANDLE (w);
  if (!self->dragging)
    return;
  trim_set_dragging (self, FALSE);
  g_signal_emit (self, t_signals[T_RELEASED], 0);
}

static gboolean
trim_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyTrimHandle *self = OMARCHY_TRIM_HANDLE (w);
  double step = (state & GDK_SHIFT_MASK) ? 10 : 1, dx = 0, dy = 0;
  if (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left)
    dx = -step;
  else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right)
    dx = step;
  else if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up)
    dy = -step;
  else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down)
    dy = step;
  else
    return FALSE;
  if (!self->crop)
    dy = 0;
  if (self->crop && trim_horizontal_bar (self))
    dx = 0;
  if (self->crop && (!strcmp (self->edge, "left") || !strcmp (self->edge, "right")))
    dy = 0;
  if (dx == 0 && dy == 0)
    return FALSE;
  g_signal_emit (self, t_signals[T_NUDGED], 0, dx, dy);
  return TRUE;
}

static void
omarchy_trim_handle_init (OmarchyTrimHandle *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->edge = g_strdup ("left");
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  trim_update_cursor (self);
  omarchy_widget_follow (w);
  omarchy_editing_add_drag (w, trim_drag_begin, trim_drag_update, trim_drag_end);
  omarchy_widget_add_keys (w, trim_key);
}

static void
trim_finalize (GObject *o)
{
  g_free (OMARCHY_TRIM_HANDLE (o)->edge);
  G_OBJECT_CLASS (omarchy_trim_handle_parent_class)->finalize (o);
}

static void
trim_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyTrimHandle *self = OMARCHY_TRIM_HANDLE (o);
  switch (id) {
  case T_PROP_EDGE: omarchy_trim_handle_set_edge (self, g_value_get_string (v)); break;
  case T_PROP_CROP: omarchy_trim_handle_set_crop (self, g_value_get_boolean (v)); break;
  case T_PROP_CURSOR: omarchy_trim_handle_set_cursor (self, g_value_get_boolean (v)); break;
  case T_PROP_COLOR: omarchy_trim_handle_set_color (self, g_value_get_boxed (v)); break;
  case T_PROP_GRIP_COLOR: omarchy_trim_handle_set_grip_color (self, g_value_get_boxed (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
trim_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyTrimHandle *self = OMARCHY_TRIM_HANDLE (o);
  switch (id) {
  case T_PROP_EDGE: g_value_set_string (v, self->edge); break;
  case T_PROP_CROP: g_value_set_boolean (v, self->crop); break;
  case T_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case T_PROP_DRAGGING: g_value_set_boolean (v, self->dragging); break;
  case T_PROP_COLOR: g_value_set_boxed (v, self->has_color ? &self->color : NULL); break;
  case T_PROP_GRIP_COLOR: g_value_set_boxed (v, self->has_grip ? &self->grip : NULL); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_trim_handle_class_init (OmarchyTrimHandleClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = trim_finalize;
  oc->set_property = trim_set_property;
  oc->get_property = trim_get_property;
  wc->measure = trim_measure;
  wc->snapshot = trim_snapshot;
  wc->contains = grab_contains;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyTrimHandle:edge:
   *
   * Which edge it trims: `left` or `right` for a trim cap; `left`, `right`, `top`, `bottom`,
   * `top-left`, `top-right`, `bottom-left` or `bottom-right` for a crop handle.
   */
  t_props[T_PROP_EDGE] = g_param_spec_string ("edge", NULL, NULL, "left", f);
  /**
   * OmarchyTrimHandle:crop:
   *
   * A crop handle (a bar or an L bracket) rather than a trim cap.
   */
  t_props[T_PROP_CROP] = g_param_spec_boolean ("crop", NULL, NULL, FALSE, f);
  /**
   * OmarchyTrimHandle:cursor:
   *
   * Drawn hot, as under the pointer (Qt's `hasCursor`).
   */
  t_props[T_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyTrimHandle:dragging:
   *
   * Whether the pointer is dragging it right now.
   */
  t_props[T_PROP_DRAGGING] = g_param_spec_boolean ("dragging", NULL, NULL, FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * OmarchyTrimHandle:color:
   *
   * Its color; %NULL for the accent.
   */
  t_props[T_PROP_COLOR] = g_param_spec_boxed ("color", NULL, NULL, GDK_TYPE_RGBA, f);
  /**
   * OmarchyTrimHandle:grip-color:
   *
   * The grip line cut into a trim cap; %NULL for the background.
   */
  t_props[T_PROP_GRIP_COLOR] = g_param_spec_boxed ("grip-color", NULL, NULL, GDK_TYPE_RGBA, f);
  g_object_class_install_properties (oc, T_N_PROPS, t_props);
  /**
   * OmarchyTrimHandle::moved:
   * @self: the handle
   * @dx: the pointer's travel since the press, across
   * @dy: and down
   */
  t_signals[T_MOVED] = g_signal_new ("moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                     G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  /**
   * OmarchyTrimHandle::released:
   * @self: the handle
   *
   * The drag ended.
   */
  t_signals[T_RELEASED] = g_signal_new ("released", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                        NULL, G_TYPE_NONE, 0);
  /**
   * OmarchyTrimHandle::nudged:
   * @self: the handle
   * @dx: pixels across (1, or 10 with Shift)
   * @dy: pixels down
   *
   * An arrow key while focused, along the axes the handle moves on.
   */
  t_signals[T_NUDGED] = g_signal_new ("nudged", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                      G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  gtk_widget_class_set_css_name (wc, "omarchy-trim-handle");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_BUTTON);
}

/**
 * omarchy_trim_handle_new:
 * @edge: `left` or `right`
 *
 * Returns: (transfer floating): a trim cap
 */
GtkWidget *
omarchy_trim_handle_new (const char *edge)
{
  return g_object_new (OMARCHY_TYPE_TRIM_HANDLE, "edge", edge, NULL);
}

/**
 * omarchy_trim_handle_new_crop:
 * @edge: `left`, `right`, `top`, `bottom`, `top-left`, `top-right`, `bottom-left` or
 *   `bottom-right`
 *
 * Returns: (transfer floating): a crop handle
 */
GtkWidget *
omarchy_trim_handle_new_crop (const char *edge)
{
  return g_object_new (OMARCHY_TYPE_TRIM_HANDLE, "edge", edge, "crop", TRUE, NULL);
}

const char *
omarchy_trim_handle_get_edge (OmarchyTrimHandle *self)
{
  return self->edge;
}

void
omarchy_trim_handle_set_edge (OmarchyTrimHandle *self, const char *edge)
{
  if (!edge || !*edge)
    edge = "left";
  if (!g_strcmp0 (self->edge, edge))
    return;
  g_free (self->edge);
  self->edge = g_strdup (edge);
  trim_update_cursor (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_EDGE]);
}

OMARCHY_BOOL_ACCESSORS (OmarchyTrimHandle, trim_handle, crop, t_props, T_PROP_CROP, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyTrimHandle, trim_handle, cursor, t_props, T_PROP_CURSOR, FALSE)

gboolean
omarchy_trim_handle_get_dragging (OmarchyTrimHandle *self)
{
  return self->dragging;
}

/**
 * omarchy_trim_handle_set_color:
 * @self: the handle
 * @color: (nullable): its color, or %NULL for the accent
 */
void
omarchy_trim_handle_set_color (OmarchyTrimHandle *self, const GdkRGBA *color)
{
  self->has_color = color != NULL;
  if (color)
    self->color = *color;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_COLOR]);
}

/**
 * omarchy_trim_handle_set_grip_color:
 * @self: the handle
 * @color: (nullable): the grip line's color, or %NULL for the background
 */
void
omarchy_trim_handle_set_grip_color (OmarchyTrimHandle *self, const GdkRGBA *color)
{
  self->has_grip = color != NULL;
  if (color)
    self->grip = *color;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_GRIP_COLOR]);
}

/* ========================================================================= CropFrame ======= */

/**
 * OmarchyCropFrame:
 *
 * A crop rectangle: a 1px accent outline, rule-of-thirds guides (1px, `track`) and the eight
 * crop #OmarchyTrimHandle<!-- -->s inside its edges (side bars centered on their side). It has
 * no fill, so it sits over the image: size and place it there (an overlay), and move the
 * rectangle on ::moved and ::nudged.
 */
static const char *const crop_edges[] = { "top-left", "top", "top-right", "left", "right", "bottom-left", "bottom",
                                          "bottom-right" };
#define N_CROP_EDGES 8

struct _OmarchyCropFrame {
  GtkWidget parent_instance;
  GtkWidget *handles[N_CROP_EDGES];
  char *cursor_edge;
};

enum { C_PROP_0, C_PROP_CURSOR_EDGE, C_N_PROPS };
static GParamSpec *c_props[C_N_PROPS];
enum { C_PRESSED, C_MOVED, C_RELEASED, C_NUDGED, C_N_SIGNALS };
static guint c_signals[C_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyCropFrame, omarchy_crop_frame, GTK_TYPE_WIDGET)

static void
crop_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  int length = omarchy_space (18);
  *min = length * 2;
  *nat = MAX (*min, omarchy_space (o == GTK_ORIENTATION_HORIZONTAL ? 240 : 140));
}

static void
crop_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyCropFrame *self = OMARCHY_CROP_FRAME (w);
  for (int i = 0; i < N_CROP_EDGES; i++) {
    const char *e = crop_edges[i];
    int cw, ch;
    gtk_widget_measure (self->handles[i], GTK_ORIENTATION_HORIZONTAL, -1, NULL, &cw, NULL, NULL);
    gtk_widget_measure (self->handles[i], GTK_ORIENTATION_VERTICAL, -1, NULL, &ch, NULL, NULL);
    int x = strstr (e, "left") ? 0 : strstr (e, "right") ? width - cw : (int) omarchy_editing_round ((width - cw) / 2.0);
    int y = g_str_has_prefix (e, "top") ? 0 : g_str_has_prefix (e, "bottom") ? height - ch
                                                                             : (int) omarchy_editing_round ((height - ch) / 2.0);
    gtk_widget_size_allocate (self->handles[i], &(GtkAllocation) { x, y, cw, ch }, -1);
  }
}

static void
crop_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyCropFrame *self = OMARCHY_CROP_FRAME (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  for (int i = 0; i < 2; i++) {
    float x = (float) omarchy_editing_round (W * (i + 1) / 3.0);
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x, 0, 1, H), 0, &p->track);
  }
  for (int i = 0; i < 2; i++) {
    float y = (float) omarchy_editing_round (H * (i + 1) / 3.0);
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (0, y, W, 1), 0, &p->track);
  }
  OmarchyBorder outline = omarchy_border_flat (&p->accent, 1);
  omarchy_draw_border (s, &GRAPHENE_RECT_INIT (0, 0, W, H), 0, &outline);
  for (int i = 0; i < N_CROP_EDGES; i++)
    gtk_widget_snapshot_child (w, self->handles[i], s);
}

static const char *
crop_edge_of (OmarchyCropFrame *self, GtkWidget *handle)
{
  for (int i = 0; i < N_CROP_EDGES; i++)
    if (self->handles[i] == handle)
      return crop_edges[i];
  return "";
}

static void
crop_dragging (GtkWidget *handle, GParamSpec *pspec, OmarchyCropFrame *self)
{
  const char *edge = crop_edge_of (self, handle);
  if (omarchy_trim_handle_get_dragging (OMARCHY_TRIM_HANDLE (handle)))
    g_signal_emit (self, c_signals[C_PRESSED], 0, edge);
  else
    g_signal_emit (self, c_signals[C_RELEASED], 0, edge);
}

static void
crop_moved (GtkWidget *handle, double dx, double dy, OmarchyCropFrame *self)
{
  g_signal_emit (self, c_signals[C_MOVED], 0, crop_edge_of (self, handle), dx, dy);
}

static void
crop_nudged (GtkWidget *handle, double dx, double dy, OmarchyCropFrame *self)
{
  g_signal_emit (self, c_signals[C_NUDGED], 0, crop_edge_of (self, handle), dx, dy);
}

static void
omarchy_crop_frame_init (OmarchyCropFrame *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->cursor_edge = g_strdup ("");
  omarchy_widget_follow (w);
  for (int i = 0; i < N_CROP_EDGES; i++) {
    self->handles[i] = omarchy_trim_handle_new_crop (crop_edges[i]);
    gtk_widget_set_parent (self->handles[i], w);
    g_signal_connect (self->handles[i], "notify::dragging", G_CALLBACK (crop_dragging), self);
    g_signal_connect (self->handles[i], "moved", G_CALLBACK (crop_moved), self);
    g_signal_connect (self->handles[i], "nudged", G_CALLBACK (crop_nudged), self);
  }
}

static void
crop_dispose (GObject *o)
{
  OmarchyCropFrame *self = OMARCHY_CROP_FRAME (o);
  for (int i = 0; i < N_CROP_EDGES; i++)
    g_clear_pointer (&self->handles[i], gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_crop_frame_parent_class)->dispose (o);
}

static void
crop_finalize (GObject *o)
{
  g_free (OMARCHY_CROP_FRAME (o)->cursor_edge);
  G_OBJECT_CLASS (omarchy_crop_frame_parent_class)->finalize (o);
}

static void
crop_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  switch (id) {
  case C_PROP_CURSOR_EDGE: omarchy_crop_frame_set_cursor_edge (OMARCHY_CROP_FRAME (o), g_value_get_string (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
crop_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  switch (id) {
  case C_PROP_CURSOR_EDGE: g_value_set_string (v, OMARCHY_CROP_FRAME (o)->cursor_edge); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_crop_frame_class_init (OmarchyCropFrameClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = crop_dispose;
  oc->finalize = crop_finalize;
  oc->set_property = crop_set_property;
  oc->get_property = crop_get_property;
  wc->measure = crop_measure;
  wc->size_allocate = crop_allocate;
  wc->snapshot = crop_snapshot;
  /**
   * OmarchyCropFrame:cursor-edge:
   *
   * The handle drawn hot, as under the pointer (`top-left`, `top`, …), or "" for none.
   */
  c_props[C_PROP_CURSOR_EDGE] = g_param_spec_string ("cursor-edge", NULL, NULL, "",
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, C_N_PROPS, c_props);
  /**
   * OmarchyCropFrame::pressed:
   * @self: the frame
   * @edge: the handle's edge
   */
  c_signals[C_PRESSED] = g_signal_new ("pressed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 1, G_TYPE_STRING);
  /**
   * OmarchyCropFrame::moved:
   * @self: the frame
   * @edge: the handle's edge
   * @dx: the pointer's travel since the press, across
   * @dy: and down
   */
  c_signals[C_MOVED] = g_signal_new ("moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                     G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  /**
   * OmarchyCropFrame::released:
   * @self: the frame
   * @edge: the handle's edge
   */
  c_signals[C_RELEASED] = g_signal_new ("released", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                        NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  /**
   * OmarchyCropFrame::nudged:
   * @self: the frame
   * @edge: the handle's edge
   * @dx: pixels across
   * @dy: pixels down
   */
  c_signals[C_NUDGED] = g_signal_new ("nudged", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  gtk_widget_class_set_css_name (wc, "omarchy-crop-frame");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_crop_frame_new:
 *
 * Returns: (transfer floating): a crop frame
 */
GtkWidget *
omarchy_crop_frame_new (void)
{
  return g_object_new (OMARCHY_TYPE_CROP_FRAME, NULL);
}

const char *
omarchy_crop_frame_get_cursor_edge (OmarchyCropFrame *self)
{
  return self->cursor_edge;
}

/**
 * omarchy_crop_frame_set_cursor_edge:
 * @self: the frame
 * @edge: (nullable): the handle to draw hot, or %NULL / "" for none
 */
void
omarchy_crop_frame_set_cursor_edge (OmarchyCropFrame *self, const char *edge)
{
  if (!edge)
    edge = "";
  if (!g_strcmp0 (self->cursor_edge, edge))
    return;
  g_free (self->cursor_edge);
  self->cursor_edge = g_strdup (edge);
  for (int i = 0; i < N_CROP_EDGES; i++)
    omarchy_trim_handle_set_cursor (OMARCHY_TRIM_HANDLE (self->handles[i]), !strcmp (crop_edges[i], edge));
  g_object_notify_by_pspec (G_OBJECT (self), c_props[C_PROP_CURSOR_EDGE]);
}

/**
 * omarchy_crop_frame_get_handle:
 * @self: the frame
 * @edge: `top-left`, `top`, `top-right`, `left`, `right`, `bottom-left`, `bottom` or
 *   `bottom-right`
 *
 * Returns: (transfer none) (nullable): the handle on that edge
 */
OmarchyTrimHandle *
omarchy_crop_frame_get_handle (OmarchyCropFrame *self, const char *edge)
{
  for (int i = 0; i < N_CROP_EDGES; i++)
    if (!g_strcmp0 (crop_edges[i], edge))
      return OMARCHY_TRIM_HANDLE (self->handles[i]);
  return NULL;
}

/* ======================================================================== DragHandle ======= */

/**
 * OmarchyDragHandle:
 *
 * The grip a row is dragged by to reorder it: a 2×3 grid of `space(3)` dots `space(3)` apart,
 * centered in `space(16)` × `space(22)`; textDim at rest and the foreground when hot (the pointer,
 * keyboard focus, a drag or #OmarchyDragHandle:cursor). ::pressed, ::moved (travel since the
 * press) and ::released follow a drag; Up/Down while focused ask to move the row
 * (::move-requested). The list owning the rows works out the drop slot and shows an
 * #OmarchyDropIndicator there.
 */
struct _OmarchyDragHandle {
  GtkWidget parent_instance;
  gboolean cursor, dragging;
  OmarchyEditingDrag drag;
};

enum { D_PROP_0, D_PROP_CURSOR, D_PROP_DRAGGING, D_N_PROPS };
static GParamSpec *d_props[D_N_PROPS];
enum { D_PRESSED, D_MOVED, D_RELEASED, D_MOVE_REQUESTED, D_N_SIGNALS };
static guint d_signals[D_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyDragHandle, omarchy_drag_handle, GTK_TYPE_WIDGET)

static void
grip_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  *min = *nat = omarchy_space (o == GTK_ORIENTATION_HORIZONTAL ? 16 : 22);
}

static void
grip_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyDragHandle *self = OMARCHY_DRAG_HANDLE (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  float dot = omarchy_space (3), gap = omarchy_space (3);
  gboolean hot = self->cursor || self->dragging || omarchy_widget_hovered (w) || omarchy_widget_focus_shown (w);
  const GdkRGBA *c = hot ? &p->foreground : &p->text_dim;
  float x0 = (W - (dot * 2 + gap)) / 2.0f;
  float y0 = (H - (dot * 3 + gap * 2)) / 2.0f;
  float radius = MIN (MIN (4, p->radius), dot / 2);
  for (int i = 0; i < 6; i++)
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x0 + (i % 2) * (dot + gap), y0 + (i / 2) * (dot + gap), dot, dot),
                       radius, c);
}

static void
grip_set_dragging (OmarchyDragHandle *self, gboolean dragging)
{
  if (self->dragging == dragging)
    return;
  self->dragging = dragging;
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), dragging ? "grabbing" : "grab");
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), d_props[D_PROP_DRAGGING]);
}

static void
grip_drag_begin (GtkWidget *w, double x, double y)
{
  OmarchyDragHandle *self = OMARCHY_DRAG_HANDLE (w);
  if (gtk_widget_get_focusable (w))
    gtk_widget_grab_focus (w);
  omarchy_editing_drag_begin (&self->drag, w, x, y);
  grip_set_dragging (self, TRUE);
  g_signal_emit (self, d_signals[D_PRESSED], 0);
}

static void
grip_drag_update (GtkWidget *w, double dx, double dy)
{
  OmarchyDragHandle *self = OMARCHY_DRAG_HANDLE (w);
  if (!self->dragging)
    return;
  double tx, ty;
  omarchy_editing_drag_travel (&self->drag, w, dx, dy, &tx, &ty);
  g_signal_emit (self, d_signals[D_MOVED], 0, tx, ty);
}

static void
grip_drag_end (GtkWidget *w, double dx, double dy)
{
  OmarchyDragHandle *self = OMARCHY_DRAG_HANDLE (w);
  if (!self->dragging)
    return;
  grip_set_dragging (self, FALSE);
  g_signal_emit (self, d_signals[D_RELEASED], 0);
}

static gboolean
grip_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  int delta;
  if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up)
    delta = -1;
  else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down)
    delta = 1;
  else
    return FALSE;
  g_signal_emit (w, d_signals[D_MOVE_REQUESTED], 0, delta);
  return TRUE;
}

static void
omarchy_drag_handle_init (OmarchyDragHandle *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  gtk_widget_set_cursor_from_name (w, "grab");
  omarchy_widget_follow (w);
  omarchy_editing_add_drag (w, grip_drag_begin, grip_drag_update, grip_drag_end);
  omarchy_widget_add_keys (w, grip_key);
}

static void
grip_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  switch (id) {
  case D_PROP_CURSOR: omarchy_drag_handle_set_cursor (OMARCHY_DRAG_HANDLE (o), g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
grip_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyDragHandle *self = OMARCHY_DRAG_HANDLE (o);
  switch (id) {
  case D_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case D_PROP_DRAGGING: g_value_set_boolean (v, self->dragging); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_drag_handle_class_init (OmarchyDragHandleClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->set_property = grip_set_property;
  oc->get_property = grip_get_property;
  wc->measure = grip_measure;
  wc->snapshot = grip_snapshot;
  /**
   * OmarchyDragHandle:cursor:
   *
   * Drawn hot, as under the pointer (Qt's `hasCursor`).
   */
  d_props[D_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE,
                                                 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * OmarchyDragHandle:dragging:
   *
   * Whether the pointer is dragging it right now.
   */
  d_props[D_PROP_DRAGGING] = g_param_spec_boolean ("dragging", NULL, NULL, FALSE,
                                                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, D_N_PROPS, d_props);
  /**
   * OmarchyDragHandle::pressed:
   * @self: the handle
   */
  d_signals[D_PRESSED] = g_signal_new ("pressed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 0);
  /**
   * OmarchyDragHandle::moved:
   * @self: the handle
   * @dx: the pointer's travel since the press, across
   * @dy: and down
   */
  d_signals[D_MOVED] = g_signal_new ("moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                     G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  /**
   * OmarchyDragHandle::released:
   * @self: the handle
   */
  d_signals[D_RELEASED] = g_signal_new ("released", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                        NULL, G_TYPE_NONE, 0);
  /**
   * OmarchyDragHandle::move-requested:
   * @self: the handle
   * @delta: -1 (Up) or 1 (Down)
   *
   * Up or Down while focused: move the row by one.
   */
  d_signals[D_MOVE_REQUESTED] = g_signal_new ("move-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                              NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
  gtk_widget_class_set_css_name (wc, "omarchy-drag-handle");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_BUTTON);
}

/**
 * omarchy_drag_handle_new:
 *
 * Returns: (transfer floating): a drag handle
 */
GtkWidget *
omarchy_drag_handle_new (void)
{
  return g_object_new (OMARCHY_TYPE_DRAG_HANDLE, NULL);
}

OMARCHY_BOOL_ACCESSORS (OmarchyDragHandle, drag_handle, cursor, d_props, D_PROP_CURSOR, FALSE)

gboolean
omarchy_drag_handle_get_dragging (OmarchyDragHandle *self)
{
  return self->dragging;
}

/* ===================================================================== DropIndicator ======= */

/**
 * OmarchyDropIndicator:
 *
 * Where a dragged row will land: an accent line [func@Omarchy.DropIndicator.get_thickness]
 * thick, as wide as the list. Center it on the boundary between two rows, over them (an overlay
 * child, so it takes no layout space).
 */
struct _OmarchyDropIndicator {
  GtkWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (OmarchyDropIndicator, omarchy_drop_indicator, GTK_TYPE_WIDGET)

static void
drop_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = 0;
    *nat = omarchy_space (200);
  } else {
    *min = *nat = omarchy_drop_indicator_get_thickness ();
  }
}

static void
drop_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (0, 0, W, H), MIN (MIN (4, p->radius), H / 2), &p->accent);
}

static void
omarchy_drop_indicator_init (OmarchyDropIndicator *self)
{
  gtk_widget_set_can_target (GTK_WIDGET (self), FALSE);
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
omarchy_drop_indicator_class_init (OmarchyDropIndicatorClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  wc->measure = drop_measure;
  wc->snapshot = drop_snapshot;
  gtk_widget_class_set_css_name (wc, "omarchy-drop-indicator");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_PRESENTATION);
}

/**
 * omarchy_drop_indicator_new:
 *
 * Returns: (transfer floating): a drop indicator
 */
GtkWidget *
omarchy_drop_indicator_new (void)
{
  return g_object_new (OMARCHY_TYPE_DROP_INDICATOR, NULL);
}

/**
 * omarchy_drop_indicator_get_thickness:
 *
 * Returns: the line's thickness, `max(1, space(2))`
 */
int
omarchy_drop_indicator_get_thickness (void)
{
  return MAX (1, omarchy_space (2));
}
