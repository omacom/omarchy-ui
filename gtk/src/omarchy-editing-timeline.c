/* omarchy-editing-timeline.c: Timeline (Timeline.qml) and Scrubber (Scrubber.qml), drawn as the
 * Qt kit draws them. */
#include "omarchy-editing-private.h"

#include <math.h>
#include <string.h>

/* ========================================================================== Timeline ======= */

/**
 * OmarchyTimeline:
 *
 * A time ruler over one or more clip tracks, with a playhead.
 *
 * The ruler is `space(24)` tall, with labeled major ticks (`caption`, textDim, m:ss) at the
 * first step of 0.1 s … 1 h that keeps them `space(90)` apart, and four minor ticks between
 * (textFaint). The tracks share the height under it, `xs` apart, as lanes in the normal fill;
 * their clips are control glass with a `bodySmall` label. The selected clip takes the selected
 * fill, an accent border, a bold label and an #OmarchyTrimHandle on each end. The playhead is
 * accent, hot accent when hot (the pointer on its head, keyboard focus,
 * #OmarchyTimeline:cursor).
 *
 * Press or drag on the ruler or an empty lane to move the playhead (::seek), press a clip to
 * select it (::clip-pressed), drag a trim handle or nudge it with the arrow keys to trim it
 * (::trimmed), and turn the wheel to zoom around the pointer (::view-changed). Left/Right move
 * the playhead a second (Shift: a major step), Home/End jump to the ends. Each updates the
 * timeline first, then emits.
 */
typedef struct {
  double start, end;
  char *label;
} Clip;

typedef enum { PRESS_NONE, PRESS_SCRUB, PRESS_PLAYHEAD } PressMode;

struct _OmarchyTimeline {
  GtkWidget parent_instance;
  double duration, position, zoom, view_start, max_zoom;
  GPtrArray *tracks; /* of GArray of Clip */
  int selected_track, selected_clip;
  gboolean cursor;
  GtkWidget *handles[2]; /* left, right */
  double trim_origin[2];
  /* pointer */
  double px, py;
  gboolean inside;
  int hover_track, hover_clip;
  gboolean playhead_hover;
  PressMode press;
  double press_x;
};

enum { L_PROP_0, L_PROP_DURATION, L_PROP_POSITION, L_PROP_SELECTED_TRACK, L_PROP_SELECTED_CLIP, L_PROP_ZOOM,
       L_PROP_VIEW_START, L_PROP_MAX_ZOOM, L_PROP_CURSOR, L_N_PROPS };
static GParamSpec *l_props[L_N_PROPS];
enum { L_SEEK, L_CLIP_PRESSED, L_TRIMMED, L_VIEW_CHANGED, L_N_SIGNALS };
static guint l_signals[L_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyTimeline, omarchy_timeline, GTK_TYPE_WIDGET)

static void
clip_clear (gpointer p)
{
  g_free (((Clip *) p)->label);
}

static GArray *
track_at (OmarchyTimeline *self, int track)
{
  if (track < 0 || track >= (int) self->tracks->len)
    return NULL;
  return g_ptr_array_index (self->tracks, track);
}

static Clip *
clip_at (OmarchyTimeline *self, int track, int clip)
{
  GArray *t = track_at (self, track);
  if (!t || clip < 0 || clip >= (int) t->len)
    return NULL;
  return &g_array_index (t, Clip, clip);
}

typedef struct {
  float W, H;
  int ruler_h, lane_gap, inset, lane_h;
  float small;
  double pps;
} TimelineGeo;

static TimelineGeo
tl_geo (OmarchyTimeline *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  TimelineGeo g;
  g.W = gtk_widget_get_width (GTK_WIDGET (self));
  g.H = gtk_widget_get_height (GTK_WIDGET (self));
  g.ruler_h = omarchy_space (24);
  g.lane_gap = t->xs;
  g.inset = t->xxs;
  g.small = MIN (4, t->radius);
  g.pps = g.W * self->zoom / MAX (0.001, self->duration);
  int n = self->tracks->len;
  g.lane_h = n > 0 ? (int) floor ((g.H - g.ruler_h - g.lane_gap * (n - 1)) / (double) n) : 0;
  return g;
}

static double
tl_x_at (OmarchyTimeline *self, const TimelineGeo *g, double time)
{
  return omarchy_editing_round ((time - self->view_start) * g->pps);
}

static double
tl_time_at (OmarchyTimeline *self, const TimelineGeo *g, double x)
{
  return CLAMP (self->view_start + x / MAX (1e-9, g->pps), 0, self->duration);
}

static float
tl_lane_y (const TimelineGeo *g, int index)
{
  return g->ruler_h + index * (g->lane_h + g->lane_gap);
}

static double
tick_step (OmarchyTimeline *self, const TimelineGeo *g)
{
  static const double steps[] = { 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
  double target = omarchy_space (90) / MAX (0.001, g->pps);
  for (guint i = 0; i < G_N_ELEMENTS (steps); i++)
    if (steps[i] >= target)
      return steps[i];
  return 3600;
}

static char *
tl_format (double seconds, double step)
{
  double s = MAX (0, omarchy_editing_round (seconds * 10) / 10);
  double m = floor (s / 60);
  double rest = s - m * 60;
  double whole = floor (rest);
  GString *out = g_string_new (NULL);
  g_string_printf (out, "%.0f:%s%.0f", m, whole < 10 ? "0" : "", whole);
  if (step < 1)
    g_string_append_printf (out, ".%.0f", omarchy_editing_round ((rest - whole) * 10));
  return g_string_free (out, FALSE);
}

static gboolean
tl_hot (OmarchyTimeline *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  return self->cursor || omarchy_widget_focus_shown (w) || self->playhead_hover || self->press == PRESS_PLAYHEAD;
}

/* The clip rectangle, in the timeline. */
static graphene_rect_t
clip_rect (OmarchyTimeline *self, const TimelineGeo *g, int track, const Clip *c)
{
  float x = (float) tl_x_at (self, g, c->start);
  float w = (float) tl_x_at (self, g, c->end) - x;
  return GRAPHENE_RECT_INIT (x, tl_lane_y (g, track) + g->inset, w, g->lane_h - g->inset * 2);
}

static gboolean
tl_clip_hit (OmarchyTimeline *self, double x, double y, int *track, int *clip)
{
  TimelineGeo g = tl_geo (self);
  for (int i = (int) self->tracks->len - 1; i >= 0; i--) {
    GArray *t = track_at (self, i);
    for (int j = (int) t->len - 1; j >= 0; j--) {
      graphene_rect_t r = clip_rect (self, &g, i, &g_array_index (t, Clip, j));
      if (graphene_rect_contains_point (&r, &GRAPHENE_POINT_INIT (x, y))) {
        *track = i;
        *clip = j;
        return TRUE;
      }
    }
  }
  return FALSE;
}

static gboolean
tl_playhead_visible (OmarchyTimeline *self)
{
  return self->position >= self->view_start && self->position <= self->view_start + self->duration / self->zoom;
}

/* The playhead's head plus `sm` on each side. */
static gboolean
tl_playhead_hit (OmarchyTimeline *self, double x, double y)
{
  if (!tl_playhead_visible (self))
    return FALSE;
  TimelineGeo g = tl_geo (self);
  int head = omarchy_space (10), sm = omarchy_tokens ()->sm;
  double hx = tl_x_at (self, &g, self->position) - omarchy_editing_round (head / 2.0);
  double hy = g.ruler_h - head;
  return x >= hx - sm && x < hx + head + sm && y >= hy && y < hy + head;
}

static void
tl_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = omarchy_space (60);
    *nat = omarchy_space (720);
  } else {
    *min = omarchy_space (24);
    *nat = omarchy_space (160);
  }
}

static void
tl_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyTimeline *self = OMARCHY_TIMELINE (w);
  TimelineGeo g = tl_geo (self);
  Clip *c = clip_at (self, self->selected_track, self->selected_clip);
  for (int i = 0; i < 2; i++) {
    gtk_widget_set_child_visible (self->handles[i], c != NULL);
    if (!c)
      continue;
    graphene_rect_t r = clip_rect (self, &g, self->selected_track, c);
    int hw;
    gtk_widget_measure (self->handles[i], GTK_ORIENTATION_HORIZONTAL, -1, NULL, &hw, NULL, NULL);
    int x = i == 0 ? (int) r.origin.x : (int) (r.origin.x + r.size.width) - hw;
    GtkAllocation a = { x, (int) r.origin.y, hw, MAX (0, (int) r.size.height) };
    gtk_widget_size_allocate (self->handles[i], &a, -1);
  }
}

static void
tl_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyTimeline *self = OMARCHY_TIMELINE (w);
  const OmarchyTokens *p = omarchy_paint ();
  const OmarchyTokens *t = omarchy_tokens ();
  TimelineGeo g = tl_geo (self);
  if (g.W <= 0 || g.H <= 0)
    return;
  gtk_snapshot_push_clip (s, &GRAPHENE_RECT_INIT (0, 0, g.W, g.H));

  /* ---- ruler */
  double step = tick_step (self, &g);
  double minor = step / 5;
  double view_end = self->view_start + self->duration / self->zoom;
  long first = (long) MAX (0, floor (self->view_start / minor));
  long last = (long) ceil (view_end / minor);
  int major_h = omarchy_space (6), minor_h = omarchy_space (3);
  for (long i = first; i <= last; i++) {
    double time = i * minor;
    if (time > self->duration + 1e-6)
      break;
    int h = i % 5 == 0 ? major_h : minor_h;
    float x = (float) MIN (g.W - 1, tl_x_at (self, &g, time));
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x, g.ruler_h - h, 1, h), 0, &p->text_faint);
  }
  first = (long) MAX (0, ceil (self->view_start / step - 1e-6));
  last = (long) floor (MIN (self->duration, view_end) / step + 1e-6);
  for (long i = first; i <= last; i++) {
    double time = i * step;
    g_autofree char *text = tl_format (time, step);
    g_autoptr (PangoLayout) l = omarchy_layout (w, text, t->caption, FALSE);
    int tw;
    omarchy_layout_size (l, &tw, NULL);
    double x = MAX (0, MIN (g.W - tw, tl_x_at (self, &g, time) - omarchy_editing_round (tw / 2.0)));
    omarchy_draw_layout (s, l, (float) x, t->xxs, &p->text_dim);
  }

  /* ---- lanes and clips */
  int line = omarchy_line_height (t->body_small);
  float stroke = MAX (1, omarchy_space (2));
  for (guint i = 0; i < self->tracks->len; i++) {
    GArray *track = g_ptr_array_index (self->tracks, i);
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (0, tl_lane_y (&g, i), g.W, g.lane_h), g.small, &p->normal.fill);
    for (guint j = 0; j < track->len; j++) {
      const Clip *c = &g_array_index (track, Clip, j);
      graphene_rect_t r = clip_rect (self, &g, i, c);
      gboolean selected = (int) i == self->selected_track && (int) j == self->selected_clip;
      gboolean hover = (int) i == self->hover_track && (int) j == self->hover_clip;
      OmarchyBorder sel = omarchy_border_flat (&p->accent, stroke);
      const OmarchyBorder *b = selected ? &sel : hover ? &p->hover.border : &p->normal.border;
      const GdkRGBA *fill = selected ? &p->selected.fill : hover ? &p->hover.fill : &p->normal.fill;
      omarchy_draw_surface (s, &r, g.small, fill, b);
      int label_inset = selected ? omarchy_space (8) + t->md : t->control_padding_x;
      int avail = (int) r.size.width - label_inset * 2;
      if (c->label && *c->label && avail > 0) {
        g_autoptr (PangoLayout) l = omarchy_layout (w, c->label, t->body_small, selected);
        pango_layout_set_width (l, avail * PANGO_SCALE);
        pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
        gtk_snapshot_push_clip (s, &r);
        omarchy_draw_layout (s, l, r.origin.x + label_inset, r.origin.y + (r.size.height - line) / 2.0f,
                             &p->foreground);
        gtk_snapshot_pop (s);
      }
    }
  }
  for (int i = 0; i < 2; i++)
    gtk_widget_snapshot_child (w, self->handles[i], s);

  /* ---- playhead */
  if (tl_playhead_visible (self)) {
    int line_w = MAX (1, omarchy_space (2)), head = omarchy_space (10);
    GdkRGBA c = tl_hot (self) ? omarchy_editing_hot (&p->accent) : p->accent;
    float x = (float) (tl_x_at (self, &g, self->position) - omarchy_editing_round (head / 2.0));
    float y = g.ruler_h - head;
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x, y, head, head), g.small, &c);
    omarchy_draw_fill (s,
                       &GRAPHENE_RECT_INIT (x + (float) omarchy_editing_round ((head - line_w) / 2.0), y + head, line_w,
                                            g.H - y - head),
                       0, &c);
  }
  gtk_snapshot_pop (s);
}

static void
tl_set_position (OmarchyTimeline *self, double position)
{
  position = CLAMP (position, 0, MAX (0, self->duration));
  if (position == self->position)
    return;
  self->position = position;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_POSITION]);
}

static void
tl_seek (OmarchyTimeline *self, double time)
{
  tl_set_position (self, time);
  g_signal_emit (self, l_signals[L_SEEK], 0, self->position);
}

static void
tl_pointer (OmarchyTimeline *self, double x, double y, gboolean inside)
{
  self->px = x;
  self->py = y;
  self->inside = inside;
  int track = -1, clip = -1;
  gboolean head = inside && tl_playhead_hit (self, x, y);
  if (inside && !head)
    tl_clip_hit (self, x, y, &track, &clip);
  if (track != self->hover_track || clip != self->hover_clip || head != self->playhead_hover) {
    self->hover_track = track;
    self->hover_clip = clip;
    self->playhead_hover = head;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), head ? "ew-resize" : clip >= 0 ? "pointer" : NULL);
}

static void
tl_motion (GtkEventControllerMotion *c, double x, double y, OmarchyTimeline *self)
{
  tl_pointer (self, x, y, TRUE);
}

static void
tl_leave (GtkEventControllerMotion *c, OmarchyTimeline *self)
{
  tl_pointer (self, self->px, self->py, FALSE);
}

static void
tl_drag_begin (GtkWidget *w, double x, double y)
{
  OmarchyTimeline *self = OMARCHY_TIMELINE (w);
  if (gtk_widget_get_focusable (w))
    gtk_widget_grab_focus (w);
  self->press_x = x;
  int track, clip;
  if (tl_playhead_hit (self, x, y)) {
    self->press = PRESS_PLAYHEAD;
    gtk_widget_queue_draw (w);
  } else if (tl_clip_hit (self, x, y, &track, &clip)) {
    self->press = PRESS_NONE;
    omarchy_timeline_set_selected (self, track, clip);
    g_signal_emit (self, l_signals[L_CLIP_PRESSED], 0, track, clip);
  } else {
    self->press = PRESS_SCRUB;
    TimelineGeo g = tl_geo (self);
    tl_seek (self, tl_time_at (self, &g, x));
  }
}

static void
tl_drag_update (GtkWidget *w, double dx, double dy)
{
  OmarchyTimeline *self = OMARCHY_TIMELINE (w);
  if (self->press == PRESS_NONE)
    return;
  TimelineGeo g = tl_geo (self);
  tl_seek (self, tl_time_at (self, &g, self->press_x + dx));
}

static void
tl_drag_end (GtkWidget *w, double dx, double dy)
{
  OmarchyTimeline *self = OMARCHY_TIMELINE (w);
  self->press = PRESS_NONE;
  gtk_widget_queue_draw (w);
}

static void
clamp_view (OmarchyTimeline *self, double *zoom, double *start)
{
  *zoom = CLAMP (*zoom, 1, MAX (1, self->max_zoom));
  double visible = self->duration / *zoom;
  *start = MAX (0, MIN (self->duration - visible, *start));
}

static gboolean
tl_scroll (GtkEventControllerScroll *c, double dx, double dy, OmarchyTimeline *self)
{
  double steps = -dy;
  if (steps == 0)
    return FALSE;
  TimelineGeo g = tl_geo (self);
  double x = self->inside ? self->px : g.W / 2;
  double anchor = tl_time_at (self, &g, x);
  double z = self->zoom * pow (1.25, steps), unused = 0;
  clamp_view (self, &z, &unused);
  double start = anchor - x / (g.W * z / MAX (0.001, self->duration));
  clamp_view (self, &z, &start);
  omarchy_timeline_set_view (self, z, start);
  g_signal_emit (self, l_signals[L_VIEW_CHANGED], 0, self->zoom, self->view_start);
  return TRUE;
}

static gboolean
tl_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyTimeline *self = OMARCHY_TIMELINE (w);
  TimelineGeo g = tl_geo (self);
  double step = (state & GDK_SHIFT_MASK) ? tick_step (self, &g) : 1;
  if (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left)
    tl_seek (self, MAX (0, self->position - step));
  else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right)
    tl_seek (self, MIN (self->duration, self->position + step));
  else if (keyval == GDK_KEY_Home || keyval == GDK_KEY_KP_Home)
    tl_seek (self, 0);
  else if (keyval == GDK_KEY_End || keyval == GDK_KEY_KP_End)
    tl_seek (self, self->duration);
  else
    return FALSE;
  return TRUE;
}

/* Trim the selected clip's `edge` (0 left, 1 right) to `time`, then tell. */
static void
tl_trim (OmarchyTimeline *self, int edge, double time)
{
  Clip *c = clip_at (self, self->selected_track, self->selected_clip);
  if (!c)
    return;
  time = CLAMP (time, 0, self->duration);
  if (edge == 0)
    c->start = time = MIN (time, c->end);
  else
    c->end = time = MAX (time, c->start);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_signal_emit (self, l_signals[L_TRIMMED], 0, self->selected_track, self->selected_clip, edge == 0 ? "left" : "right",
                 time);
}

static int
handle_index (OmarchyTimeline *self, GtkWidget *h)
{
  return h == self->handles[1] ? 1 : 0;
}

static void
handle_dragging (GtkWidget *h, GParamSpec *pspec, OmarchyTimeline *self)
{
  int i = handle_index (self, h);
  Clip *c = clip_at (self, self->selected_track, self->selected_clip);
  if (c && omarchy_trim_handle_get_dragging (OMARCHY_TRIM_HANDLE (h)))
    self->trim_origin[i] = i == 0 ? c->start : c->end;
}

static void
handle_moved (GtkWidget *h, double dx, double dy, OmarchyTimeline *self)
{
  int i = handle_index (self, h);
  TimelineGeo g = tl_geo (self);
  tl_trim (self, i, self->trim_origin[i] + dx / MAX (1e-9, g.pps));
}

static void
handle_nudged (GtkWidget *h, double dx, double dy, OmarchyTimeline *self)
{
  int i = handle_index (self, h);
  Clip *c = clip_at (self, self->selected_track, self->selected_clip);
  if (!c)
    return;
  TimelineGeo g = tl_geo (self);
  tl_trim (self, i, (i == 0 ? c->start : c->end) + dx / MAX (1e-9, g.pps));
}

static void
omarchy_timeline_init (OmarchyTimeline *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->duration = 30;
  self->zoom = 1;
  self->max_zoom = 64;
  self->selected_track = self->selected_clip = -1;
  self->hover_track = self->hover_clip = -1;
  self->tracks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_array_unref);
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  const char *edges[] = { "left", "right" };
  for (int i = 0; i < 2; i++) {
    self->handles[i] = omarchy_trim_handle_new (edges[i]);
    gtk_widget_set_parent (self->handles[i], w);
    gtk_widget_set_child_visible (self->handles[i], FALSE);
    g_signal_connect (self->handles[i], "notify::dragging", G_CALLBACK (handle_dragging), self);
    g_signal_connect (self->handles[i], "moved", G_CALLBACK (handle_moved), self);
    g_signal_connect (self->handles[i], "nudged", G_CALLBACK (handle_nudged), self);
  }
  omarchy_editing_add_drag (w, tl_drag_begin, tl_drag_update, tl_drag_end);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "enter", G_CALLBACK (tl_motion), self);
  g_signal_connect (m, "motion", G_CALLBACK (tl_motion), self);
  g_signal_connect (m, "leave", G_CALLBACK (tl_leave), self);
  gtk_widget_add_controller (w, m);
  GtkEventController *sc = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL
                                                            | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
  g_signal_connect (sc, "scroll", G_CALLBACK (tl_scroll), self);
  gtk_widget_add_controller (w, sc);
  omarchy_widget_add_keys (w, tl_key);
}

static void
tl_dispose (GObject *o)
{
  OmarchyTimeline *self = OMARCHY_TIMELINE (o);
  for (int i = 0; i < 2; i++)
    g_clear_pointer (&self->handles[i], gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_timeline_parent_class)->dispose (o);
}

static void
tl_finalize (GObject *o)
{
  g_ptr_array_unref (OMARCHY_TIMELINE (o)->tracks);
  G_OBJECT_CLASS (omarchy_timeline_parent_class)->finalize (o);
}

static void
tl_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyTimeline *self = OMARCHY_TIMELINE (o);
  switch (id) {
  case L_PROP_DURATION: omarchy_timeline_set_duration (self, g_value_get_double (v)); break;
  case L_PROP_POSITION: omarchy_timeline_set_position (self, g_value_get_double (v)); break;
  case L_PROP_SELECTED_TRACK: omarchy_timeline_set_selected (self, g_value_get_int (v), self->selected_clip); break;
  case L_PROP_SELECTED_CLIP: omarchy_timeline_set_selected (self, self->selected_track, g_value_get_int (v)); break;
  case L_PROP_ZOOM: omarchy_timeline_set_view (self, g_value_get_double (v), self->view_start); break;
  case L_PROP_VIEW_START: omarchy_timeline_set_view (self, self->zoom, g_value_get_double (v)); break;
  case L_PROP_MAX_ZOOM: omarchy_timeline_set_max_zoom (self, g_value_get_double (v)); break;
  case L_PROP_CURSOR: omarchy_timeline_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
tl_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyTimeline *self = OMARCHY_TIMELINE (o);
  switch (id) {
  case L_PROP_DURATION: g_value_set_double (v, self->duration); break;
  case L_PROP_POSITION: g_value_set_double (v, self->position); break;
  case L_PROP_SELECTED_TRACK: g_value_set_int (v, self->selected_track); break;
  case L_PROP_SELECTED_CLIP: g_value_set_int (v, self->selected_clip); break;
  case L_PROP_ZOOM: g_value_set_double (v, self->zoom); break;
  case L_PROP_VIEW_START: g_value_set_double (v, self->view_start); break;
  case L_PROP_MAX_ZOOM: g_value_set_double (v, self->max_zoom); break;
  case L_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_timeline_class_init (OmarchyTimelineClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = tl_dispose;
  oc->finalize = tl_finalize;
  oc->set_property = tl_set_property;
  oc->get_property = tl_get_property;
  wc->measure = tl_measure;
  wc->size_allocate = tl_allocate;
  wc->snapshot = tl_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyTimeline:duration:
   *
   * The length of the timeline, in seconds.
   */
  l_props[L_PROP_DURATION] = g_param_spec_double ("duration", NULL, NULL, 0, G_MAXDOUBLE, 30, f);
  /**
   * OmarchyTimeline:position:
   *
   * Where the playhead is, in seconds.
   */
  l_props[L_PROP_POSITION] = g_param_spec_double ("position", NULL, NULL, 0, G_MAXDOUBLE, 0, f);
  /**
   * OmarchyTimeline:selected-track:
   *
   * The selected clip's track, or -1.
   */
  l_props[L_PROP_SELECTED_TRACK] = g_param_spec_int ("selected-track", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyTimeline:selected-clip:
   *
   * The selected clip's index in its track, or -1.
   */
  l_props[L_PROP_SELECTED_CLIP] = g_param_spec_int ("selected-clip", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyTimeline:zoom:
   *
   * 1 shows the whole duration; more zooms in (up to #OmarchyTimeline:max-zoom).
   */
  l_props[L_PROP_ZOOM] = g_param_spec_double ("zoom", NULL, NULL, 1, G_MAXDOUBLE, 1, f);
  /**
   * OmarchyTimeline:view-start:
   *
   * The time at the left edge, in seconds.
   */
  l_props[L_PROP_VIEW_START] = g_param_spec_double ("view-start", NULL, NULL, 0, G_MAXDOUBLE, 0, f);
  /**
   * OmarchyTimeline:max-zoom:
   *
   * How far the wheel zooms in.
   */
  l_props[L_PROP_MAX_ZOOM] = g_param_spec_double ("max-zoom", NULL, NULL, 1, G_MAXDOUBLE, 64, f);
  /**
   * OmarchyTimeline:cursor:
   *
   * Draws the playhead hot (Qt's `hasCursor`).
   */
  l_props[L_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, L_N_PROPS, l_props);
  /**
   * OmarchyTimeline::seek:
   * @self: the timeline
   * @time: the playhead's new position, in seconds
   *
   * A press or drag on the ruler or an empty lane, or Left/Right/Home/End.
   */
  l_signals[L_SEEK] = g_signal_new ("seek", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 1, G_TYPE_DOUBLE);
  /**
   * OmarchyTimeline::clip-pressed:
   * @self: the timeline
   * @track: the clip's track
   * @clip: the clip's index in it
   *
   * A clip was pressed; it is selected now.
   */
  l_signals[L_CLIP_PRESSED] = g_signal_new ("clip-pressed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                            NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
  /**
   * OmarchyTimeline::trimmed:
   * @self: the timeline
   * @track: the clip's track
   * @clip: the clip's index in it
   * @edge: `left` (its start) or `right` (its end)
   * @time: the edge's new time, in seconds
   *
   * A trim handle was dragged or nudged; the clip has the new time now.
   */
  l_signals[L_TRIMMED] = g_signal_new ("trimmed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 4, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_DOUBLE);
  /**
   * OmarchyTimeline::view-changed:
   * @self: the timeline
   * @zoom: the new zoom
   * @view_start: the new time at the left edge
   *
   * The wheel zoomed ×1.25 per notch around the pointer.
   */
  l_signals[L_VIEW_CHANGED] = g_signal_new ("view-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                            NULL, NULL, G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  gtk_widget_class_set_css_name (wc, "omarchy-timeline");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_timeline_new:
 * @duration: its length, in seconds
 *
 * Returns: (transfer floating): a timeline with no tracks
 */
GtkWidget *
omarchy_timeline_new (double duration)
{
  return g_object_new (OMARCHY_TYPE_TIMELINE, "duration", duration, NULL);
}

double
omarchy_timeline_get_duration (OmarchyTimeline *self)
{
  return self->duration;
}

void
omarchy_timeline_set_duration (OmarchyTimeline *self, double duration)
{
  duration = MAX (0, duration);
  if (duration == self->duration)
    return;
  self->duration = duration;
  self->position = MIN (self->position, duration);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_DURATION]);
}

double
omarchy_timeline_get_position (OmarchyTimeline *self)
{
  return self->position;
}

void
omarchy_timeline_set_position (OmarchyTimeline *self, double position)
{
  tl_set_position (self, position);
}

/**
 * omarchy_timeline_add_track:
 * @self: the timeline
 *
 * Adds an empty track at the bottom.
 *
 * Returns: its index
 */
int
omarchy_timeline_add_track (OmarchyTimeline *self)
{
  GArray *t = g_array_new (FALSE, TRUE, sizeof (Clip));
  g_array_set_clear_func (t, clip_clear);
  g_ptr_array_add (self->tracks, t);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  return self->tracks->len - 1;
}

/**
 * omarchy_timeline_append_clip:
 * @self: the timeline
 * @track: the track's index
 * @start: where it starts, in seconds
 * @end: where it ends
 * @label: (nullable): its label
 *
 * Returns: the clip's index in the track, or -1 when there's no such track
 */
int
omarchy_timeline_append_clip (OmarchyTimeline *self, int track, double start, double end, const char *label)
{
  GArray *t = track_at (self, track);
  if (!t)
    return -1;
  Clip c = { start, MAX (start, end), g_strdup (label ? label : "") };
  g_array_append_val (t, c);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  return t->len - 1;
}

/**
 * omarchy_timeline_set_clip_range:
 * @self: the timeline
 * @track: the track's index
 * @clip: the clip's index in it
 * @start: where it starts now, in seconds
 * @end: where it ends now
 */
void
omarchy_timeline_set_clip_range (OmarchyTimeline *self, int track, int clip, double start, double end)
{
  Clip *c = clip_at (self, track, clip);
  if (!c)
    return;
  c->start = start;
  c->end = MAX (start, end);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * omarchy_timeline_get_clip:
 * @self: the timeline
 * @track: the track's index
 * @clip: the clip's index in it
 * @start: (out) (optional): where it starts
 * @end: (out) (optional): where it ends
 * @label: (out) (optional) (transfer none): its label
 *
 * Returns: %FALSE when there's no such clip
 */
gboolean
omarchy_timeline_get_clip (OmarchyTimeline *self, int track, int clip, double *start, double *end, const char **label)
{
  Clip *c = clip_at (self, track, clip);
  if (!c)
    return FALSE;
  if (start)
    *start = c->start;
  if (end)
    *end = c->end;
  if (label)
    *label = c->label;
  return TRUE;
}

int
omarchy_timeline_get_n_tracks (OmarchyTimeline *self)
{
  return self->tracks->len;
}

int
omarchy_timeline_get_n_clips (OmarchyTimeline *self, int track)
{
  GArray *t = track_at (self, track);
  return t ? (int) t->len : 0;
}

/**
 * omarchy_timeline_remove_all:
 * @self: the timeline
 *
 * Removes every track (and the selection).
 */
void
omarchy_timeline_remove_all (OmarchyTimeline *self)
{
  g_ptr_array_set_size (self->tracks, 0);
  omarchy_timeline_set_selected (self, -1, -1);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

int
omarchy_timeline_get_selected_track (OmarchyTimeline *self)
{
  return self->selected_track;
}

int
omarchy_timeline_get_selected_clip (OmarchyTimeline *self)
{
  return self->selected_clip;
}

/**
 * omarchy_timeline_set_selected:
 * @self: the timeline
 * @track: the track, or -1
 * @clip: the clip in it, or -1
 *
 * Selects a clip (its trim handles show), or nothing.
 */
void
omarchy_timeline_set_selected (OmarchyTimeline *self, int track, int clip)
{
  gboolean t = track != self->selected_track, c = clip != self->selected_clip;
  if (!t && !c)
    return;
  self->selected_track = track;
  self->selected_clip = clip;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  if (t)
    g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_SELECTED_TRACK]);
  if (c)
    g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_SELECTED_CLIP]);
}

double
omarchy_timeline_get_zoom (OmarchyTimeline *self)
{
  return self->zoom;
}

double
omarchy_timeline_get_view_start (OmarchyTimeline *self)
{
  return self->view_start;
}

/**
 * omarchy_timeline_set_view:
 * @self: the timeline
 * @zoom: 1 for the whole duration, more to zoom in
 * @view_start: the time at the left edge
 *
 * Both are clamped: the zoom to 1 … #OmarchyTimeline:max-zoom, the view to the duration.
 */
void
omarchy_timeline_set_view (OmarchyTimeline *self, double zoom, double view_start)
{
  clamp_view (self, &zoom, &view_start);
  gboolean z = zoom != self->zoom, s = view_start != self->view_start;
  self->zoom = zoom;
  self->view_start = view_start;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  if (z)
    g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_ZOOM]);
  if (s)
    g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_VIEW_START]);
}

double
omarchy_timeline_get_max_zoom (OmarchyTimeline *self)
{
  return self->max_zoom;
}

void
omarchy_timeline_set_max_zoom (OmarchyTimeline *self, double max_zoom)
{
  self->max_zoom = MAX (1, max_zoom);
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_MAX_ZOOM]);
  omarchy_timeline_set_view (self, self->zoom, self->view_start);
}

/**
 * omarchy_timeline_get_tick_step:
 * @self: the timeline
 *
 * Returns: the seconds between labeled ticks at the current width and zoom (Shift+arrow's step)
 */
double
omarchy_timeline_get_tick_step (OmarchyTimeline *self)
{
  TimelineGeo g = tl_geo (self);
  return tick_step (self, &g);
}

OMARCHY_BOOL_ACCESSORS (OmarchyTimeline, timeline, cursor, l_props, L_PROP_CURSOR, FALSE)

/* ========================================================================== Scrubber ======= */

/**
 * OmarchyScrubber:
 *
 * A media transport scrub: the elapsed time, a position bar and the total time. The times are
 * `bodySmall` textDim, m:ss (h:mm:ss from an hour), `controlGap` from the bar. The bar has the
 * Slider's metrics: the unplayed track in `track`, the played part in the foreground, and the
 * Slider's knob, 1.15× when hot (the pointer, keyboard focus, a drag or
 * #OmarchyScrubber:cursor).
 *
 * Press or drag the bar to ::seek; Left/Right step 5 s (Shift: 30 s), Home/End jump to the
 * ends. Each moves #OmarchyScrubber:position first, then emits.
 */
struct _OmarchyScrubber {
  GtkWidget parent_instance;
  double position, duration;
  gboolean cursor, dragging, over_bar;
  char *surface;
  double press_x;
};

enum { S_PROP_0, S_PROP_POSITION, S_PROP_DURATION, S_PROP_CURSOR, S_N_PROPS };
static GParamSpec *s_props[S_N_PROPS];
enum { S_SEEK, S_N_SIGNALS };
static guint s_signals[S_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyScrubber, omarchy_scrubber, GTK_TYPE_WIDGET)

static char *
scrub_format (double seconds)
{
  long s = (long) MAX (0, floor (seconds));
  long h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
  return h > 0 ? g_strdup_printf ("%ld:%02ld:%02ld", h, m, sec) : g_strdup_printf ("%ld:%02ld", m, sec);
}

typedef struct {
  PangoLayout *elapsed, *total;
  int elapsed_w, total_w, line;
  float bar_x, bar_w, track_x, track_w;
  int track_h, knob;
} ScrubGeo;

static void
scrub_geo (OmarchyScrubber *self, ScrubGeo *g)
{
  const OmarchyTokens *t = omarchy_tokens ();
  GtkWidget *w = GTK_WIDGET (self);
  g_autofree char *a = scrub_format (self->position);
  g_autofree char *b = scrub_format (self->duration);
  g->elapsed = omarchy_layout (w, a, t->body_small, FALSE);
  g->total = omarchy_layout (w, b, t->body_small, FALSE);
  omarchy_layout_size (g->elapsed, &g->elapsed_w, NULL);
  omarchy_layout_size (g->total, &g->total_w, NULL);
  g->line = omarchy_line_height (t->body_small);
  float W = gtk_widget_get_width (w);
  /* Qt's Text is as wide as the text's fractional advance; the bar sits right after it. */
  PangoRectangle ea, ta;
  pango_layout_get_extents (g->elapsed, NULL, &ea);
  pango_layout_get_extents (g->total, NULL, &ta);
  g->bar_x = ea.width / (float) PANGO_SCALE + t->control_gap;
  g->bar_w = (W - ta.width / (float) PANGO_SCALE) - t->control_gap - g->bar_x;
  g->knob = omarchy_editing_knob ();
  g->track_h = MAX (4, (int) lround (t->control_height * 0.11));
  g->track_x = g->bar_x + g->knob / 2.0f;
  g->track_w = g->bar_w - g->knob;
}

static void
scrub_geo_clear (ScrubGeo *g)
{
  g_clear_object (&g->elapsed);
  g_clear_object (&g->total);
}

static double
scrub_progress (OmarchyScrubber *self)
{
  return self->duration > 0 ? CLAMP (self->position / self->duration, 0, 1) : 0;
}

static void
scrub_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  const OmarchyTokens *t = omarchy_tokens ();
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = omarchy_space (120);
    *nat = omarchy_space (360);
  } else {
    *min = *nat = MAX (omarchy_space (22), omarchy_editing_knob () + t->md);
  }
}

static void
scrub_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyScrubber *self = OMARCHY_SCRUBBER (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  ScrubGeo g;
  scrub_geo (self, &g);
  float ty = (H - g.line) / 2.0f;
  omarchy_draw_layout (s, g.elapsed, 0, ty, &p->text_dim);
  omarchy_draw_layout (s, g.total, W - g.total_w, ty, &p->text_dim);

  float cy = H / 2.0f, track_y = (H - g.track_h) / 2.0f;
  if (g.track_w > 0) {
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (g.track_x, track_y, g.track_w, g.track_h), g.track_h / 2.0f, &p->track);
    float played = (float) omarchy_editing_round (g.track_w * scrub_progress (self));
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (g.track_x, track_y, played, g.track_h), g.track_h / 2.0f, &p->foreground);
    gboolean hot = self->cursor || self->dragging || self->over_bar || omarchy_widget_focus_shown (w);
    omarchy_editing_draw_knob (s, g.track_x + played, cy, hot, &p->foreground, omarchy_editing_surface (p, self->surface));
  }
  scrub_geo_clear (&g);
}

static void
scrub_set_position (OmarchyScrubber *self, double position)
{
  if (position == self->position)
    return;
  self->position = position;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), s_props[S_PROP_POSITION]);
}

static void
scrub_seek (OmarchyScrubber *self, double time)
{
  scrub_set_position (self, time);
  g_signal_emit (self, s_signals[S_SEEK], 0, time);
}

static double
scrub_time_at (OmarchyScrubber *self, double x)
{
  ScrubGeo g;
  scrub_geo (self, &g);
  double out = g.track_w <= 0 ? 0 : CLAMP ((x - g.track_x) / g.track_w, 0, 1) * self->duration;
  scrub_geo_clear (&g);
  return out;
}

static gboolean
scrub_on_bar (OmarchyScrubber *self, double x)
{
  ScrubGeo g;
  scrub_geo (self, &g);
  gboolean on = x >= g.bar_x && x < g.bar_x + g.bar_w;
  scrub_geo_clear (&g);
  return on;
}

static void
scrub_drag_begin (GtkWidget *w, double x, double y)
{
  OmarchyScrubber *self = OMARCHY_SCRUBBER (w);
  if (!scrub_on_bar (self, x))
    return;
  if (gtk_widget_get_focusable (w))
    gtk_widget_grab_focus (w);
  self->dragging = TRUE;
  self->press_x = x;
  scrub_seek (self, scrub_time_at (self, x));
}

static void
scrub_drag_update (GtkWidget *w, double dx, double dy)
{
  OmarchyScrubber *self = OMARCHY_SCRUBBER (w);
  if (self->dragging)
    scrub_seek (self, scrub_time_at (self, self->press_x + dx));
}

static void
scrub_drag_end (GtkWidget *w, double dx, double dy)
{
  OmarchyScrubber *self = OMARCHY_SCRUBBER (w);
  self->dragging = FALSE;
  gtk_widget_queue_draw (w);
}

static void
scrub_motion (GtkEventControllerMotion *c, double x, double y, OmarchyScrubber *self)
{
  gboolean on = scrub_on_bar (self, x);
  if (on != self->over_bar) {
    self->over_bar = on;
    gtk_widget_set_cursor_from_name (GTK_WIDGET (self), on ? "pointer" : NULL);
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
scrub_leave (GtkEventControllerMotion *c, OmarchyScrubber *self)
{
  self->over_bar = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static gboolean
scrub_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyScrubber *self = OMARCHY_SCRUBBER (w);
  double step = (state & GDK_SHIFT_MASK) ? 30 : 5;
  if (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left)
    scrub_seek (self, MAX (0, self->position - step));
  else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right)
    scrub_seek (self, MIN (self->duration, self->position + step));
  else if (keyval == GDK_KEY_Home || keyval == GDK_KEY_KP_Home)
    scrub_seek (self, 0);
  else if (keyval == GDK_KEY_End || keyval == GDK_KEY_KP_End)
    scrub_seek (self, self->duration);
  else
    return FALSE;
  return TRUE;
}

static void
omarchy_scrubber_init (OmarchyScrubber *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->surface = g_strdup ("popups");
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_editing_add_drag (w, scrub_drag_begin, scrub_drag_update, scrub_drag_end);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "enter", G_CALLBACK (scrub_motion), self);
  g_signal_connect (m, "motion", G_CALLBACK (scrub_motion), self);
  g_signal_connect (m, "leave", G_CALLBACK (scrub_leave), self);
  gtk_widget_add_controller (w, m);
  omarchy_widget_add_keys (w, scrub_key);
}

static void
scrub_finalize (GObject *o)
{
  g_free (OMARCHY_SCRUBBER (o)->surface);
  G_OBJECT_CLASS (omarchy_scrubber_parent_class)->finalize (o);
}

static void
scrub_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyScrubber *self = OMARCHY_SCRUBBER (o);
  switch (id) {
  case S_PROP_POSITION: omarchy_scrubber_set_position (self, g_value_get_double (v)); break;
  case S_PROP_DURATION: omarchy_scrubber_set_duration (self, g_value_get_double (v)); break;
  case S_PROP_CURSOR: omarchy_scrubber_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
scrub_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyScrubber *self = OMARCHY_SCRUBBER (o);
  switch (id) {
  case S_PROP_POSITION: g_value_set_double (v, self->position); break;
  case S_PROP_DURATION: g_value_set_double (v, self->duration); break;
  case S_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_scrubber_class_init (OmarchyScrubberClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = scrub_finalize;
  oc->set_property = scrub_set_property;
  oc->get_property = scrub_get_property;
  wc->measure = scrub_measure;
  wc->snapshot = scrub_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyScrubber:position:
   *
   * The elapsed time, in seconds.
   */
  s_props[S_PROP_POSITION] = g_param_spec_double ("position", NULL, NULL, 0, G_MAXDOUBLE, 0, f);
  /**
   * OmarchyScrubber:duration:
   *
   * The total time, in seconds.
   */
  s_props[S_PROP_DURATION] = g_param_spec_double ("duration", NULL, NULL, 0, G_MAXDOUBLE, 0, f);
  /**
   * OmarchyScrubber:cursor:
   *
   * Draws the knob hot (Qt's `hasCursor`).
   */
  s_props[S_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, S_N_PROPS, s_props);
  /**
   * OmarchyScrubber::seek:
   * @self: the scrubber
   * @time: the new position, in seconds
   */
  s_signals[S_SEEK] = g_signal_new ("seek", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 1, G_TYPE_DOUBLE);
  gtk_widget_class_set_css_name (wc, "omarchy-scrubber");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_SLIDER);
}

/**
 * omarchy_scrubber_new:
 * @position: the elapsed time, in seconds
 * @duration: the total time
 *
 * Returns: (transfer floating): a scrubber
 */
GtkWidget *
omarchy_scrubber_new (double position, double duration)
{
  return g_object_new (OMARCHY_TYPE_SCRUBBER, "duration", duration, "position", position, NULL);
}

double
omarchy_scrubber_get_position (OmarchyScrubber *self)
{
  return self->position;
}

void
omarchy_scrubber_set_position (OmarchyScrubber *self, double position)
{
  scrub_set_position (self, MAX (0, position));
}

double
omarchy_scrubber_get_duration (OmarchyScrubber *self)
{
  return self->duration;
}

void
omarchy_scrubber_set_duration (OmarchyScrubber *self, double duration)
{
  duration = MAX (0, duration);
  if (duration == self->duration)
    return;
  self->duration = duration;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), s_props[S_PROP_DURATION]);
}

OMARCHY_BOOL_ACCESSORS (OmarchyScrubber, scrubber, cursor, s_props, S_PROP_CURSOR, FALSE)

gboolean
omarchy_scrubber_get_dragging (OmarchyScrubber *self)
{
  return self->dragging;
}

/**
 * omarchy_scrubber_set_surface:
 * @self: the scrubber
 * @surface: `popups` (the default), `background`, `tooltip` or `menu`
 *
 * The surface it sits on: the knob's ring is drawn in its background (Qt's `surfaceColor`).
 */
void
omarchy_scrubber_set_surface (OmarchyScrubber *self, const char *surface)
{
  g_free (self->surface);
  self->surface = g_strdup (surface ? surface : "popups");
  gtk_widget_queue_draw (GTK_WIDGET (self));
}
