/* omarchy-toggle.c: ToggleSwitch (ToggleSwitch.qml) and Toggle (Toggle.qml).
 *
 * Both flip #checked themselves on a click or Space/Return and then emit ::toggled (GTK's way;
 * Qt's caller flips it). Read the new state in the handler or bind notify::checked. */
#include "omarchy-private.h"

#include <math.h>

typedef struct {
  int track_h, track_w, knob, inset, pad;
} SwitchMetrics;

static SwitchMetrics
switch_metrics (gboolean ring)
{
  const OmarchyTokens *t = omarchy_tokens ();
  SwitchMetrics m;
  m.track_h = MAX (22, (int) lround (t->control_height * 0.55));
  m.track_w = (int) lround (m.track_h * 1.9);
  m.knob = MAX (6, (int) lround (m.track_h * 0.72));
  m.inset = MAX (1, (int) lround ((m.track_h - m.knob) / 2.0));
  m.pad = ring ? omarchy_space (6) : 0;
  return m;
}

/* The track at (x, y), the knob `pos` of the way from off to on (0..1). */
static void
draw_switch (GtkSnapshot *s, float x, float y, const SwitchMetrics *m, gboolean checked, double pos, gboolean rtl)
{
  const OmarchyTokens *p = omarchy_paint ();
  gboolean rounded = p->radius > 0;
  graphene_rect_t track = GRAPHENE_RECT_INIT (x, y, m->track_w, m->track_h);
  omarchy_draw_surface (s, &track, rounded ? m->track_h / 2.0f : 0, checked ? &p->selected.fill : &p->normal.fill,
                        checked ? &p->selected.border : &p->normal.border);
  double off_x = m->inset, on_x = m->track_w - m->knob - m->inset;
  if (rtl) {
    double tmp = off_x;
    off_x = on_x;
    on_x = tmp;
  }
  float kx = (float) (x + off_x + (on_x - off_x) * pos);
  float ky = y + roundf ((m->track_h - m->knob) / 2.0f);
  GdkRGBA knob = checked ? p->selected.text : p->text_dim;
  omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (kx, ky, m->knob, m->knob), rounded ? m->knob / 2.0f : 0, &knob);
}

static double
out_cubic (double t)
{
  t = CLAMP (t, 0, 1) - 1;
  return t * t * t + 1;
}

/* ========================================================================== ToggleSwitch === */

/**
 * OmarchyToggleSwitch:
 *
 * A bare switch: a track and a sliding knob, sharp unless Hyprland rounds (a pill when it
 * does). The keyboard cursor and the pointer draw a ring `space(6)` outside the track.
 * #OmarchyToggleSwitch:interactive off makes it presentation only, for a row that owns the
 * click.
 */
struct _OmarchyToggleSwitch {
  GtkWidget parent_instance;
  gboolean checked, cursor, interactive, busy;
  double pos, from;
  gint64 start;
  guint tick;
};

enum { S_PROP_0, S_PROP_CHECKED, S_PROP_CURSOR, S_PROP_INTERACTIVE, S_PROP_BUSY, S_N_PROPS };
static GParamSpec *s_props[S_N_PROPS];
enum { S_TOGGLED, S_N_SIGNALS };
static guint s_signals[S_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyToggleSwitch, omarchy_toggle_switch, GTK_TYPE_WIDGET)

static gboolean
switch_tick (GtkWidget *w, GdkFrameClock *clock, gpointer data)
{
  OmarchyToggleSwitch *self = OMARCHY_TOGGLE_SWITCH (w);
  double target = self->checked ? 1 : 0;
  double d = omarchy_tokens ()->motion_fast * 1000.0;
  double x = d > 0 ? (gdk_frame_clock_get_frame_time (clock) - self->start) / d : 1;
  self->pos = x >= 1 ? target : self->from + (target - self->from) * out_cubic (x);
  gtk_widget_queue_draw (w);
  if (x >= 1) {
    self->tick = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

static void
switch_animate (OmarchyToggleSwitch *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  double target = self->checked ? 1 : 0;
  if (omarchy_tokens ()->reduce_motion || !gtk_widget_get_mapped (w)) {
    self->pos = target;
    gtk_widget_queue_draw (w);
    return;
  }
  self->from = self->pos;
  self->start = g_get_monotonic_time ();
  if (!self->tick)
    self->tick = gtk_widget_add_tick_callback (w, switch_tick, NULL, NULL);
}

static void
switch_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyToggleSwitch *self = OMARCHY_TOGGLE_SWITCH (w);
  SwitchMetrics m = switch_metrics (self->interactive);
  *min = *nat = o == GTK_ORIENTATION_HORIZONTAL ? m.track_w + m.pad * 2 : m.track_h + m.pad * 2;
}

static void
switch_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyToggleSwitch *self = OMARCHY_TOGGLE_SWITCH (w);
  const OmarchyTokens *p = omarchy_paint ();
  SwitchMetrics m = switch_metrics (self->interactive);
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gboolean hot = self->cursor || omarchy_widget_hovered (w) || omarchy_widget_focus_shown (w);
  if (self->interactive && hot)
    omarchy_draw_border (s, &GRAPHENE_RECT_INIT (0, 0, W, H), p->radius, &p->hover.border);
  draw_switch (s, roundf ((W - m.track_w) / 2), roundf ((H - m.track_h) / 2), &m, self->checked, self->pos,
               gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL);
}

static void
switch_flip (OmarchyToggleSwitch *self)
{
  if (self->busy || !self->interactive)
    return;
  omarchy_toggle_switch_set_checked (self, !self->checked);
  g_signal_emit (self, s_signals[S_TOGGLED], 0);
}

static void
switch_clicked (GtkWidget *w, guint button, double x, double y)
{
  if (button == GDK_BUTTON_PRIMARY)
    switch_flip (OMARCHY_TOGGLE_SWITCH (w));
}

static gboolean
switch_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  if (!omarchy_key_is_activate (keyval))
    return FALSE;
  switch_flip (OMARCHY_TOGGLE_SWITCH (w));
  return TRUE;
}

static void
omarchy_toggle_switch_init (OmarchyToggleSwitch *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->interactive = TRUE;
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, switch_clicked);
  omarchy_widget_add_keys (w, switch_key);
}

static void
switch_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyToggleSwitch *self = OMARCHY_TOGGLE_SWITCH (o);
  switch (id) {
  case S_PROP_CHECKED: omarchy_toggle_switch_set_checked (self, g_value_get_boolean (v)); break;
  case S_PROP_CURSOR: omarchy_toggle_switch_set_cursor (self, g_value_get_boolean (v)); break;
  case S_PROP_INTERACTIVE: omarchy_toggle_switch_set_interactive (self, g_value_get_boolean (v)); break;
  case S_PROP_BUSY: omarchy_toggle_switch_set_busy (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
switch_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyToggleSwitch *self = OMARCHY_TOGGLE_SWITCH (o);
  switch (id) {
  case S_PROP_CHECKED: g_value_set_boolean (v, self->checked); break;
  case S_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case S_PROP_INTERACTIVE: g_value_set_boolean (v, self->interactive); break;
  case S_PROP_BUSY: g_value_set_boolean (v, self->busy); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_toggle_switch_class_init (OmarchyToggleSwitchClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->set_property = switch_set_property;
  oc->get_property = switch_get_property;
  wc->measure = switch_measure;
  wc->snapshot = switch_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  s_props[S_PROP_CHECKED] = g_param_spec_boolean ("checked", NULL, NULL, FALSE, f);
  s_props[S_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyToggleSwitch:interactive:
   *
   * Takes clicks and keys and draws the cursor ring. Off when a surrounding row owns them.
   */
  s_props[S_PROP_INTERACTIVE] = g_param_spec_boolean ("interactive", NULL, NULL, TRUE, f);
  /**
   * OmarchyToggleSwitch:busy:
   *
   * An operation is in flight: clicks are swallowed, hover and cursor stay.
   */
  s_props[S_PROP_BUSY] = g_param_spec_boolean ("busy", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, S_N_PROPS, s_props);
  /**
   * OmarchyToggleSwitch::toggled:
   *
   * After a click or Space/Return flipped #OmarchyToggleSwitch:checked.
   */
  s_signals[S_TOGGLED] = g_signal_new ("toggled", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-toggle-switch");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_SWITCH);
}

/**
 * omarchy_toggle_switch_new:
 *
 * Returns: (transfer floating): a switch, off
 */
GtkWidget *
omarchy_toggle_switch_new (void)
{
  return g_object_new (OMARCHY_TYPE_TOGGLE_SWITCH, NULL);
}

gboolean
omarchy_toggle_switch_get_checked (OmarchyToggleSwitch *self)
{
  return self->checked;
}

void
omarchy_toggle_switch_set_checked (OmarchyToggleSwitch *self, gboolean checked)
{
  if (self->checked == !!checked)
    return;
  self->checked = !!checked;
  gtk_accessible_update_state (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_STATE_CHECKED, self->checked, -1);
  switch_animate (self);
  g_object_notify_by_pspec (G_OBJECT (self), s_props[S_PROP_CHECKED]);
}

OMARCHY_BOOL_ACCESSORS (OmarchyToggleSwitch, toggle_switch, cursor, s_props, S_PROP_CURSOR, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyToggleSwitch, toggle_switch, interactive, s_props, S_PROP_INTERACTIVE, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyToggleSwitch, toggle_switch, busy, s_props, S_PROP_BUSY, FALSE)

/* ================================================================================ Toggle === */

/**
 * OmarchyToggle:
 *
 * A labeled row: a bold title and an optional description, the switch at the end. The whole
 * row is the control: a click anywhere, or Space/Return while focused, flips it. At least 54
 * tall, `rowPaddingX` inside, focus and the cursor drawn as hover.
 */
struct _OmarchyToggle {
  GtkWidget parent_instance;
  char *label, *description;
  gboolean checked, cursor;
  double pos, from;
  gint64 start;
  guint tick;
};

enum { T_PROP_0, T_PROP_LABEL, T_PROP_DESCRIPTION, T_PROP_CHECKED, T_PROP_CURSOR, T_N_PROPS };
static GParamSpec *t_props[T_N_PROPS];
enum { T_TOGGLED, T_N_SIGNALS };
static guint t_signals[T_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyToggle, omarchy_toggle, GTK_TYPE_WIDGET)

static const OmarchyBorder *
toggle_border (OmarchyToggle *self, const OmarchyTokens *p)
{
  GtkWidget *w = GTK_WIDGET (self);
  if (gtk_widget_has_focus (w) && omarchy_widget_focus_shown (w))
    return &p->focus.border;
  if (self->cursor || omarchy_widget_hovered (w))
    return &p->hover.border;
  return &p->normal.border;
}

static int
toggle_text_width (OmarchyToggle *self, int width)
{
  const OmarchyTokens *t = omarchy_tokens ();
  const OmarchyBorder *b = &t->normal.border;
  SwitchMetrics m = switch_metrics (FALSE);
  return MAX (1, width - (int) (b->widths[1] + b->widths[3]) - t->row_padding_x * 3 - m.track_w);
}

static int
toggle_text_height (OmarchyToggle *self, int text_w)
{
  const OmarchyTokens *t = omarchy_tokens ();
  int h = omarchy_line_height (t->subtitle);
  if (self->description && *self->description) {
    g_autoptr (PangoLayout) l = omarchy_layout (GTK_WIDGET (self), self->description, t->caption, FALSE);
    if (text_w < 100000)
      pango_layout_set_width (l, text_w * PANGO_SCALE);
    pango_layout_set_wrap (l, PANGO_WRAP_WORD_CHAR);
    int lines = MAX (1, pango_layout_get_line_count (l));
    int dh = lines == 1 ? omarchy_line_height (t->caption) : 0;
    if (lines > 1) {
      PangoRectangle r;
      pango_layout_get_extents (l, NULL, &r);
      dh = (int) ceil (r.height / (double) PANGO_SCALE);
    }
    h += t->xs + dh;
  }
  return h;
}

static void
toggle_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyToggle *self = OMARCHY_TOGGLE (w);
  const OmarchyTokens *t = omarchy_tokens ();
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    SwitchMetrics m = switch_metrics (FALSE);
    *min = m.track_w + t->row_padding_x * 3 + 2;
    *nat = MAX (*min, omarchy_space (240));
    return;
  }
  int width = for_size >= 0 ? for_size : G_MAXINT / 4;
  SwitchMetrics m = switch_metrics (FALSE);
  int content = MAX (toggle_text_height (self, toggle_text_width (self, width)), m.track_h);
  *min = *nat = MAX (54, content + t->huge);
}

static GtkSizeRequestMode
toggle_request_mode (GtkWidget *w)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
toggle_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyToggle *self = OMARCHY_TOGGLE (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gboolean focus = gtk_widget_has_focus (w) && omarchy_widget_focus_shown (w);
  gboolean hot = self->cursor || omarchy_widget_hovered (w);
  const GdkRGBA *fill = focus ? &p->focus.fill : hot ? &p->hover.fill : &p->normal.fill;
  const OmarchyBorder *b = toggle_border (self, p);
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), p->radius, fill, b);

  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  SwitchMetrics m = switch_metrics (FALSE);
  int text_w = toggle_text_width (self, (int) W);
  int text_h = toggle_text_height (self, text_w);
  int content_h = MAX (text_h, m.track_h);
  float top = roundf ((H - content_h) / 2);
  float left = b->widths[3] + p->row_padding_x, right = W - b->widths[1] - p->row_padding_x;

  float ty = top + roundf ((content_h - text_h) / 2.0f);
  g_autoptr (PangoLayout) title = omarchy_layout (w, self->label, p->subtitle, TRUE);
  pango_layout_set_width (title, text_w * PANGO_SCALE);
  pango_layout_set_ellipsize (title, PANGO_ELLIPSIZE_END);
  if (rtl)
    pango_layout_set_alignment (title, PANGO_ALIGN_RIGHT);
  float tx = rtl ? right - text_w : left;
  omarchy_draw_layout (s, title, tx, ty, &p->foreground);
  if (self->description && *self->description) {
    g_autoptr (PangoLayout) d = omarchy_layout (w, self->description, p->caption, FALSE);
    pango_layout_set_width (d, text_w * PANGO_SCALE);
    pango_layout_set_wrap (d, PANGO_WRAP_WORD_CHAR);
    if (rtl)
      pango_layout_set_alignment (d, PANGO_ALIGN_RIGHT);
    omarchy_draw_layout (s, d, tx, ty + omarchy_line_height (p->subtitle) + p->xs, &p->text_dim);
  }
  float sx = rtl ? left : right - m.track_w;
  draw_switch (s, sx, top + roundf ((content_h - m.track_h) / 2.0f), &m, self->checked, self->pos, rtl);
}

static gboolean
toggle_tick (GtkWidget *w, GdkFrameClock *clock, gpointer data)
{
  OmarchyToggle *self = OMARCHY_TOGGLE (w);
  double target = self->checked ? 1 : 0;
  double d = omarchy_tokens ()->motion_fast * 1000.0;
  double x = d > 0 ? (gdk_frame_clock_get_frame_time (clock) - self->start) / d : 1;
  self->pos = x >= 1 ? target : self->from + (target - self->from) * out_cubic (x);
  gtk_widget_queue_draw (w);
  if (x >= 1) {
    self->tick = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

static void
toggle_flip (OmarchyToggle *self)
{
  omarchy_toggle_set_checked (self, !self->checked);
  g_signal_emit (self, t_signals[T_TOGGLED], 0);
}

static void
toggle_clicked (GtkWidget *w, guint button, double x, double y)
{
  if (button == GDK_BUTTON_PRIMARY)
    toggle_flip (OMARCHY_TOGGLE (w));
}

static gboolean
toggle_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  if (!omarchy_key_is_activate (keyval))
    return FALSE;
  toggle_flip (OMARCHY_TOGGLE (w));
  return TRUE;
}

static void
omarchy_toggle_init (OmarchyToggle *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->label = g_strdup ("");
  self->description = g_strdup ("");
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, toggle_clicked);
  omarchy_widget_add_keys (w, toggle_key);
}

static void
toggle_finalize (GObject *o)
{
  OmarchyToggle *self = OMARCHY_TOGGLE (o);
  g_free (self->label);
  g_free (self->description);
  G_OBJECT_CLASS (omarchy_toggle_parent_class)->finalize (o);
}

static void
toggle_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyToggle *self = OMARCHY_TOGGLE (o);
  switch (id) {
  case T_PROP_LABEL: omarchy_toggle_set_label (self, g_value_get_string (v)); break;
  case T_PROP_DESCRIPTION: omarchy_toggle_set_description (self, g_value_get_string (v)); break;
  case T_PROP_CHECKED: omarchy_toggle_set_checked (self, g_value_get_boolean (v)); break;
  case T_PROP_CURSOR: omarchy_toggle_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
toggle_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyToggle *self = OMARCHY_TOGGLE (o);
  switch (id) {
  case T_PROP_LABEL: g_value_set_string (v, self->label); break;
  case T_PROP_DESCRIPTION: g_value_set_string (v, self->description); break;
  case T_PROP_CHECKED: g_value_set_boolean (v, self->checked); break;
  case T_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_toggle_class_init (OmarchyToggleClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = toggle_finalize;
  oc->set_property = toggle_set_property;
  oc->get_property = toggle_get_property;
  wc->measure = toggle_measure;
  wc->snapshot = toggle_snapshot;
  wc->get_request_mode = toggle_request_mode;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  t_props[T_PROP_LABEL] = g_param_spec_string ("label", NULL, NULL, "", f);
  t_props[T_PROP_DESCRIPTION] = g_param_spec_string ("description", NULL, NULL, "", f);
  t_props[T_PROP_CHECKED] = g_param_spec_boolean ("checked", NULL, NULL, FALSE, f);
  t_props[T_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, T_N_PROPS, t_props);
  /**
   * OmarchyToggle::toggled:
   *
   * After a click or Space/Return flipped #OmarchyToggle:checked.
   */
  t_signals[T_TOGGLED] = g_signal_new ("toggled", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-toggle");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_SWITCH);
}

/**
 * omarchy_toggle_new:
 * @label: (nullable): the title
 * @description: (nullable): the line below it
 *
 * Returns: (transfer floating): a toggle row, off
 */
GtkWidget *
omarchy_toggle_new (const char *label, const char *description)
{
  return g_object_new (OMARCHY_TYPE_TOGGLE, "label", label ? label : "", "description", description ? description : "",
                       NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyToggle, toggle, label, t_props, T_PROP_LABEL, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyToggle, toggle, description, t_props, T_PROP_DESCRIPTION, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyToggle, toggle, cursor, t_props, T_PROP_CURSOR, FALSE)

gboolean
omarchy_toggle_get_checked (OmarchyToggle *self)
{
  return self->checked;
}

void
omarchy_toggle_set_checked (OmarchyToggle *self, gboolean checked)
{
  if (self->checked == !!checked)
    return;
  self->checked = !!checked;
  gtk_accessible_update_state (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_STATE_CHECKED, self->checked, -1);
  GtkWidget *w = GTK_WIDGET (self);
  double target = self->checked ? 1 : 0;
  if (omarchy_tokens ()->reduce_motion || !gtk_widget_get_mapped (w)) {
    self->pos = target;
    gtk_widget_queue_draw (w);
  } else {
    self->from = self->pos;
    self->start = g_get_monotonic_time ();
    if (!self->tick)
      self->tick = gtk_widget_add_tick_callback (w, toggle_tick, NULL, NULL);
  }
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_CHECKED]);
}
