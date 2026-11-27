/* omarchy-slider.c: Slider, drawn as Qt's PanelSlider (ValueSlider.qml): a thin track in the
 * selected fill, a foreground fill, a foreground knob ringed in the surface color, optional
 * notches cut into the track. */
#include "omarchy-private.h"

#include <math.h>
#include <string.h>

/**
 * OmarchySlider:
 *
 * A value on a thin track. Drag or click to set it, the wheel and Left/Right (h/l) step it,
 * Home/End jump. ::moved while it changes, ::released when a drag (or a step) ends; both carry
 * the value, and #OmarchySlider:value follows. #OmarchySlider:tick-count cuts that many notches.
 */
struct _OmarchySlider {
  GtkWidget parent_instance;
  double value, minimum, maximum, step;
  gboolean integer, dragging;
  int ticks;
  char *surface;
  double shown, from;
  gint64 start;
  guint tick;
  double knob_scale;
};

enum { PROP_0, PROP_VALUE, PROP_MINIMUM, PROP_MAXIMUM, PROP_STEP, PROP_INTEGER, PROP_TICK_COUNT, N_PROPS };
static GParamSpec *props[N_PROPS];
enum { MOVED, RELEASED, RIGHT_CLICKED, N_SIGNALS };
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchySlider, omarchy_slider, GTK_TYPE_WIDGET)

typedef struct {
  int track_h, knob;
  float track_x, track_w, cy;
} SliderGeo;

static SliderGeo
geo (OmarchySlider *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  SliderGeo g;
  g.track_h = MAX (4, (int) lround (t->control_height * 0.11));
  g.knob = MAX (14, (int) lround (t->control_height * 0.38));
  g.track_x = g.knob / 2.0f;
  g.track_w = MAX (0, gtk_widget_get_width (GTK_WIDGET (self)) - g.knob);
  g.cy = gtk_widget_get_height (GTK_WIDGET (self)) / 2.0f;
  return g;
}

static const GdkRGBA *
surface_bg (OmarchySlider *self, const OmarchyTokens *p)
{
  if (!g_strcmp0 (self->surface, "background"))
    return &p->background;
  if (!g_strcmp0 (self->surface, "tooltip"))
    return &p->tooltip.background;
  if (!g_strcmp0 (self->surface, "menu"))
    return &p->menu.background;
  return &p->popups.background;
}

static double
progress (OmarchySlider *self, double v)
{
  double range = MAX (0.0001, self->maximum - self->minimum);
  return CLAMP ((v - self->minimum) / range, 0, 1);
}

static void
measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  const OmarchyTokens *t = omarchy_tokens ();
  int knob = MAX (14, (int) lround (t->control_height * 0.38));
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = knob * 2;
    *nat = omarchy_space (200);
  } else {
    *min = *nat = MAX (omarchy_space (22), knob + t->md);
  }
}

static void
snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchySlider *self = OMARCHY_SLIDER (w);
  const OmarchyTokens *p = omarchy_paint ();
  SliderGeo g = geo (self);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  double frac = progress (self, self->shown);
  float ty = g.cy - g.track_h / 2.0f;
  graphene_rect_t track = GRAPHENE_RECT_INIT (g.track_x, ty, g.track_w, g.track_h);
  omarchy_draw_fill (s, &track, g.track_h / 2.0f, &p->selected.fill);
  float fill_w = (float) (g.track_w * frac);
  graphene_rect_t fill = GRAPHENE_RECT_INIT (rtl ? g.track_x + g.track_w - fill_w : g.track_x, ty, fill_w, g.track_h);
  omarchy_draw_fill (s, &fill, g.track_h / 2.0f, &p->foreground);
  const GdkRGBA *bg = surface_bg (self, p);
  if (self->ticks > 2) {
    float tw = MAX (1, omarchy_space (2)), th = g.track_h + omarchy_space (4);
    for (int i = 1; i < self->ticks - 1; i++) {
      double f = (double) i / (self->ticks - 1);
      float x = (float) (g.track_x + g.track_w * (rtl ? 1 - f : f) - tw / 2);
      omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x, g.cy - th / 2, tw, th), 1, bg);
    }
  }
  float kx = (float) (g.track_x + g.track_w * (rtl ? 1 - frac : frac));
  gboolean hot = self->dragging || omarchy_widget_hovered (w) || omarchy_widget_focus_shown (w);
  float k = g.knob * (hot && !p->reduce_motion ? 1.15f : 1.0f);
  OmarchyBorder ring = omarchy_border_flat (bg, MAX (1, omarchy_space (2)));
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (kx - k / 2, g.cy - k / 2, k, k), k / 2, &p->foreground, &ring);
}

static gboolean
animate_tick (GtkWidget *w, GdkFrameClock *clock, gpointer data)
{
  OmarchySlider *self = OMARCHY_SLIDER (w);
  double x = (gdk_frame_clock_get_frame_time (clock) - self->start) / 140000.0;
  double e = 1 - pow (1 - CLAMP (x, 0, 1), 3);
  self->shown = x >= 1 ? self->value : self->from + (self->value - self->from) * e;
  gtk_widget_queue_draw (w);
  if (x >= 1) {
    self->tick = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

static void
show_value (OmarchySlider *self, gboolean animate)
{
  GtkWidget *w = GTK_WIDGET (self);
  if (!animate || omarchy_tokens ()->reduce_motion || !gtk_widget_get_mapped (w)) {
    if (self->tick) {
      gtk_widget_remove_tick_callback (w, self->tick);
      self->tick = 0;
    }
    self->shown = self->value;
    gtk_widget_queue_draw (w);
    return;
  }
  self->from = self->shown;
  self->start = g_get_monotonic_time ();
  if (!self->tick)
    self->tick = gtk_widget_add_tick_callback (w, animate_tick, NULL, NULL);
}

static double
snap (OmarchySlider *self, double v)
{
  if (self->integer)
    v = round (v);
  return CLAMP (v, self->minimum, self->maximum);
}

static double
value_at (OmarchySlider *self, double x)
{
  SliderGeo g = geo (self);
  if (g.track_w <= 0)
    return self->minimum;
  double f = CLAMP ((x - g.track_x) / g.track_w, 0, 1);
  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    f = 1 - f;
  return snap (self, self->minimum + f * (self->maximum - self->minimum));
}

static void
move_to (OmarchySlider *self, double v, gboolean animate)
{
  v = snap (self, v);
  if (v != self->value) {
    self->value = v;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_VALUE]);
  }
  show_value (self, animate);
  g_signal_emit (self, signals[MOVED], 0, self->value);
}

static void
drag_begin (GtkGestureDrag *g, double x, double y, OmarchySlider *self)
{
  if (gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (g)) == GDK_BUTTON_SECONDARY) {
    g_signal_emit (self, signals[RIGHT_CLICKED], 0);
    gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_DENIED);
    return;
  }
  self->dragging = TRUE;
  if (gtk_widget_get_focusable (GTK_WIDGET (self)))
    gtk_widget_grab_focus (GTK_WIDGET (self));
  move_to (self, value_at (self, x), FALSE);
}

static void
drag_update (GtkGestureDrag *g, double dx, double dy, OmarchySlider *self)
{
  double x0, y0;
  gtk_gesture_drag_get_start_point (g, &x0, &y0);
  if (self->dragging)
    move_to (self, value_at (self, x0 + dx), FALSE);
}

static void
drag_end (GtkGestureDrag *g, double dx, double dy, OmarchySlider *self)
{
  if (!self->dragging)
    return;
  self->dragging = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_signal_emit (self, signals[RELEASED], 0, self->value);
}

static gboolean
scroll (GtkEventControllerScroll *c, double dx, double dy, OmarchySlider *self)
{
  double delta = dy < 0 || dx > 0 ? self->step : -self->step;
  move_to (self, self->value + delta, TRUE);
  g_signal_emit (self, signals[RELEASED], 0, self->value);
  return TRUE;
}

static gboolean
key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchySlider *self = OMARCHY_SLIDER (w);
  guint k = omarchy_reading_key (w, keyval, TRUE);
  double v;
  if (k == GDK_KEY_Right || k == GDK_KEY_Up)
    v = self->value + self->step;
  else if (k == GDK_KEY_Left || k == GDK_KEY_Down)
    v = self->value - self->step;
  else if (k == GDK_KEY_Home)
    v = self->minimum;
  else if (k == GDK_KEY_End)
    v = self->maximum;
  else
    return FALSE;
  move_to (self, v, TRUE);
  g_signal_emit (self, signals[RELEASED], 0, self->value);
  return TRUE;
}

static void
omarchy_slider_init (OmarchySlider *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->maximum = 1;
  self->step = 0.05;
  self->surface = g_strdup ("popups");
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  gtk_widget_set_cursor_from_name (w, "pointer");
  omarchy_widget_follow (w);
  GtkGesture *drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), 0);
  g_signal_connect (drag, "drag-begin", G_CALLBACK (drag_begin), self);
  g_signal_connect (drag, "drag-update", G_CALLBACK (drag_update), self);
  g_signal_connect (drag, "drag-end", G_CALLBACK (drag_end), self);
  gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (drag));
  GtkEventController *sc = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
  g_signal_connect (sc, "scroll", G_CALLBACK (scroll), self);
  gtk_widget_add_controller (w, sc);
  omarchy_widget_add_keys (w, key);
}

static void
finalize (GObject *o)
{
  g_free (OMARCHY_SLIDER (o)->surface);
  G_OBJECT_CLASS (omarchy_slider_parent_class)->finalize (o);
}

static void
set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchySlider *self = OMARCHY_SLIDER (o);
  switch (id) {
  case PROP_VALUE: omarchy_slider_set_value (self, g_value_get_double (v)); break;
  case PROP_MINIMUM: omarchy_slider_set_range (self, g_value_get_double (v), self->maximum); break;
  case PROP_MAXIMUM: omarchy_slider_set_range (self, self->minimum, g_value_get_double (v)); break;
  case PROP_STEP: omarchy_slider_set_step (self, g_value_get_double (v)); break;
  case PROP_INTEGER: omarchy_slider_set_integer (self, g_value_get_boolean (v)); break;
  case PROP_TICK_COUNT: omarchy_slider_set_tick_count (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchySlider *self = OMARCHY_SLIDER (o);
  switch (id) {
  case PROP_VALUE: g_value_set_double (v, self->value); break;
  case PROP_MINIMUM: g_value_set_double (v, self->minimum); break;
  case PROP_MAXIMUM: g_value_set_double (v, self->maximum); break;
  case PROP_STEP: g_value_set_double (v, self->step); break;
  case PROP_INTEGER: g_value_set_boolean (v, self->integer); break;
  case PROP_TICK_COUNT: g_value_set_int (v, self->ticks); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_slider_class_init (OmarchySliderClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = finalize;
  oc->set_property = set_property;
  oc->get_property = get_property;
  wc->measure = measure;
  wc->snapshot = snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  props[PROP_VALUE] = g_param_spec_double ("value", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, f);
  props[PROP_MINIMUM] = g_param_spec_double ("minimum", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, f);
  props[PROP_MAXIMUM] = g_param_spec_double ("maximum", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 1, f);
  props[PROP_STEP] = g_param_spec_double ("step", NULL, NULL, 0, G_MAXDOUBLE, 0.05, f);
  props[PROP_INTEGER] = g_param_spec_boolean ("integer", NULL, NULL, FALSE, f);
  /**
   * OmarchySlider:tick-count:
   *
   * When above 1, that many evenly spaced notches cut into the track (in the surface color).
   * Purely visual: snap with #OmarchySlider:step and #OmarchySlider:integer.
   */
  props[PROP_TICK_COUNT] = g_param_spec_int ("tick-count", NULL, NULL, 0, G_MAXINT, 0, f);
  g_object_class_install_properties (oc, N_PROPS, props);
  signals[MOVED] = g_signal_new ("moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                 G_TYPE_NONE, 1, G_TYPE_DOUBLE);
  signals[RELEASED] = g_signal_new ("released", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 1, G_TYPE_DOUBLE);
  signals[RIGHT_CLICKED] = g_signal_new ("right-clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                         NULL, NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-slider");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_SLIDER);
}

/**
 * omarchy_slider_new:
 * @value: the value, between 0 and 1 until [method@Omarchy.Slider.set_range]
 *
 * Returns: (transfer floating): a slider
 */
GtkWidget *
omarchy_slider_new (double value)
{
  OmarchySlider *self = g_object_new (OMARCHY_TYPE_SLIDER, NULL);
  omarchy_slider_set_value (self, value);
  return GTK_WIDGET (self);
}

double
omarchy_slider_get_value (OmarchySlider *self)
{
  return self->value;
}

void
omarchy_slider_set_value (OmarchySlider *self, double value)
{
  value = CLAMP (value, self->minimum, self->maximum);
  if (value == self->value)
    return;
  self->value = value;
  if (!self->dragging)
    show_value (self, TRUE);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_VALUE_NOW, value, -1);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_VALUE]);
}

void
omarchy_slider_set_range (OmarchySlider *self, double minimum, double maximum)
{
  self->minimum = MIN (minimum, maximum);
  self->maximum = MAX (minimum, maximum);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, self->minimum,
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_MAX, self->maximum, -1);
  self->value = CLAMP (self->value, self->minimum, self->maximum);
  show_value (self, FALSE);
}

double
omarchy_slider_get_minimum (OmarchySlider *self)
{
  return self->minimum;
}

double
omarchy_slider_get_maximum (OmarchySlider *self)
{
  return self->maximum;
}

void
omarchy_slider_set_step (OmarchySlider *self, double step)
{
  self->step = step;
}

void
omarchy_slider_set_integer (OmarchySlider *self, gboolean integer)
{
  self->integer = !!integer;
}

int
omarchy_slider_get_tick_count (OmarchySlider *self)
{
  return self->ticks;
}

void
omarchy_slider_set_tick_count (OmarchySlider *self, int ticks)
{
  self->ticks = MAX (0, ticks);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TICK_COUNT]);
}

gboolean
omarchy_slider_get_dragging (OmarchySlider *self)
{
  return self->dragging;
}

/**
 * omarchy_slider_set_surface:
 * @self: the slider
 * @surface: `popups` (the default), `background`, `tooltip` or `menu`
 *
 * The surface it sits on: the knob's ring and the notches are drawn in its background.
 */
void
omarchy_slider_set_surface (OmarchySlider *self, const char *surface)
{
  g_free (self->surface);
  self->surface = g_strdup (surface ? surface : "popups");
  gtk_widget_queue_draw (GTK_WIDGET (self));
}
