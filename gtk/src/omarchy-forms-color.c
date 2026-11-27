/* omarchy-forms-color.c: ColorWell (ColorWell.qml) and SwatchStrip (SwatchStrip.qml). */
#include "omarchy-forms-private.h"

#include <math.h>

/* ========================================================================= ColorWell === */

/**
 * OmarchyColorWell:
 *
 * A button showing a color: the swatch inset `xxs` in a `space(44)` × `space(26)` well
 * (`normal` fill and border, `radius_small` corners). The cursor, the pointer and keyboard
 * focus draw a ring in the hover border `sm` outside the well, since a well under the hover
 * border would only get fainter. A click or Space/Return emits ::clicked (an app opens its
 * picker); pressing scales the well to 0.97.
 *
 * The swatch is a fixed color, or a palette role (#OmarchyColorWell:swatch-role) that follows
 * the theme.
 */
struct _OmarchyColorWell {
  GtkWidget parent_instance;
  GdkRGBA swatch;
  char *role;
  gboolean cursor;
  double scale, from;
  gint64 start;
  guint tick;
};

enum { W_PROP_0, W_PROP_SWATCH, W_PROP_SWATCH_ROLE, W_PROP_CURSOR, W_N_PROPS };
static GParamSpec *w_props[W_N_PROPS];
enum { W_CLICKED, W_HOVERED, W_N_SIGNALS };
static guint w_signals[W_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyColorWell, omarchy_color_well, GTK_TYPE_WIDGET)

static void
well_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  int ring = omarchy_tokens ()->sm;
  *min = *nat = (o == GTK_ORIENTATION_HORIZONTAL ? omarchy_space (44) : omarchy_space (26)) + ring * 2;
}

static void
well_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyColorWell *self = OMARCHY_COLOR_WELL (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  float rs = omarchy_radius_small ();
  gboolean hot = self->cursor || omarchy_widget_hovered (w) || omarchy_widget_focus_shown (w);
  if (hot) {
    float outer_r = rs > 0 ? MIN ((float) p->radius, rs + p->sm) : 0;
    omarchy_draw_border (s, &GRAPHENE_RECT_INIT (0, 0, W, H), outer_r, &p->hover.border);
  }
  float ww = omarchy_space (44), wh = omarchy_space (26);
  graphene_rect_t well = GRAPHENE_RECT_INIT (roundf ((W - ww) / 2), roundf ((H - wh) / 2), ww, wh);
  gtk_snapshot_save (s);
  if (self->scale != 1) {
    graphene_point_t c;
    graphene_rect_get_center (&well, &c);
    gtk_snapshot_translate (s, &c);
    gtk_snapshot_scale (s, (float) self->scale, (float) self->scale);
    gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (-c.x, -c.y));
  }
  omarchy_draw_surface (s, &well, rs, &p->normal.fill, &p->normal.border);
  float inset = p->normal.border.widths[0] + p->xxs;
  graphene_rect_t color = GRAPHENE_RECT_INIT (well.origin.x + inset, well.origin.y + inset, ww - 2 * inset, wh - 2 * inset);
  GdkRGBA swatch = self->role ? omarchy_role_color (self->role) : self->swatch;
  omarchy_draw_fill (s, &color, MAX (0.0f, rs - inset), &swatch);
  gtk_snapshot_restore (s);
}

static gboolean
well_tick (GtkWidget *w, GdkFrameClock *clock, gpointer data)
{
  OmarchyColorWell *self = OMARCHY_COLOR_WELL (w);
  double target = omarchy_widget_pressed (w) ? 0.97 : 1;
  double d = omarchy_tokens ()->motion_press * 1000.0;
  double x = d > 0 ? (gdk_frame_clock_get_frame_time (clock) - self->start) / d : 1;
  self->scale = x >= 1 ? target : self->from + (target - self->from) * omarchy_forms_out_cubic (x);
  gtk_widget_queue_draw (w);
  if (x >= 1) {
    self->tick = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

static void
well_state_changed (GtkWidget *w, GtkStateFlags old, gpointer data)
{
  OmarchyColorWell *self = OMARCHY_COLOR_WELL (w);
  if (!((gtk_widget_get_state_flags (w) ^ old) & GTK_STATE_FLAG_ACTIVE))
    return;
  if (omarchy_tokens ()->reduce_motion || !gtk_widget_get_mapped (w)) {
    self->scale = omarchy_widget_pressed (w) ? 0.97 : 1;
    gtk_widget_queue_draw (w);
    return;
  }
  self->from = self->scale;
  self->start = g_get_monotonic_time ();
  if (!self->tick)
    self->tick = gtk_widget_add_tick_callback (w, well_tick, NULL, NULL);
}

static void
well_clicked (GtkWidget *w, guint button, double x, double y)
{
  if (button == GDK_BUTTON_PRIMARY)
    g_signal_emit (w, w_signals[W_CLICKED], 0);
}

static gboolean
well_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  if (!omarchy_key_is_activate (keyval))
    return FALSE;
  g_signal_emit (w, w_signals[W_CLICKED], 0);
  return TRUE;
}

static void
omarchy_color_well_init (OmarchyColorWell *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->scale = 1;
  self->swatch = (GdkRGBA) { 0, 0, 0, 1 };
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, well_clicked);
  omarchy_widget_add_keys (w, well_key);
  omarchy_forms_add_hover_signal (w, w_signals[W_HOVERED]);
  g_signal_connect (w, "state-flags-changed", G_CALLBACK (well_state_changed), NULL);
}

static void
well_finalize (GObject *o)
{
  g_free (OMARCHY_COLOR_WELL (o)->role);
  G_OBJECT_CLASS (omarchy_color_well_parent_class)->finalize (o);
}

static void
well_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyColorWell *self = OMARCHY_COLOR_WELL (o);
  switch (id) {
  case W_PROP_SWATCH:
    if (g_value_get_boxed (v))
      omarchy_color_well_set_swatch (self, g_value_get_boxed (v));
    break;
  case W_PROP_SWATCH_ROLE: omarchy_color_well_set_swatch_role (self, g_value_get_string (v)); break;
  case W_PROP_CURSOR: omarchy_color_well_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
well_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyColorWell *self = OMARCHY_COLOR_WELL (o);
  switch (id) {
  case W_PROP_SWATCH: {
    GdkRGBA c;
    omarchy_color_well_get_swatch (self, &c);
    g_value_set_boxed (v, &c);
    break;
  }
  case W_PROP_SWATCH_ROLE: g_value_set_string (v, self->role); break;
  case W_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_color_well_class_init (OmarchyColorWellClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = well_finalize;
  oc->set_property = well_set_property;
  oc->get_property = well_get_property;
  wc->measure = well_measure;
  wc->snapshot = well_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyColorWell:swatch:
   *
   * The color shown (the role's color while #OmarchyColorWell:swatch-role is set).
   */
  w_props[W_PROP_SWATCH] = g_param_spec_boxed ("swatch", NULL, NULL, GDK_TYPE_RGBA, f);
  /**
   * OmarchyColorWell:swatch-role:
   *
   * A palette role (`accent`, `success`, `urgent`, …) whose color the well shows, following
   * the theme; %NULL for #OmarchyColorWell:swatch.
   */
  w_props[W_PROP_SWATCH_ROLE] = g_param_spec_string ("swatch-role", NULL, NULL, NULL, f);
  /**
   * OmarchyColorWell:cursor:
   *
   * Force the cursor ring, as a keyboard-driven panel's cursor does.
   */
  w_props[W_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, W_N_PROPS, w_props);
  /**
   * OmarchyColorWell::clicked:
   *
   * A click or Space/Return: open a picker.
   */
  w_signals[W_CLICKED] = g_signal_new ("clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 0);
  /**
   * OmarchyColorWell::hovered:
   * @self: the colorwell
   * @hovered: the pointer is over it
   */
  w_signals[W_HOVERED] = g_signal_new ("hovered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  gtk_widget_class_set_css_name (wc, "omarchy-color-well");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_BUTTON);
}

/**
 * omarchy_color_well_new:
 * @swatch: (nullable): the color
 *
 * Returns: (transfer floating): a color well
 */
GtkWidget *
omarchy_color_well_new (const GdkRGBA *swatch)
{
  OmarchyColorWell *self = g_object_new (OMARCHY_TYPE_COLOR_WELL, NULL);
  if (swatch)
    omarchy_color_well_set_swatch (self, swatch);
  return GTK_WIDGET (self);
}

/**
 * omarchy_color_well_new_for_role:
 * @role: a palette role (`accent`, `success`, `urgent`, …)
 *
 * Returns: (transfer floating): a color well showing the role's color, following the theme
 */
GtkWidget *
omarchy_color_well_new_for_role (const char *role)
{
  return g_object_new (OMARCHY_TYPE_COLOR_WELL, "swatch-role", role, NULL);
}

/**
 * omarchy_color_well_get_swatch:
 * @self: the well
 * @swatch: (out caller-allocates): the color shown
 */
void
omarchy_color_well_get_swatch (OmarchyColorWell *self, GdkRGBA *swatch)
{
  *swatch = self->role ? omarchy_role_color (self->role) : self->swatch;
}

void
omarchy_color_well_set_swatch (OmarchyColorWell *self, const GdkRGBA *swatch)
{
  if (!self->role && gdk_rgba_equal (&self->swatch, swatch))
    return;
  self->swatch = *swatch;
  g_clear_pointer (&self->role, g_free);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), w_props[W_PROP_SWATCH]);
}

const char *
omarchy_color_well_get_swatch_role (OmarchyColorWell *self)
{
  return self->role;
}

/**
 * omarchy_color_well_set_swatch_role:
 * @self: the well
 * @role: (nullable): a palette role, or %NULL for the fixed swatch
 */
void
omarchy_color_well_set_swatch_role (OmarchyColorWell *self, const char *role)
{
  if (!g_strcmp0 (self->role, role))
    return;
  g_free (self->role);
  self->role = g_strdup (role);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), w_props[W_PROP_SWATCH_ROLE]);
  g_object_notify_by_pspec (G_OBJECT (self), w_props[W_PROP_SWATCH]);
}

OMARCHY_BOOL_ACCESSORS (OmarchyColorWell, color_well, cursor, w_props, W_PROP_CURSOR, FALSE)

/* ======================================================================== SwatchStrip === */

/**
 * OmarchySwatchStrip:
 *
 * A row of preset colors, one chosen: `space(22)` chips (`radius_small`, a 1 px `track` rim
 * so a background-colored chip shows) `xxs` apart. The chosen chip wears an accent ring
 * `max(1, space(2))` wide, `xxs` outside it; the keyboard cursor and the pointer wear the hover
 * border's color in the same place (chosen wins). Left/Right (h/l) walk the cursor,
 * Space/Return pick; a click picks. Picking sets #OmarchySwatchStrip:current-index and emits
 * ::picked.
 */
typedef struct {
  GdkRGBA color;
  char *role;
} Swatch;

struct _OmarchySwatchStrip {
  GtkWidget parent_instance;
  GArray *swatches;
  int current, cursor, hover;
};

enum { S_PROP_0, S_PROP_CURRENT_INDEX, S_PROP_CURSOR_INDEX, S_N_PROPS };
static GParamSpec *s_props[S_N_PROPS];
enum { S_PICKED, S_N_SIGNALS };
static guint s_signals[S_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchySwatchStrip, omarchy_swatch_strip, GTK_TYPE_WIDGET)

static int
strip_ring_w (void)
{
  return MAX (1, omarchy_space (2));
}

static int
strip_cell (void)
{
  return omarchy_space (22) + (omarchy_tokens ()->xxs + strip_ring_w ()) * 2;
}

static float
strip_cell_x (OmarchySwatchStrip *self, int i)
{
  int cell = strip_cell (), gap = omarchy_tokens ()->xxs;
  float x = i * (cell + gap);
  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    x = gtk_widget_get_width (GTK_WIDGET (self)) - x - cell;
  return x;
}

static int
strip_index_at (OmarchySwatchStrip *self, double x, double y)
{
  int cell = strip_cell ();
  for (guint i = 0; i < self->swatches->len; i++) {
    float cx = strip_cell_x (self, (int) i);
    if (x >= cx && x < cx + cell && y >= 0 && y < cell)
      return (int) i;
  }
  return -1;
}

static void
strip_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchySwatchStrip *self = OMARCHY_SWATCH_STRIP (w);
  int n = (int) self->swatches->len, cell = strip_cell ();
  if (o == GTK_ORIENTATION_HORIZONTAL)
    *min = *nat = n * cell + MAX (0, n - 1) * omarchy_tokens ()->xxs;
  else
    *min = *nat = cell;
}

static void
strip_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchySwatchStrip *self = OMARCHY_SWATCH_STRIP (w);
  const OmarchyTokens *p = omarchy_paint ();
  int cell = strip_cell (), chip = omarchy_space (22), ring = strip_ring_w ();
  float rs = omarchy_radius_small ();
  for (guint i = 0; i < self->swatches->len; i++) {
    const Swatch *sw = &g_array_index (self->swatches, Swatch, i);
    float x = strip_cell_x (self, (int) i);
    gboolean chosen = (int) i == self->current;
    gboolean hot = (int) i == self->cursor || (int) i == self->hover;
    if (chosen || hot) {
      OmarchyBorder b = omarchy_border_flat (chosen ? &p->accent : &p->hover.border.color, ring);
      omarchy_draw_border (s, &GRAPHENE_RECT_INIT (x, 0, cell, cell), rs > 0 ? rs + p->xxs + ring : 0, &b);
    }
    GdkRGBA c = sw->role ? omarchy_role_color (sw->role) : sw->color;
    OmarchyBorder rim = omarchy_border_flat (&p->track, 1);
    float off = roundf ((cell - chip) / 2.0f);
    omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (x + off, off, chip, chip), rs, &c, &rim);
  }
}

static void
strip_pick (OmarchySwatchStrip *self, int index)
{
  if (index < 0 || index >= (int) self->swatches->len)
    return;
  omarchy_swatch_strip_set_current_index (self, index);
  g_signal_emit (self, s_signals[S_PICKED], 0, index);
}

static void
strip_clicked (GtkWidget *w, guint button, double x, double y)
{
  if (button == GDK_BUTTON_PRIMARY)
    strip_pick (OMARCHY_SWATCH_STRIP (w), strip_index_at (OMARCHY_SWATCH_STRIP (w), x, y));
}

static gboolean
strip_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchySwatchStrip *self = OMARCHY_SWATCH_STRIP (w);
  int n = (int) self->swatches->len;
  int from = self->cursor >= 0 ? self->cursor : MAX (0, self->current);
  guint key = omarchy_reading_key (w, keyval, TRUE);
  if (key == GDK_KEY_Right || key == GDK_KEY_KP_Right) {
    omarchy_swatch_strip_set_cursor_index (self, MIN (n - 1, from + 1));
    return TRUE;
  }
  if (key == GDK_KEY_Left || key == GDK_KEY_KP_Left) {
    omarchy_swatch_strip_set_cursor_index (self, MAX (0, from - 1));
    return TRUE;
  }
  if (omarchy_key_is_activate (keyval)) {
    strip_pick (self, from);
    return TRUE;
  }
  return FALSE;
}

static void
strip_motion (GtkEventControllerMotion *c, double x, double y, OmarchySwatchStrip *self)
{
  int i = strip_index_at (self, x, y);
  if (i != self->hover) {
    self->hover = i;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
strip_leave (GtkEventControllerMotion *c, OmarchySwatchStrip *self)
{
  self->hover = -1;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
swatch_clear (gpointer data)
{
  g_free (((Swatch *) data)->role);
}

static void
omarchy_swatch_strip_init (OmarchySwatchStrip *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->swatches = g_array_new (FALSE, TRUE, sizeof (Swatch));
  g_array_set_clear_func (self->swatches, swatch_clear);
  self->current = self->cursor = self->hover = -1;
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, strip_clicked);
  omarchy_widget_add_keys (w, strip_key);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "motion", G_CALLBACK (strip_motion), self);
  g_signal_connect (m, "leave", G_CALLBACK (strip_leave), self);
  gtk_widget_add_controller (w, m);
}

static void
strip_finalize (GObject *o)
{
  g_array_unref (OMARCHY_SWATCH_STRIP (o)->swatches);
  G_OBJECT_CLASS (omarchy_swatch_strip_parent_class)->finalize (o);
}

static void
strip_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchySwatchStrip *self = OMARCHY_SWATCH_STRIP (o);
  switch (id) {
  case S_PROP_CURRENT_INDEX: omarchy_swatch_strip_set_current_index (self, g_value_get_int (v)); break;
  case S_PROP_CURSOR_INDEX: omarchy_swatch_strip_set_cursor_index (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
strip_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchySwatchStrip *self = OMARCHY_SWATCH_STRIP (o);
  switch (id) {
  case S_PROP_CURRENT_INDEX: g_value_set_int (v, self->current); break;
  case S_PROP_CURSOR_INDEX: g_value_set_int (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_swatch_strip_class_init (OmarchySwatchStripClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = strip_finalize;
  oc->set_property = strip_set_property;
  oc->get_property = strip_get_property;
  wc->measure = strip_measure;
  wc->snapshot = strip_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchySwatchStrip:current-index:
   *
   * The chosen color, or -1.
   */
  s_props[S_PROP_CURRENT_INDEX] = g_param_spec_int ("current-index", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchySwatchStrip:cursor-index:
   *
   * The keyboard cursor, or -1 for none.
   */
  s_props[S_PROP_CURSOR_INDEX] = g_param_spec_int ("cursor-index", NULL, NULL, -1, G_MAXINT, -1, f);
  g_object_class_install_properties (oc, S_N_PROPS, s_props);
  /**
   * OmarchySwatchStrip::picked:
   * @self: the swatchstrip
   * @index: the chosen color
   *
   * A click or Space/Return picked a color.
   */
  s_signals[S_PICKED] = g_signal_new ("picked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                      G_TYPE_NONE, 1, G_TYPE_INT);
  gtk_widget_class_set_css_name (wc, "omarchy-swatch-strip");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_RADIO_GROUP);
}

/**
 * omarchy_swatch_strip_new:
 *
 * Returns: (transfer floating): an empty strip; add colors with
 *   [method@Omarchy.SwatchStrip.append] or [method@Omarchy.SwatchStrip.append_role]
 */
GtkWidget *
omarchy_swatch_strip_new (void)
{
  return g_object_new (OMARCHY_TYPE_SWATCH_STRIP, NULL);
}

void
omarchy_swatch_strip_append (OmarchySwatchStrip *self, const GdkRGBA *color)
{
  Swatch sw = { *color, NULL };
  g_array_append_val (self->swatches, sw);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

/**
 * omarchy_swatch_strip_append_role:
 * @self: the strip
 * @role: a palette role (`accent`, `urgent`, `dim`, …) whose color follows the theme
 */
void
omarchy_swatch_strip_append_role (OmarchySwatchStrip *self, const char *role)
{
  Swatch sw = { { 0 }, g_strdup (role) };
  g_array_append_val (self->swatches, sw);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
omarchy_swatch_strip_remove_all (OmarchySwatchStrip *self)
{
  g_array_set_size (self->swatches, 0);
  self->current = self->cursor = self->hover = -1;
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

guint
omarchy_swatch_strip_get_n_colors (OmarchySwatchStrip *self)
{
  return self->swatches->len;
}

/**
 * omarchy_swatch_strip_get_color:
 * @self: the strip
 * @index: which
 * @color: (out caller-allocates): its color now
 */
void
omarchy_swatch_strip_get_color (OmarchySwatchStrip *self, guint index, GdkRGBA *color)
{
  g_return_if_fail (index < self->swatches->len);
  const Swatch *sw = &g_array_index (self->swatches, Swatch, index);
  *color = sw->role ? omarchy_role_color (sw->role) : sw->color;
}

int
omarchy_swatch_strip_get_current_index (OmarchySwatchStrip *self)
{
  return self->current;
}

void
omarchy_swatch_strip_set_current_index (OmarchySwatchStrip *self, int index)
{
  index = MAX (-1, index);
  if (self->current == index)
    return;
  self->current = index;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), s_props[S_PROP_CURRENT_INDEX]);
}

int
omarchy_swatch_strip_get_cursor_index (OmarchySwatchStrip *self)
{
  return self->cursor;
}

void
omarchy_swatch_strip_set_cursor_index (OmarchySwatchStrip *self, int index)
{
  index = MAX (-1, index);
  if (self->cursor == index)
    return;
  self->cursor = index;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), s_props[S_PROP_CURSOR_INDEX]);
}
