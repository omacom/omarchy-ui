/* omarchy-data-split.c: SplitView (SplitView.qml, Qt Quick's SplitView with Omarchy's handle)
 * and Sidebar (Sidebar.qml) (spec/components/data.md). */
#include "omarchy-data-private.h"

/* ============================================================================ SplitHandle === */

/* The strip between two panes: `md` thick, a hairline in `track` at floor((md − hairline) / 2),
 * lit with the hover fill at min(4, radius) under the pointer, while dragged, with keyboard
 * focus, or when the view has the cursor flag. */
#define OMARCHY_TYPE_SPLIT_HANDLE (omarchy_split_handle_get_type ())
G_DECLARE_FINAL_TYPE (OmarchySplitHandle, omarchy_split_handle, OMARCHY, SPLIT_HANDLE, GtkWidget)

struct _OmarchySplitHandle {
  GtkWidget parent_instance;
  OmarchySplitView *view;
  int gap;      /* which gap between visible panes */
  gboolean dragging;
  int pane;     /* the pane a drag or key resizes, -1 none */
  int sign;     /* +1: the leading pane; -1: the trailing one */
  int start;    /* its size when the drag began */
  int room;     /* how much it may grow (what the fill panes have) */
};

G_DEFINE_FINAL_TYPE (OmarchySplitHandle, omarchy_split_handle, GTK_TYPE_WIDGET)

/* ============================================================================== SplitView === */

typedef struct {
  GtkWidget *child;
  int size; /* -1: fills */
  int min, max; /* max -1: none */
  int at, extent; /* allocated along the split, start-edge */
} SplitPane;

/**
 * OmarchySplitView:
 *
 * Panes in a row (or a column when vertical, through #GtkOrientable) with a draggable handle
 * between each visible pair: a strip `md` thick holding a hairline in `track`, lit with the
 * hover fill under the pointer, while dragged and with #OmarchySplitView:has-cursor. Sized panes
 * (omarchy_split_view_append_sized()) take their size, clamped to their minimum and maximum;
 * the rest share what is left (the last pane fills when none does). Dragging a handle resizes
 * the sized pane beside it, the leading one first, and emits ::resized. A hidden pane takes its
 * handle with it. Handles take keyboard focus: the arrows along the split (h/l, j/k) move one by
 * `md`, Home/End to the ends.
 */
struct _OmarchySplitView {
  GtkWidget parent_instance;
  GtkOrientation orientation;
  GPtrArray *panes;   /* SplitPane, owned */
  GPtrArray *handles; /* OmarchySplitHandle, one per pane but the first */
  gboolean has_cursor;
};

enum { SV_PROP_0, SV_PROP_HAS_CURSOR, SV_PROP_ORIENTATION, SV_N_PROPS };
static GParamSpec *sv_props[SV_N_PROPS];
enum { SV_RESIZED, SV_N_SIGNALS };
static guint sv_signals[SV_N_SIGNALS];

static void split_buildable_init (GtkBuildableIface *iface);
G_DEFINE_FINAL_TYPE_WITH_CODE (OmarchySplitView, omarchy_split_view, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_ORIENTABLE, NULL)
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, split_buildable_init))

static gboolean
split_across (OmarchySplitView *self)
{
  return self->orientation == GTK_ORIENTATION_HORIZONTAL;
}

static int
split_clamp (const SplitPane *p, int size)
{
  int hi = p->max >= 0 ? MAX (p->max, p->min) : G_MAXINT / 4;
  return CLAMP (size, p->min, hi);
}

/* The visible panes, in order. */
static GPtrArray *
split_visible (OmarchySplitView *self)
{
  GPtrArray *out = g_ptr_array_new ();
  for (guint i = 0; i < self->panes->len; i++) {
    SplitPane *p = g_ptr_array_index (self->panes, i);
    if (gtk_widget_get_visible (p->child))
      g_ptr_array_add (out, p);
  }
  return out;
}

/* Whether a visible pane shares the leftover: a fill pane, or the last when none fills. */
static gboolean
split_fills (GPtrArray *visible, guint k)
{
  SplitPane *p = g_ptr_array_index (visible, k);
  if (p->size < 0)
    return TRUE;
  for (guint i = 0; i < visible->len; i++)
    if (((SplitPane *) g_ptr_array_index (visible, i))->size < 0)
      return FALSE;
  return k == visible->len - 1;
}

static void
split_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchySplitView *self = OMARCHY_SPLIT_VIEW (w);
  g_autoptr (GPtrArray) visible = split_visible (self);
  int thick = omarchy_tokens ()->md;
  gboolean along = o == self->orientation;
  *min = *nat = 0;
  for (guint k = 0; k < visible->len; k++) {
    SplitPane *p = g_ptr_array_index (visible, k);
    int cmin, cnat;
    gtk_widget_measure (p->child, o, -1, &cmin, &cnat, NULL, NULL);
    if (along) {
      if (split_fills (visible, k)) {
        *min += cmin;
        *nat += cnat;
      } else {
        *min += split_clamp (p, 0);
        *nat += split_clamp (p, p->size);
      }
      if (k > 0) {
        *min += thick;
        *nat += thick;
      }
    } else {
      *min = MAX (*min, cmin);
      *nat = MAX (*nat, cnat);
    }
  }
}

static void
split_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchySplitView *self = OMARCHY_SPLIT_VIEW (w);
  g_autoptr (GPtrArray) visible = split_visible (self);
  gboolean across = split_across (self);
  gboolean rtl = across && data_rtl (w);
  int thick = omarchy_tokens ()->md;
  int length = across ? width : height, cross = across ? height : width;
  int n = visible->len, fixed = 0, fills = 0;
  for (int k = 0; k < n; k++) {
    SplitPane *p = g_ptr_array_index (visible, k);
    if (split_fills (visible, k))
      fills++;
    else
      fixed += split_clamp (p, p->size);
  }
  int left = MAX (0, length - fixed - thick * MAX (0, n - 1));
  int share = fills > 0 ? left / fills : 0, spare = fills > 0 ? left - share * fills : 0;
  int at = 0;
  for (int k = 0; k < n; k++) {
    SplitPane *p = g_ptr_array_index (visible, k);
    int extent;
    if (split_fills (visible, k)) {
      extent = share + (spare-- > 0 ? 1 : 0);
    } else {
      extent = split_clamp (p, p->size);
    }
    if (k > 0) {
      OmarchySplitHandle *h = g_ptr_array_index (self->handles, k - 1);
      h->gap = k - 1;
      gtk_widget_set_child_visible (GTK_WIDGET (h), TRUE);
      int hx = rtl ? length - at - thick : at;
      GtkAllocation ha = across ? (GtkAllocation) { hx, 0, thick, cross } : (GtkAllocation) { 0, hx, cross, thick };
      gtk_widget_size_allocate (GTK_WIDGET (h), &ha, -1);
      at += thick;
    }
    p->at = at;
    p->extent = extent;
    /* Never below the child's own minimum; the snapshot clips it to the pane. */
    int cmin;
    gtk_widget_measure (p->child, self->orientation, cross, &cmin, NULL, NULL, NULL);
    int give = MAX (extent, cmin);
    int px = rtl ? length - at - give : at;
    GtkAllocation a = across ? (GtkAllocation) { px, 0, give, cross } : (GtkAllocation) { 0, px, cross, give };
    gtk_widget_size_allocate (p->child, &a, -1);
    at += extent;
  }
  for (guint i = MAX (0, n - 1); i < self->handles->len; i++)
    gtk_widget_set_child_visible (g_ptr_array_index (self->handles, i), FALSE);
}

static void
split_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchySplitView *self = OMARCHY_SPLIT_VIEW (w);
  g_autoptr (GPtrArray) visible = split_visible (self);
  gboolean across = split_across (self);
  gboolean rtl = across && data_rtl (w);
  int W = gtk_widget_get_width (w), H = gtk_widget_get_height (w), length = across ? W : H;
  for (guint k = 0; k < visible->len; k++) {
    SplitPane *p = g_ptr_array_index (visible, k);
    int x = rtl ? length - p->at - p->extent : p->at;
    gtk_snapshot_push_clip (s, across ? &GRAPHENE_RECT_INIT (x, 0, p->extent, H) : &GRAPHENE_RECT_INIT (0, x, W, p->extent));
    gtk_widget_snapshot_child (w, p->child, s);
    gtk_snapshot_pop (s);
  }
  for (guint i = 0; i < self->handles->len; i++)
    gtk_widget_snapshot_child (w, g_ptr_array_index (self->handles, i), s);
}

static GtkWidget *split_handle_new (OmarchySplitView *view);

static void
split_add (OmarchySplitView *self, GtkWidget *child, int size, int min, int max)
{
  g_return_if_fail (GTK_IS_WIDGET (child) && gtk_widget_get_parent (child) == NULL);
  SplitPane *p = g_new0 (SplitPane, 1);
  p->child = child;
  p->size = size;
  p->min = MAX (0, min);
  p->max = max;
  if (self->panes->len > 0) {
    GtkWidget *h = split_handle_new (self);
    gtk_widget_set_parent (h, GTK_WIDGET (self));
    g_ptr_array_add (self->handles, h);
  }
  g_ptr_array_add (self->panes, p);
  gtk_widget_set_parent (child, GTK_WIDGET (self));
}

static int
split_index_of (OmarchySplitView *self, GtkWidget *child)
{
  for (guint i = 0; i < self->panes->len; i++)
    if (((SplitPane *) g_ptr_array_index (self->panes, i))->child == child)
      return i;
  return -1;
}

static void
omarchy_split_view_init (OmarchySplitView *self)
{
  self->orientation = GTK_ORIENTATION_HORIZONTAL;
  self->panes = g_ptr_array_new_with_free_func (g_free);
  self->handles = g_ptr_array_new ();
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
split_dispose (GObject *o)
{
  OmarchySplitView *self = OMARCHY_SPLIT_VIEW (o);
  for (guint i = 0; i < self->panes->len; i++)
    gtk_widget_unparent (((SplitPane *) g_ptr_array_index (self->panes, i))->child);
  g_ptr_array_set_size (self->panes, 0);
  for (guint i = 0; i < self->handles->len; i++)
    gtk_widget_unparent (g_ptr_array_index (self->handles, i));
  g_ptr_array_set_size (self->handles, 0);
  G_OBJECT_CLASS (omarchy_split_view_parent_class)->dispose (o);
}

static void
split_finalize (GObject *o)
{
  OmarchySplitView *self = OMARCHY_SPLIT_VIEW (o);
  g_ptr_array_unref (self->panes);
  g_ptr_array_unref (self->handles);
  G_OBJECT_CLASS (omarchy_split_view_parent_class)->finalize (o);
}

static void
split_set_orientation (OmarchySplitView *self, GtkOrientation o)
{
  if (self->orientation == o)
    return;
  self->orientation = o;
  for (guint i = 0; i < self->handles->len; i++)
    gtk_widget_set_cursor_from_name (g_ptr_array_index (self->handles, i), o == GTK_ORIENTATION_HORIZONTAL ? "col-resize" : "row-resize");
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify (G_OBJECT (self), "orientation");
}

static void
split_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchySplitView *self = OMARCHY_SPLIT_VIEW (o);
  switch (id) {
  case SV_PROP_HAS_CURSOR: omarchy_split_view_set_has_cursor (self, g_value_get_boolean (v)); break;
  case SV_PROP_ORIENTATION: split_set_orientation (self, g_value_get_enum (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
split_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchySplitView *self = OMARCHY_SPLIT_VIEW (o);
  switch (id) {
  case SV_PROP_HAS_CURSOR: g_value_set_boolean (v, self->has_cursor); break;
  case SV_PROP_ORIENTATION: g_value_set_enum (v, self->orientation); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static GtkBuildableIface *split_parent_buildable;

static void
split_add_child (GtkBuildable *b, GtkBuilder *builder, GObject *child, const char *type)
{
  if (GTK_IS_WIDGET (child))
    omarchy_split_view_append (OMARCHY_SPLIT_VIEW (b), GTK_WIDGET (child));
  else
    split_parent_buildable->add_child (b, builder, child, type);
}

static void
split_buildable_init (GtkBuildableIface *iface)
{
  split_parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = split_add_child;
}

static void
omarchy_split_view_class_init (OmarchySplitViewClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = split_dispose;
  oc->finalize = split_finalize;
  oc->set_property = split_set_property;
  oc->get_property = split_get_property;
  wc->measure = split_measure;
  wc->size_allocate = split_allocate;
  wc->snapshot = split_snapshot;
  /**
   * OmarchySplitView:has-cursor:
   *
   * Light every handle, for still renders and panels that drive the cursor.
   */
  sv_props[SV_PROP_HAS_CURSOR] = g_param_spec_boolean ("has-cursor", NULL, NULL, FALSE,
                                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_property (oc, SV_PROP_HAS_CURSOR, sv_props[SV_PROP_HAS_CURSOR]);
  g_object_class_override_property (oc, SV_PROP_ORIENTATION, "orientation");
  /**
   * OmarchySplitView::resized:
   * @self: the splitview
   * @view: the split view
   * @pane: the index of the pane a handle resized (hidden panes count)
   * @size: its new size along the split
   */
  sv_signals[SV_RESIZED] = g_signal_new ("resized", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                         G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
  gtk_widget_class_set_css_name (wc, "omarchy-split-view");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_split_view_new:
 *
 * Returns: (transfer floating): an empty, horizontal split view
 */
GtkWidget *
omarchy_split_view_new (void)
{
  return g_object_new (OMARCHY_TYPE_SPLIT_VIEW, NULL);
}

/**
 * omarchy_split_view_append:
 * @self: the split view
 * @child: (transfer floating): a pane that shares what the sized panes leave
 */
void
omarchy_split_view_append (OmarchySplitView *self, GtkWidget *child)
{
  split_add (self, child, -1, 0, -1);
}

/**
 * omarchy_split_view_append_sized:
 * @self: the split view
 * @child: (transfer floating): the pane
 * @size: its size along the split (Qt's preferredWidth)
 * @minimum: the least a drag leaves it
 * @maximum: the most a drag gives it, or -1 for no limit
 */
void
omarchy_split_view_append_sized (OmarchySplitView *self, GtkWidget *child, int size, int minimum, int maximum)
{
  split_add (self, child, MAX (0, size), minimum, maximum);
}

void
omarchy_split_view_remove (OmarchySplitView *self, GtkWidget *child)
{
  int i = split_index_of (self, child);
  g_return_if_fail (i >= 0);
  gtk_widget_unparent (child);
  g_ptr_array_remove_index (self->panes, i);
  if (self->handles->len > 0) {
    gtk_widget_unparent (g_ptr_array_index (self->handles, self->handles->len - 1));
    g_ptr_array_remove_index (self->handles, self->handles->len - 1);
  }
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

/**
 * omarchy_split_view_get_pane_size:
 * @self: the split view
 * @child: a pane
 *
 * Returns: the pane's size along the split as laid out
 */
int
omarchy_split_view_get_pane_size (OmarchySplitView *self, GtkWidget *child)
{
  int i = split_index_of (self, child);
  return i >= 0 ? ((SplitPane *) g_ptr_array_index (self->panes, i))->extent : 0;
}

/**
 * omarchy_split_view_set_pane_size:
 * @self: the split view
 * @child: a sized pane
 * @size: its new size (clamped to its minimum and maximum)
 */
void
omarchy_split_view_set_pane_size (OmarchySplitView *self, GtkWidget *child, int size)
{
  int i = split_index_of (self, child);
  g_return_if_fail (i >= 0);
  SplitPane *p = g_ptr_array_index (self->panes, i);
  p->size = split_clamp (p, size);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

gboolean
omarchy_split_view_get_has_cursor (OmarchySplitView *self)
{
  return self->has_cursor;
}

void
omarchy_split_view_set_has_cursor (OmarchySplitView *self, gboolean has_cursor)
{
  if (self->has_cursor == !!has_cursor)
    return;
  self->has_cursor = !!has_cursor;
  for (guint i = 0; i < self->handles->len; i++)
    gtk_widget_queue_draw (g_ptr_array_index (self->handles, i));
  g_object_notify_by_pspec (G_OBJECT (self), sv_props[SV_PROP_HAS_CURSOR]);
}

/**
 * omarchy_split_view_get_handle_thickness:
 * @self: the split view
 *
 * Returns: the handles' thickness (`md`)
 */
int
omarchy_split_view_get_handle_thickness (OmarchySplitView *self)
{
  return omarchy_tokens ()->md;
}

/**
 * omarchy_split_view_get_handle:
 * @self: the split view
 * @index: a handle, from the start
 *
 * Returns: (transfer none) (nullable): the handle widget (for tests and focus)
 */
GtkWidget *
omarchy_split_view_get_handle (OmarchySplitView *self, int index)
{
  return index >= 0 && index < (int) self->handles->len ? g_ptr_array_index (self->handles, index) : NULL;
}

/* ---- the handle's behavior ---------------------------------------------------------------- */

/* Which pane a handle resizes: the sized one beside it, the leading one first. */
static void
handle_target (OmarchySplitHandle *h)
{
  OmarchySplitView *self = h->view;
  g_autoptr (GPtrArray) visible = split_visible (self);
  h->pane = -1;
  if (h->gap + 1 >= (int) visible->len)
    return;
  int a = h->gap, b = h->gap + 1, k = -1;
  if (!split_fills (visible, a)) {
    k = a;
    h->sign = 1;
  } else if (!split_fills (visible, b)) {
    k = b;
    h->sign = -1;
  }
  if (k < 0)
    return;
  SplitPane *p = g_ptr_array_index (visible, k);
  h->pane = split_index_of (self, p->child);
  h->start = p->extent;
  h->room = 0;
  for (guint i = 0; i < visible->len; i++)
    if (split_fills (visible, i))
      h->room += ((SplitPane *) g_ptr_array_index (visible, i))->extent;
}

static void
handle_resize (OmarchySplitHandle *h, double delta)
{
  OmarchySplitView *self = h->view;
  if (h->pane < 0)
    return;
  SplitPane *p = g_ptr_array_index (self->panes, h->pane);
  int size = (int) lround (h->start + h->sign * delta);
  size = MIN (size, h->start + h->room);
  size = split_clamp (p, MAX (0, size));
  if (size == p->size)
    return;
  p->size = size;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  g_signal_emit (self, sv_signals[SV_RESIZED], 0, h->pane, size);
}

static void
handle_drag_begin (GtkGestureDrag *g, double x, double y, OmarchySplitHandle *h)
{
  handle_target (h);
  h->dragging = TRUE;
  gtk_widget_queue_draw (GTK_WIDGET (h));
}

static void
handle_drag_update (GtkGestureDrag *g, double dx, double dy, OmarchySplitHandle *h)
{
  gboolean across = split_across (h->view);
  double delta = across ? dx : dy;
  if (across && data_rtl (GTK_WIDGET (h->view)))
    delta = -delta;
  handle_resize (h, delta);
}

static void
handle_drag_end (GtkGestureDrag *g, double dx, double dy, OmarchySplitHandle *h)
{
  h->dragging = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (h));
}

static gboolean
handle_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchySplitHandle *h = OMARCHY_SPLIT_HANDLE (w);
  gboolean across = split_across (h->view);
  int step = omarchy_tokens ()->md;
  double delta;
  if (across) {
    guint k = omarchy_reading_key (GTK_WIDGET (h->view), keyval, TRUE);
    if (k == GDK_KEY_Left || k == GDK_KEY_KP_Left)
      delta = -step;
    else if (k == GDK_KEY_Right || k == GDK_KEY_KP_Right)
      delta = step;
    else if (keyval == GDK_KEY_Home)
      delta = -G_MAXINT / 4;
    else if (keyval == GDK_KEY_End)
      delta = G_MAXINT / 4;
    else
      return FALSE;
  } else {
    int v = data_vertical_step (keyval);
    if (v)
      delta = v * step;
    else if (keyval == GDK_KEY_Home)
      delta = -G_MAXINT / 4;
    else if (keyval == GDK_KEY_End)
      delta = G_MAXINT / 4;
    else
      return FALSE;
  }
  handle_target (h);
  handle_resize (h, delta);
  return TRUE;
}

static void
handle_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchySplitHandle *h = OMARCHY_SPLIT_HANDLE (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gboolean hot = h->view->has_cursor || h->dragging || omarchy_widget_hovered (w) || omarchy_widget_focus_shown (w);
  if (hot)
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (0, 0, W, H), MIN (4, p->radius), &p->hover.fill);
  float hair = p->hairline;
  if (split_across (h->view))
    gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (floorf ((W - hair) / 2), 0, hair, H));
  else
    gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, floorf ((H - hair) / 2), W, hair));
}

static void
handle_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchySplitHandle *h = OMARCHY_SPLIT_HANDLE (w);
  *min = *nat = o == h->view->orientation ? omarchy_tokens ()->md : 0;
}

static void
omarchy_split_handle_init (OmarchySplitHandle *h)
{
  GtkWidget *w = GTK_WIDGET (h);
  h->pane = -1;
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_keys (w, handle_key);
  GtkGesture *drag = gtk_gesture_drag_new ();
  g_signal_connect (drag, "drag-begin", G_CALLBACK (handle_drag_begin), h);
  g_signal_connect (drag, "drag-update", G_CALLBACK (handle_drag_update), h);
  g_signal_connect (drag, "drag-end", G_CALLBACK (handle_drag_end), h);
  gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (drag));
}

static void
omarchy_split_handle_class_init (OmarchySplitHandleClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  wc->snapshot = handle_snapshot;
  wc->measure = handle_measure;
  gtk_widget_class_set_css_name (wc, "omarchy-split-handle");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_SEPARATOR);
}

static GtkWidget *
split_handle_new (OmarchySplitView *view)
{
  OmarchySplitHandle *h = g_object_new (OMARCHY_TYPE_SPLIT_HANDLE, NULL);
  h->view = view;
  gtk_widget_set_cursor_from_name (GTK_WIDGET (h), split_across (view) ? "col-resize" : "row-resize");
  gtk_accessible_update_property (GTK_ACCESSIBLE (h), GTK_ACCESSIBLE_PROPERTY_ORIENTATION,
                                  split_across (view) ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, -1);
  return GTK_WIDGET (h);
}

/* =============================================================================== Sidebar === */

typedef struct {
  char *id, *label, *glyph;
} SidebarItem;

typedef struct {
  char *title;
  GPtrArray *items; /* SidebarItem */
} SidebarSection;

static void
sidebar_item_free (gpointer p)
{
  SidebarItem *it = p;
  g_free (it->id);
  g_free (it->label);
  g_free (it->glyph);
  g_free (it);
}

static void
sidebar_section_free (gpointer p)
{
  SidebarSection *sec = p;
  g_free (sec->title);
  g_ptr_array_unref (sec->items);
  g_free (sec);
}

/* One placed part: a section header (item NULL) or a row, its top edge. */
typedef struct {
  SidebarSection *section;
  SidebarItem *item;
  int y, h;
} SidebarPlaced;

/**
 * OmarchySidebar:
 *
 * An app's navigation sidebar: a column padded `sm`, sections `md` apart, each a section header
 * (`caption` bold, `textDim`, inset `controlPaddingX`, `xxs` below) over `popupRowHeight` rows
 * of glyph + label (inset `controlPaddingX`, the label `icon + controlGap` from the glyph's
 * start). One row is selected (selected fill, bold); #OmarchySidebar:cursor-id is the keyboard
 * cursor (hover fill), which the pointer moves too. Up/Down (j/k) walk every row across
 * sections; Return/Space or a click choose (::activated, after #OmarchySidebar:selected-id takes
 * it). #OmarchySidebar:collapsed folds it to a rail of glyphs, omarchy_sidebar_get_rail_width()
 * wide.
 */
struct _OmarchySidebar {
  GtkWidget parent_instance;
  GPtrArray *sections; /* SidebarSection */
  char *selected_id, *cursor_id;
  gboolean has_cursor, collapsed, hovering;
};

enum { SB_PROP_0, SB_PROP_SELECTED_ID, SB_PROP_CURSOR_ID, SB_PROP_HAS_CURSOR, SB_PROP_COLLAPSED, SB_N_PROPS };
static GParamSpec *sb_props[SB_N_PROPS];
enum { SB_ACTIVATED, SB_N_SIGNALS };
static guint sb_signals[SB_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchySidebar, omarchy_sidebar, GTK_TYPE_WIDGET)

static int
sidebar_header_height (const OmarchyTokens *t)
{
  return (int) ceil (t->caption * 0.15) + omarchy_line_height (t->caption) + t->xxs;
}

/* Lay the parts out top-down from `sm`; returns the content's bottom edge. */
static int
sidebar_layout (OmarchySidebar *self, GArray *out)
{
  const OmarchyTokens *t = omarchy_tokens ();
  int y = t->sm;
  gboolean first = TRUE;
  for (guint i = 0; i < self->sections->len; i++) {
    SidebarSection *sec = g_ptr_array_index (self->sections, i);
    gboolean header = !self->collapsed && sec->title && *sec->title;
    if (!header && sec->items->len == 0)
      continue; /* nothing to show: Qt's Column skips it */
    if (!first)
      y += t->md;
    first = FALSE;
    if (header) {
      SidebarPlaced p = { sec, NULL, y, sidebar_header_height (t) };
      if (out)
        g_array_append_val (out, p);
      y += p.h;
    }
    for (guint j = 0; j < sec->items->len; j++) {
      SidebarPlaced p = { sec, g_ptr_array_index (sec->items, j), y, t->popup_row_height };
      if (out)
        g_array_append_val (out, p);
      y += p.h;
    }
  }
  return y;
}

static void
sidebar_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchySidebar *self = OMARCHY_SIDEBAR (w);
  const OmarchyTokens *t = omarchy_tokens ();
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = 0;
    *nat = self->collapsed ? omarchy_sidebar_get_rail_width (self) : omarchy_space (200);
  } else {
    *min = *nat = sidebar_layout (self, NULL) + t->sm;
  }
}

static void
sidebar_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchySidebar *self = OMARCHY_SIDEBAR (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w);
  int pad = p->sm, inset = p->control_padding_x, rh = p->popup_row_height;
  float rw = MAX (0, W - pad * 2);
  gboolean show = data_show_cursor (w, self->has_cursor, self->hovering);
  g_autoptr (GArray) parts = g_array_new (FALSE, FALSE, sizeof (SidebarPlaced));
  sidebar_layout (self, parts);
  for (guint i = 0; i < parts->len; i++) {
    SidebarPlaced *pl = &g_array_index (parts, SidebarPlaced, i);
    if (!pl->item) {
      g_autoptr (PangoLayout) l = data_text (w, pl->section->title, p->caption, TRUE, MAX (0, (int) rw - inset));
      data_draw_text (w, s, l, W, pad + inset, pl->y + (int) ceil (p->caption * 0.15), &p->text_dim);
      continue;
    }
    SidebarItem *it = pl->item;
    gboolean selected = !g_strcmp0 (self->selected_id, it->id);
    data_row_fill (s, data_x (w, W, pad, rw), pl->y, rw, rh, show && !g_strcmp0 (self->cursor_id, it->id), selected);
    data_draw_optical_glyph (w, s, it->glyph, p->icon, W, pad + inset, pl->y, p->icon, rh,
                             selected ? &p->selected.text : &p->text_dim);
    if (!self->collapsed) {
      int lx = inset + p->icon + p->control_gap;
      g_autoptr (PangoLayout) l = data_text (w, it->label, p->body, selected, MAX (0, (int) rw - lx - inset));
      data_draw_text (w, s, l, W, pad + lx, data_center_y (pl->y, rh, p->body),
                      selected ? &p->selected.text : &p->foreground);
    }
  }
}

static SidebarItem *
sidebar_item_at (OmarchySidebar *self, double x, double y)
{
  const OmarchyTokens *t = omarchy_tokens ();
  double W = gtk_widget_get_width (GTK_WIDGET (self));
  if (x < t->sm || x >= W - t->sm)
    return NULL;
  g_autoptr (GArray) parts = g_array_new (FALSE, FALSE, sizeof (SidebarPlaced));
  sidebar_layout (self, parts);
  for (guint i = 0; i < parts->len; i++) {
    SidebarPlaced *pl = &g_array_index (parts, SidebarPlaced, i);
    if (pl->item && y >= pl->y && y < pl->y + pl->h)
      return pl->item;
  }
  return NULL;
}

/* Every row's id, across sections. */
static GPtrArray *
sidebar_ids (OmarchySidebar *self)
{
  GPtrArray *out = g_ptr_array_new ();
  for (guint i = 0; i < self->sections->len; i++) {
    SidebarSection *sec = g_ptr_array_index (self->sections, i);
    for (guint j = 0; j < sec->items->len; j++)
      g_ptr_array_add (out, ((SidebarItem *) g_ptr_array_index (sec->items, j))->id);
  }
  return out;
}

static void
sidebar_activate (OmarchySidebar *self, const char *id)
{
  if (!id || !*id)
    return;
  g_autofree char *copy = g_strdup (id);
  omarchy_sidebar_set_selected_id (self, copy);
  g_signal_emit (self, sb_signals[SB_ACTIVATED], 0, copy);
}

static void
sidebar_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchySidebar *self = OMARCHY_SIDEBAR (w);
  SidebarItem *it = button == GDK_BUTTON_PRIMARY ? sidebar_item_at (self, x, y) : NULL;
  if (!it)
    return;
  gtk_widget_grab_focus (w);
  sidebar_activate (self, it->id);
}

static gboolean
sidebar_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchySidebar *self = OMARCHY_SIDEBAR (w);
  int step = data_vertical_step (keyval);
  if (step) {
    g_autoptr (GPtrArray) ids = sidebar_ids (self);
    if (ids->len == 0)
      return TRUE;
    int at = -1;
    for (guint i = 0; i < ids->len; i++)
      if (!g_strcmp0 (g_ptr_array_index (ids, i), self->cursor_id))
        at = i;
    int from = at < 0 ? (step > 0 ? -1 : (int) ids->len) : at;
    omarchy_sidebar_set_cursor_id (self, g_ptr_array_index (ids, CLAMP (from + step, 0, (int) ids->len - 1)));
    return TRUE;
  }
  if (omarchy_key_is_activate (keyval)) {
    sidebar_activate (self, self->cursor_id);
    return TRUE;
  }
  return FALSE;
}

static void
sidebar_motion (GtkEventControllerMotion *c, double x, double y, OmarchySidebar *self)
{
  SidebarItem *it = sidebar_item_at (self, x, y);
  if (it)
    omarchy_sidebar_set_cursor_id (self, it->id);
  if (!!it != self->hovering) {
    self->hovering = !!it;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
sidebar_leave (GtkEventControllerMotion *c, OmarchySidebar *self)
{
  self->hovering = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
omarchy_sidebar_init (OmarchySidebar *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->sections = g_ptr_array_new_with_free_func (sidebar_section_free);
  self->selected_id = g_strdup ("");
  self->cursor_id = g_strdup ("");
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, sidebar_clicked);
  omarchy_widget_add_keys (w, sidebar_key);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "enter", G_CALLBACK (sidebar_motion), self);
  g_signal_connect (m, "motion", G_CALLBACK (sidebar_motion), self);
  g_signal_connect (m, "leave", G_CALLBACK (sidebar_leave), self);
  gtk_widget_add_controller (w, m);
}

static void
sidebar_finalize (GObject *o)
{
  OmarchySidebar *self = OMARCHY_SIDEBAR (o);
  g_ptr_array_unref (self->sections);
  g_free (self->selected_id);
  g_free (self->cursor_id);
  G_OBJECT_CLASS (omarchy_sidebar_parent_class)->finalize (o);
}

static void
sidebar_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchySidebar *self = OMARCHY_SIDEBAR (o);
  switch (id) {
  case SB_PROP_SELECTED_ID: omarchy_sidebar_set_selected_id (self, g_value_get_string (v)); break;
  case SB_PROP_CURSOR_ID: omarchy_sidebar_set_cursor_id (self, g_value_get_string (v)); break;
  case SB_PROP_HAS_CURSOR: omarchy_sidebar_set_has_cursor (self, g_value_get_boolean (v)); break;
  case SB_PROP_COLLAPSED: omarchy_sidebar_set_collapsed (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
sidebar_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchySidebar *self = OMARCHY_SIDEBAR (o);
  switch (id) {
  case SB_PROP_SELECTED_ID: g_value_set_string (v, self->selected_id); break;
  case SB_PROP_CURSOR_ID: g_value_set_string (v, self->cursor_id); break;
  case SB_PROP_HAS_CURSOR: g_value_set_boolean (v, self->has_cursor); break;
  case SB_PROP_COLLAPSED: g_value_set_boolean (v, self->collapsed); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_sidebar_class_init (OmarchySidebarClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = sidebar_finalize;
  oc->set_property = sidebar_set_property;
  oc->get_property = sidebar_get_property;
  wc->measure = sidebar_measure;
  wc->snapshot = sidebar_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchySidebar:selected-id:
   *
   * The chosen row's id (selected fill, bold), or "".
   */
  sb_props[SB_PROP_SELECTED_ID] = g_param_spec_string ("selected-id", NULL, NULL, "", f);
  /**
   * OmarchySidebar:cursor-id:
   *
   * The keyboard cursor's row (hover fill), or "". The pointer moves it too.
   */
  sb_props[SB_PROP_CURSOR_ID] = g_param_spec_string ("cursor-id", NULL, NULL, "", f);
  /**
   * OmarchySidebar:has-cursor:
   *
   * Show the cursor row's hover look without focus.
   */
  sb_props[SB_PROP_HAS_CURSOR] = g_param_spec_boolean ("has-cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchySidebar:collapsed:
   *
   * Hide headers and labels: a rail of glyph rows.
   */
  sb_props[SB_PROP_COLLAPSED] = g_param_spec_boolean ("collapsed", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, SB_N_PROPS, sb_props);
  /**
   * OmarchySidebar::activated:
   * @self: the sidebar
   * @sidebar: the sidebar
   * @id: the row chosen (now #OmarchySidebar:selected-id)
   */
  sb_signals[SB_ACTIVATED] = g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                           NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  gtk_widget_class_set_css_name (wc, "omarchy-sidebar");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_NAVIGATION);
}

/**
 * omarchy_sidebar_new:
 *
 * Returns: (transfer floating): an empty sidebar; add sections and their items
 */
GtkWidget *
omarchy_sidebar_new (void)
{
  return g_object_new (OMARCHY_TYPE_SIDEBAR, NULL);
}

/**
 * omarchy_sidebar_append_section:
 * @self: the sidebar
 * @title: (nullable): the section's header, or %NULL for none
 *
 * Starts a section; omarchy_sidebar_append_item() adds to the last one.
 */
void
omarchy_sidebar_append_section (OmarchySidebar *self, const char *title)
{
  SidebarSection *sec = g_new0 (SidebarSection, 1);
  sec->title = g_strdup (title ? title : "");
  sec->items = g_ptr_array_new_with_free_func (sidebar_item_free);
  g_ptr_array_add (self->sections, sec);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

/**
 * omarchy_sidebar_append_item:
 * @self: the sidebar
 * @id: the row's id
 * @label: (nullable): its label
 * @glyph: (nullable): a Nerd Font glyph
 *
 * Adds a row to the last section (starting an untitled one when there is none).
 */
void
omarchy_sidebar_append_item (OmarchySidebar *self, const char *id, const char *label, const char *glyph)
{
  g_return_if_fail (id != NULL);
  if (self->sections->len == 0)
    omarchy_sidebar_append_section (self, NULL);
  SidebarSection *sec = g_ptr_array_index (self->sections, self->sections->len - 1);
  SidebarItem *it = g_new0 (SidebarItem, 1);
  it->id = g_strdup (id);
  it->label = g_strdup (label ? label : "");
  it->glyph = g_strdup (glyph ? glyph : "");
  g_ptr_array_add (sec->items, it);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
omarchy_sidebar_remove_all (OmarchySidebar *self)
{
  g_ptr_array_set_size (self->sections, 0);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

/**
 * omarchy_sidebar_get_rail_width:
 * @self: the sidebar
 *
 * Returns: the collapsed width: `sm × 2 + controlPaddingX × 2 + icon`
 */
int
omarchy_sidebar_get_rail_width (OmarchySidebar *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return t->sm * 2 + t->control_padding_x * 2 + t->icon;
}

OMARCHY_STRING_ACCESSORS (OmarchySidebar, sidebar, selected_id, sb_props, SB_PROP_SELECTED_ID, FALSE)
OMARCHY_STRING_ACCESSORS (OmarchySidebar, sidebar, cursor_id, sb_props, SB_PROP_CURSOR_ID, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchySidebar, sidebar, has_cursor, sb_props, SB_PROP_HAS_CURSOR, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchySidebar, sidebar, collapsed, sb_props, SB_PROP_COLLAPSED, TRUE)
