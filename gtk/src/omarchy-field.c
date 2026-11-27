/* omarchy-field.c: TextField (TextField.qml), NumberField (NumberField.qml) and Dropdown
 * (Dropdown.qml), drawn as the Qt kit draws them. Text editing is GTK's own GtkText inside. */
#include "omarchy-private.h"

#include <math.h>
#include <stdlib.h>

/* The field chrome every input shares: focus > hover-cursor > normal. */
static void
field_look (GtkWidget *w, gboolean cursor, const GdkRGBA **fill, const OmarchyBorder **border)
{
  const OmarchyTokens *p = omarchy_paint ();
  GtkStateFlags f = gtk_widget_get_state_flags (w);
  gboolean focused = (f & GTK_STATE_FLAG_FOCUS_WITHIN) != 0;
  gboolean hot = cursor || (f & GTK_STATE_FLAG_PRELIGHT);
  *fill = focused ? &p->focus.fill : hot ? &p->hover.fill : &p->normal.fill;
  *border = focused ? &p->focus.border : hot ? &p->hover.border : &p->normal.border;
}

/* ============================================================================ Popups ====== */

GtkPopover *
omarchy_popup_new (GtkWidget *anchor, GtkWidget *content)
{
  GtkWidget *pop = gtk_popover_new ();
  gtk_widget_add_css_class (pop, "omarchy-popup");
  gtk_popover_set_has_arrow (GTK_POPOVER (pop), FALSE);
  gtk_popover_set_position (GTK_POPOVER (pop), GTK_POS_BOTTOM);
  gtk_popover_set_autohide (GTK_POPOVER (pop), TRUE);
  gtk_popover_set_child (GTK_POPOVER (pop), content);
  gtk_widget_set_parent (pop, anchor);
  return GTK_POPOVER (pop);
}

void
omarchy_popup_show_below (GtkPopover *pop, GtkWidget *anchor, const graphene_rect_t *trigger, int gap)
{
  GdkRectangle r = { (int) trigger->origin.x, (int) trigger->origin.y, (int) trigger->size.width, (int) trigger->size.height };
  gtk_popover_set_pointing_to (pop, &r);
  gtk_popover_set_offset (pop, 0, gap);
  gtk_popover_popup (pop);
}

/* ========================================================================= TextField ====== */

/**
 * OmarchyTextField:
 *
 * A single-line input: the normal border at rest, hover and focus sharing the hover-cursor look,
 * `controlPaddingX` × `inputPaddingY`, `body` text, `textFaint` placeholder. It is a
 * #GtkEditable (text, position, ::changed through the delegate GtkText).
 */
struct _OmarchyTextField {
  GtkWidget parent_instance;
  GtkWidget *text;
  gboolean cursor;
};

enum { F_PROP_0, F_PROP_PLACEHOLDER, F_PROP_CURSOR, F_PROP_PASSWORD, F_N_PROPS };
static GParamSpec *f_props[F_N_PROPS];
enum { F_ACTIVATE, F_N_SIGNALS };
static guint f_signals[F_N_SIGNALS];

static GtkEditable *
text_field_delegate (GtkEditable *e)
{
  return GTK_EDITABLE (OMARCHY_TEXT_FIELD (e)->text);
}

static void
text_field_editable_init (GtkEditableInterface *iface)
{
  iface->get_delegate = text_field_delegate;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (OmarchyTextField, omarchy_text_field, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_EDITABLE, text_field_editable_init))

static void
text_field_insets (float in[4])
{
  const OmarchyTokens *t = omarchy_tokens ();
  const OmarchyBorder *specs[] = { &t->normal.border, &t->hover.border, &t->focus.border };
  float r[4];
  omarchy_border_reserve (r, specs, 3);
  in[0] = r[0] + t->input_padding_y;
  in[2] = r[2] + t->input_padding_y;
  in[1] = r[1] + t->control_padding_x;
  in[3] = r[3] + t->control_padding_x;
}

static void
text_field_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyTextField *self = OMARCHY_TEXT_FIELD (w);
  float in[4];
  text_field_insets (in);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    int cmin, cnat;
    gtk_widget_measure (self->text, o, -1, &cmin, &cnat, NULL, NULL);
    *min = (int) (in[1] + in[3]) + MIN (cmin, omarchy_space (40));
    *nat = (int) (in[1] + in[3]) + MAX (cnat, omarchy_space (180));
  } else {
    *min = *nat = (int) (in[0] + in[2]) + omarchy_line_height (omarchy_tokens ()->body);
  }
}

static void
text_field_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyTextField *self = OMARCHY_TEXT_FIELD (w);
  float in[4];
  text_field_insets (in);
  int ch;
  gtk_widget_measure (self->text, GTK_ORIENTATION_VERTICAL, -1, NULL, &ch, NULL, NULL);
  int inner = height - (int) (in[0] + in[2]);
  GtkAllocation a = { (int) in[3], (int) in[0] + (inner - ch) / 2, MAX (0, width - (int) (in[1] + in[3])), ch };
  gtk_widget_size_allocate (self->text, &a, -1);
}

static void
text_field_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyTextField *self = OMARCHY_TEXT_FIELD (w);
  const GdkRGBA *fill;
  const OmarchyBorder *border;
  field_look (w, self->cursor, &fill, &border);
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, gtk_widget_get_width (w), gtk_widget_get_height (w)),
                        omarchy_paint ()->radius, fill, border);
  gtk_widget_snapshot_child (w, self->text, s);
}

static gboolean
text_field_grab_focus (GtkWidget *w)
{
  return gtk_widget_grab_focus (OMARCHY_TEXT_FIELD (w)->text);
}

static void
text_field_activate (GtkText *text, OmarchyTextField *self)
{
  g_signal_emit (self, f_signals[F_ACTIVATE], 0);
}

static void
text_field_pressed (GtkGestureClick *g, int n, double x, double y, OmarchyTextField *self)
{
  gtk_widget_grab_focus (self->text);
}

static void
omarchy_text_field_init (OmarchyTextField *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->text = gtk_text_new ();
  gtk_widget_set_parent (self->text, w);
  gtk_editable_init_delegate (GTK_EDITABLE (self));
  g_signal_connect (self->text, "activate", G_CALLBACK (text_field_activate), self);
  GtkGesture *g = gtk_gesture_click_new ();
  g_signal_connect (g, "pressed", G_CALLBACK (text_field_pressed), self);
  gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (g));
  gtk_widget_set_cursor_from_name (w, "text");
  omarchy_widget_follow (w);
}

static void
text_field_dispose (GObject *o)
{
  OmarchyTextField *self = OMARCHY_TEXT_FIELD (o);
  if (self->text)
    gtk_editable_finish_delegate (GTK_EDITABLE (self));
  g_clear_pointer (&self->text, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_text_field_parent_class)->dispose (o);
}

static void
text_field_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyTextField *self = OMARCHY_TEXT_FIELD (o);
  if (gtk_editable_delegate_set_property (o, id, v, pspec))
    return;
  switch (id) {
  case F_PROP_PLACEHOLDER: omarchy_text_field_set_placeholder_text (self, g_value_get_string (v)); break;
  case F_PROP_CURSOR: omarchy_text_field_set_cursor (self, g_value_get_boolean (v)); break;
  case F_PROP_PASSWORD: omarchy_text_field_set_password (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
text_field_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyTextField *self = OMARCHY_TEXT_FIELD (o);
  if (gtk_editable_delegate_get_property (o, id, v, pspec))
    return;
  switch (id) {
  case F_PROP_PLACEHOLDER: g_value_set_string (v, omarchy_text_field_get_placeholder_text (self)); break;
  case F_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case F_PROP_PASSWORD: g_value_set_boolean (v, omarchy_text_field_get_password (self)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_text_field_class_init (OmarchyTextFieldClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = text_field_dispose;
  oc->set_property = text_field_set_property;
  oc->get_property = text_field_get_property;
  wc->measure = text_field_measure;
  wc->size_allocate = text_field_allocate;
  wc->snapshot = text_field_snapshot;
  wc->grab_focus = text_field_grab_focus;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  f_props[F_PROP_PLACEHOLDER] = g_param_spec_string ("placeholder-text", NULL, NULL, "", f);
  /**
   * OmarchyTextField:cursor:
   *
   * Force the hover-cursor look, as a keyboard-driven panel's cursor does.
   */
  f_props[F_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  f_props[F_PROP_PASSWORD] = g_param_spec_boolean ("password", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, F_N_PROPS, f_props);
  gtk_editable_install_properties (oc, F_N_PROPS);
  /**
   * OmarchyTextField::activate:
   *
   * Return in the field.
   */
  f_signals[F_ACTIVATE] = g_signal_new ("activate", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                        G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-text-field");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_TEXT_BOX);
}

/**
 * omarchy_text_field_new:
 *
 * Returns: (transfer floating): an empty text field
 */
GtkWidget *
omarchy_text_field_new (void)
{
  return g_object_new (OMARCHY_TYPE_TEXT_FIELD, NULL);
}

const char *
omarchy_text_field_get_placeholder_text (OmarchyTextField *self)
{
  const char *p = gtk_text_get_placeholder_text (GTK_TEXT (self->text));
  return p ? p : "";
}

void
omarchy_text_field_set_placeholder_text (OmarchyTextField *self, const char *text)
{
  gtk_text_set_placeholder_text (GTK_TEXT (self->text), text);
  g_object_notify_by_pspec (G_OBJECT (self), f_props[F_PROP_PLACEHOLDER]);
}

OMARCHY_BOOL_ACCESSORS (OmarchyTextField, text_field, cursor, f_props, F_PROP_CURSOR, FALSE)

gboolean
omarchy_text_field_get_password (OmarchyTextField *self)
{
  return !gtk_text_get_visibility (GTK_TEXT (self->text));
}

void
omarchy_text_field_set_password (OmarchyTextField *self, gboolean password)
{
  gtk_text_set_visibility (GTK_TEXT (self->text), !password);
  gtk_text_set_input_purpose (GTK_TEXT (self->text), password ? GTK_INPUT_PURPOSE_PASSWORD : GTK_INPUT_PURPOSE_FREE_FORM);
  g_object_notify_by_pspec (G_OBJECT (self), f_props[F_PROP_PASSWORD]);
}

/**
 * omarchy_text_field_get_text_widget:
 * @self: the field
 *
 * Returns: (transfer none): the GtkText inside, for what the editable API doesn't cover
 */
GtkText *
omarchy_text_field_get_text_widget (OmarchyTextField *self)
{
  return GTK_TEXT (self->text);
}

/* ======================================================================== NumberField ====== */

/**
 * OmarchyNumberField:
 *
 * An integer stepper with a label: `[−] value [+]`, `numberFieldWidth` × `controlHeight`, the
 * label `bodySmall` in `textDim` `md` above. Up/Down step it, the step buttons click, typing a
 * number and Return sets it. ::modified when the user changed it.
 */
struct _OmarchyNumberField {
  GtkWidget parent_instance;
  char *label;
  int value, from, to, step;
  gboolean cursor;
  GtkWidget *text;
  int hover_part;   /* -1 down, +1 up, 0 none */
  int pressed_part;
};

enum { N_PROP_0, N_PROP_LABEL, N_PROP_VALUE, N_PROP_FROM, N_PROP_TO, N_PROP_STEP, N_PROP_CURSOR, N_N_PROPS };
static GParamSpec *n_props[N_N_PROPS];
enum { N_MODIFIED, N_N_SIGNALS };
static guint n_signals[N_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyNumberField, omarchy_number_field, GTK_TYPE_WIDGET)

static int
number_label_h (OmarchyNumberField *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return self->label && *self->label ? omarchy_line_height (t->body_small) + t->md : 0;
}

static int
number_spin_h (void)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return MAX (t->control_height, t->body + t->control_padding_y * 2);
}

static void
number_spin_rect (OmarchyNumberField *self, graphene_rect_t *r)
{
  *r = GRAPHENE_RECT_INIT (0, number_label_h (self), omarchy_tokens ()->number_field_width, number_spin_h ());
}

/* The step button squares: inset by the border, as tall as the field inside it. */
static void
number_part_rect (OmarchyNumberField *self, int part, graphene_rect_t *out)
{
  graphene_rect_t spin;
  number_spin_rect (self, &spin);
  float inset = omarchy_tokens ()->normal.border.widths[0];
  float size = spin.size.height - inset * 2;
  gboolean rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;
  gboolean right = (part > 0) != rtl;
  *out = GRAPHENE_RECT_INIT (right ? spin.size.width - size - inset : inset, spin.origin.y + inset, size, size);
}

static int
number_part_at (OmarchyNumberField *self, double x, double y)
{
  graphene_rect_t r;
  for (int part = -1; part <= 1; part += 2) {
    number_part_rect (self, part, &r);
    if (graphene_rect_contains_point (&r, &GRAPHENE_POINT_INIT (x, y)))
      return part;
  }
  return 0;
}

static void
number_sync_text (OmarchyNumberField *self)
{
  char buf[32];
  g_snprintf (buf, sizeof buf, "%d", self->value);
  if (g_strcmp0 (gtk_editable_get_text (GTK_EDITABLE (self->text)), buf))
    gtk_editable_set_text (GTK_EDITABLE (self->text), buf);
}

static void
number_modify (OmarchyNumberField *self, int value)
{
  value = CLAMP (value, self->from, self->to);
  if (value == self->value) {
    number_sync_text (self);
    return;
  }
  omarchy_number_field_set_value (self, value);
  g_signal_emit (self, n_signals[N_MODIFIED], 0, self->value);
}

static void
number_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyNumberField *self = OMARCHY_NUMBER_FIELD (w);
  const OmarchyTokens *t = omarchy_tokens ();
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    int lw = 0;
    if (self->label && *self->label) {
      g_autoptr (PangoLayout) l = omarchy_layout (w, self->label, t->body_small, FALSE);
      omarchy_layout_size (l, &lw, NULL);
    }
    *min = *nat = MAX (t->number_field_width, lw);
  } else {
    *min = *nat = number_label_h (self) + number_spin_h ();
  }
}

static void
number_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyNumberField *self = OMARCHY_NUMBER_FIELD (w);
  graphene_rect_t spin, down, up;
  number_spin_rect (self, &spin);
  number_part_rect (self, -1, &down);
  number_part_rect (self, 1, &up);
  int ch;
  gtk_widget_measure (self->text, GTK_ORIENTATION_VERTICAL, -1, NULL, &ch, NULL, NULL);
  float left = MIN (down.origin.x, up.origin.x) + down.size.width;
  float right = MAX (down.origin.x, up.origin.x);
  GtkAllocation a = { (int) left, (int) (spin.origin.y + (spin.size.height - ch) / 2), MAX (0, (int) (right - left)), ch };
  gtk_widget_size_allocate (self->text, &a, -1);
}

static void
number_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyNumberField *self = OMARCHY_NUMBER_FIELD (w);
  const OmarchyTokens *p = omarchy_paint ();
  if (self->label && *self->label) {
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->label, p->body_small, FALSE);
    int lw;
    omarchy_layout_size (l, &lw, NULL);
    gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
    omarchy_draw_layout (s, l, rtl ? gtk_widget_get_width (w) - lw : 0, 0, &p->text_dim);
  }
  graphene_rect_t spin;
  number_spin_rect (self, &spin);
  const GdkRGBA *fill;
  const OmarchyBorder *border;
  field_look (w, self->cursor, &fill, &border);
  omarchy_draw_surface (s, &spin, p->radius, fill, border);
  for (int part = -1; part <= 1; part += 2) {
    graphene_rect_t r;
    number_part_rect (self, part, &r);
    gboolean enabled = part < 0 ? self->value > self->from : self->value < self->to;
    if (enabled && self->pressed_part == part)
      omarchy_draw_fill (s, &r, MIN (4, p->radius), &p->pressed);
    else if (enabled && self->hover_part == part)
      omarchy_draw_fill (s, &r, MIN (4, p->radius), &p->hover.fill);
    g_autoptr (PangoLayout) g = omarchy_layout (w, part < 0 ? "\xe2\x88\x92" : "+", p->body, FALSE);
    int gw;
    omarchy_layout_size (g, &gw, NULL);
    int gh = omarchy_line_height (p->body);
    GdkRGBA ink = p->foreground;
    if (!enabled)
      ink.alpha *= 0.4f;
    omarchy_draw_layout (s, g, r.origin.x + roundf ((r.size.width - gw) / 2), r.origin.y + roundf ((r.size.height - gh) / 2), &ink);
  }
  gtk_widget_snapshot_child (w, self->text, s);
}

static void
number_motion (GtkEventControllerMotion *c, double x, double y, OmarchyNumberField *self)
{
  int part = number_part_at (self, x, y);
  if (part != self->hover_part) {
    self->hover_part = part;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
number_leave (GtkEventControllerMotion *c, OmarchyNumberField *self)
{
  self->hover_part = 0;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
number_pressed (GtkGestureClick *g, int n, double x, double y, OmarchyNumberField *self)
{
  self->pressed_part = number_part_at (self, x, y);
  if (self->pressed_part)
    gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_CLAIMED);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
number_released (GtkGestureClick *g, int n, double x, double y, OmarchyNumberField *self)
{
  int part = self->pressed_part;
  self->pressed_part = 0;
  if (part && number_part_at (self, x, y) == part)
    number_modify (self, self->value + part * self->step);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static gboolean
number_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyNumberField *self = OMARCHY_NUMBER_FIELD (w);
  if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up) {
    number_modify (self, self->value + self->step);
    return TRUE;
  }
  if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down) {
    number_modify (self, self->value - self->step);
    return TRUE;
  }
  return FALSE;
}

static void
number_commit (OmarchyNumberField *self)
{
  const char *text = gtk_editable_get_text (GTK_EDITABLE (self->text));
  char *end;
  long v = strtol (text, &end, 10);
  if (end != text)
    number_modify (self, (int) CLAMP (v, G_MININT, G_MAXINT));
  else
    number_sync_text (self);
}

static void
number_focus_out (GtkEventControllerFocus *c, OmarchyNumberField *self)
{
  number_commit (self);
}

static gboolean
number_grab_focus (GtkWidget *w)
{
  return gtk_widget_grab_focus (OMARCHY_NUMBER_FIELD (w)->text);
}

static void
omarchy_number_field_init (OmarchyNumberField *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->label = g_strdup ("");
  self->to = 100;
  self->step = 1;
  self->text = gtk_text_new ();
  gtk_editable_set_alignment (GTK_EDITABLE (self->text), 0.5f);
  gtk_text_set_input_purpose (GTK_TEXT (self->text), GTK_INPUT_PURPOSE_NUMBER);
  gtk_editable_set_width_chars (GTK_EDITABLE (self->text), 1);
  gtk_widget_set_parent (self->text, w);
  number_sync_text (self);
  g_signal_connect_swapped (self->text, "activate", G_CALLBACK (number_commit), self);
  GtkEventController *focus = gtk_event_controller_focus_new ();
  g_signal_connect (focus, "leave", G_CALLBACK (number_focus_out), self);
  gtk_widget_add_controller (self->text, focus);
  GtkEventController *motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "motion", G_CALLBACK (number_motion), self);
  g_signal_connect (motion, "leave", G_CALLBACK (number_leave), self);
  gtk_widget_add_controller (w, motion);
  GtkGesture *click = gtk_gesture_click_new ();
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click), GTK_PHASE_CAPTURE);
  g_signal_connect (click, "pressed", G_CALLBACK (number_pressed), self);
  g_signal_connect (click, "released", G_CALLBACK (number_released), self);
  gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (click));
  GtkEventController *keys = omarchy_widget_add_keys (w, number_key);
  gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
  omarchy_widget_follow (w);
}

static void
number_dispose (GObject *o)
{
  OmarchyNumberField *self = OMARCHY_NUMBER_FIELD (o);
  g_clear_pointer (&self->text, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_number_field_parent_class)->dispose (o);
}

static void
number_finalize (GObject *o)
{
  g_free (OMARCHY_NUMBER_FIELD (o)->label);
  G_OBJECT_CLASS (omarchy_number_field_parent_class)->finalize (o);
}

static void
number_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyNumberField *self = OMARCHY_NUMBER_FIELD (o);
  switch (id) {
  case N_PROP_LABEL: omarchy_number_field_set_label (self, g_value_get_string (v)); break;
  case N_PROP_VALUE: omarchy_number_field_set_value (self, g_value_get_int (v)); break;
  case N_PROP_FROM: omarchy_number_field_set_range (self, g_value_get_int (v), MAX (self->to, g_value_get_int (v))); break;
  case N_PROP_TO: omarchy_number_field_set_range (self, MIN (self->from, g_value_get_int (v)), g_value_get_int (v)); break;
  case N_PROP_STEP: omarchy_number_field_set_step (self, g_value_get_int (v)); break;
  case N_PROP_CURSOR: omarchy_number_field_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
number_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyNumberField *self = OMARCHY_NUMBER_FIELD (o);
  switch (id) {
  case N_PROP_LABEL: g_value_set_string (v, self->label); break;
  case N_PROP_VALUE: g_value_set_int (v, self->value); break;
  case N_PROP_FROM: g_value_set_int (v, self->from); break;
  case N_PROP_TO: g_value_set_int (v, self->to); break;
  case N_PROP_STEP: g_value_set_int (v, self->step); break;
  case N_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_number_field_class_init (OmarchyNumberFieldClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = number_dispose;
  oc->finalize = number_finalize;
  oc->set_property = number_set_property;
  oc->get_property = number_get_property;
  wc->measure = number_measure;
  wc->size_allocate = number_allocate;
  wc->snapshot = number_snapshot;
  wc->grab_focus = number_grab_focus;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  n_props[N_PROP_LABEL] = g_param_spec_string ("label", NULL, NULL, "", f);
  n_props[N_PROP_VALUE] = g_param_spec_int ("value", NULL, NULL, G_MININT, G_MAXINT, 0, f);
  n_props[N_PROP_FROM] = g_param_spec_int ("from", NULL, NULL, G_MININT, G_MAXINT, 0, f);
  n_props[N_PROP_TO] = g_param_spec_int ("to", NULL, NULL, G_MININT, G_MAXINT, 100, f);
  n_props[N_PROP_STEP] = g_param_spec_int ("step", NULL, NULL, 1, G_MAXINT, 1, f);
  n_props[N_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, N_N_PROPS, n_props);
  /**
   * OmarchyNumberField::modified:
   * @self: the numberfield
   * @value: the new value
   *
   * The user stepped or typed a new value (not when the app sets one).
   */
  n_signals[N_MODIFIED] = g_signal_new ("modified", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                        G_TYPE_NONE, 1, G_TYPE_INT);
  gtk_widget_class_set_css_name (wc, "omarchy-number-field");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_SPIN_BUTTON);
}

/**
 * omarchy_number_field_new:
 * @label: (nullable): the label above it
 * @from: the smallest value
 * @to: the largest value
 *
 * Returns: (transfer floating): a number field at @from
 */
GtkWidget *
omarchy_number_field_new (const char *label, int from, int to)
{
  OmarchyNumberField *self = g_object_new (OMARCHY_TYPE_NUMBER_FIELD, "label", label ? label : "", NULL);
  omarchy_number_field_set_range (self, from, to);
  omarchy_number_field_set_value (self, from);
  return GTK_WIDGET (self);
}

OMARCHY_STRING_ACCESSORS (OmarchyNumberField, number_field, label, n_props, N_PROP_LABEL, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyNumberField, number_field, cursor, n_props, N_PROP_CURSOR, FALSE)

int
omarchy_number_field_get_value (OmarchyNumberField *self)
{
  return self->value;
}

void
omarchy_number_field_set_value (OmarchyNumberField *self, int value)
{
  value = CLAMP (value, self->from, self->to);
  if (value != self->value) {
    self->value = value;
    g_object_notify_by_pspec (G_OBJECT (self), n_props[N_PROP_VALUE]);
  }
  number_sync_text (self);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_VALUE_NOW, (double) self->value,
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, (double) self->from,
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_MAX, (double) self->to, -1);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
omarchy_number_field_set_range (OmarchyNumberField *self, int from, int to)
{
  self->from = MIN (from, to);
  self->to = MAX (from, to);
  omarchy_number_field_set_value (self, self->value);
}

void
omarchy_number_field_set_step (OmarchyNumberField *self, int step)
{
  self->step = MAX (1, step);
}

/* ========================================================================== Dropdown ====== */

/**
 * OmarchyDropdown:
 *
 * A labeled single choice: `dropdownWidth` × `controlHeight`, the label `caption` bold in
 * `textDim` above, a chevron at the end; the list opens `xxs` below on the popups surface.
 * Return, Space, Down or j open it; in it j/k or Up/Down walk, Return chooses, Escape closes.
 * Choosing sets #OmarchyDropdown:value and emits ::changed.
 */
struct _OmarchyDropdown {
  GtkWidget parent_instance;
  char *label;
  char *value;
  GPtrArray *values, *labels;
  gboolean cursor;
  GtkPopover *popup;
  GtkWidget *list;
  int current;
};

enum { D_PROP_0, D_PROP_LABEL, D_PROP_VALUE, D_PROP_CURSOR, D_PROP_POPUP_OPEN, D_N_PROPS };
static GParamSpec *d_props[D_N_PROPS];
enum { D_CHANGED, D_N_SIGNALS };
static guint d_signals[D_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyDropdown, omarchy_dropdown, GTK_TYPE_WIDGET)

/* The open list: a private widget drawing the popups surface and its rows. */
#define OMARCHY_TYPE_DROPDOWN_LIST (omarchy_dropdown_list_get_type ())
G_DECLARE_FINAL_TYPE (OmarchyDropdownList, omarchy_dropdown_list, OMARCHY, DROPDOWN_LIST, GtkWidget)
struct _OmarchyDropdownList {
  GtkWidget parent_instance;
  OmarchyDropdown *owner;
};
G_DEFINE_FINAL_TYPE (OmarchyDropdownList, omarchy_dropdown_list, GTK_TYPE_WIDGET)

static OmarchyBorder
dropdown_popup_border (void)
{
  const OmarchyTokens *p = omarchy_paint ();
  return omarchy_border_with_width (&p->popups.border, p->normal.border.widths[0]);
}

static int
dropdown_label_h (OmarchyDropdown *self)
{
  return self->label && *self->label ? omarchy_tokens ()->huge : 0;
}

static void
dropdown_trigger_rect (OmarchyDropdown *self, graphene_rect_t *r)
{
  const OmarchyTokens *t = omarchy_tokens ();
  int label_h = self->label && *self->label ? omarchy_line_height (t->caption) + t->label_gap : 0;
  *r = GRAPHENE_RECT_INIT (0, label_h, gtk_widget_get_width (GTK_WIDGET (self)), t->control_height);
}

static const char *
dropdown_current_label (OmarchyDropdown *self)
{
  for (guint i = 0; i < self->values->len; i++)
    if (!g_strcmp0 (g_ptr_array_index (self->values, i), self->value))
      return g_ptr_array_index (self->labels, i);
  return self->value;
}

static int
dropdown_index_of (OmarchyDropdown *self, const char *value)
{
  for (guint i = 0; i < self->values->len; i++)
    if (!g_strcmp0 (g_ptr_array_index (self->values, i), value))
      return (int) i;
  return -1;
}

static void
dropdown_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyDropdown *self = OMARCHY_DROPDOWN (w);
  const OmarchyTokens *t = omarchy_tokens ();
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = omarchy_space (80);
    *nat = t->dropdown_width;
  } else {
    *min = *nat = t->control_height + dropdown_label_h (self);
  }
}

static void
dropdown_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyDropdown *self = OMARCHY_DROPDOWN (w);
  const OmarchyTokens *p = omarchy_paint ();
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  float W = gtk_widget_get_width (w);
  if (self->label && *self->label) {
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->label, p->caption, TRUE);
    int lw;
    omarchy_layout_size (l, &lw, NULL);
    omarchy_draw_layout (s, l, rtl ? W - lw : 0, 0, &p->text_dim);
  }
  graphene_rect_t r;
  dropdown_trigger_rect (self, &r);
  GtkStateFlags f = gtk_widget_get_state_flags (w);
  gboolean focused = (f & GTK_STATE_FLAG_FOCUSED) && (f & GTK_STATE_FLAG_FOCUS_VISIBLE);
  gboolean hot = self->cursor || (f & GTK_STATE_FLAG_PRELIGHT);
  const GdkRGBA *fill = focused ? &p->focus.fill : hot ? &p->hover.fill : &p->normal.fill;
  const OmarchyBorder *b = focused ? &p->focus.border : hot ? &p->hover.border : &p->normal.border;
  omarchy_draw_surface (s, &r, p->radius, fill, b);

  int line = omarchy_line_height (p->body);
  g_autoptr (PangoLayout) chevron = omarchy_layout (w, "\xf3\xb0\x85\x80", p->body, FALSE);
  int cw;
  omarchy_layout_size (chevron, &cw, NULL);
  float cy = r.origin.y + roundf ((r.size.height - line) / 2);
  float cx = rtl ? b->widths[3] + p->control_gap : W - b->widths[1] - p->control_gap - cw;
  omarchy_draw_layout (s, chevron, cx, cy, &p->text_dim);

  g_autoptr (PangoLayout) text = omarchy_layout (w, dropdown_current_label (self), p->body, FALSE);
  float left = b->widths[3] + p->control_padding_x;
  float room = W - left - b->widths[1] - p->md - cw - p->control_gap;
  pango_layout_set_width (text, (int) (MAX (1, room) * PANGO_SCALE));
  pango_layout_set_ellipsize (text, PANGO_ELLIPSIZE_END);
  if (rtl)
    pango_layout_set_alignment (text, PANGO_ALIGN_RIGHT);
  omarchy_draw_layout (s, text, rtl ? W - left - room : left, cy, &p->foreground);
}

static void
dropdown_size_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyDropdown *self = OMARCHY_DROPDOWN (w);
  if (self->popup)
    gtk_popover_present (self->popup);
}

static void
dropdown_choose (OmarchyDropdown *self, int index)
{
  if (index < 0 || index >= (int) self->values->len)
    return;
  const char *v = g_ptr_array_index (self->values, index);
  omarchy_dropdown_set_value (self, v);
  omarchy_dropdown_close (self);
  g_signal_emit (self, d_signals[D_CHANGED], 0, v);
}

/* -- the list -- */

static int
list_row_y (int i)
{
  const OmarchyTokens *t = omarchy_tokens ();
  OmarchyBorder b = dropdown_popup_border ();
  return (int) b.widths[0] + t->hairline + i * (t->popup_row_height + t->label_gap);
}

static int
list_index_at (OmarchyDropdownList *self, double y)
{
  const OmarchyTokens *t = omarchy_tokens ();
  for (guint i = 0; i < self->owner->values->len; i++) {
    int top = list_row_y ((int) i);
    if (y >= top && y < top + t->popup_row_height)
      return (int) i;
  }
  return -1;
}

static void
list_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyDropdownList *self = OMARCHY_DROPDOWN_LIST (w);
  const OmarchyTokens *t = omarchy_tokens ();
  OmarchyBorder b = dropdown_popup_border ();
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = *nat = MAX (1, gtk_widget_get_width (GTK_WIDGET (self->owner)));
    return;
  }
  int n = (int) self->owner->values->len;
  int shown = MIN (n, 8);
  *min = *nat = shown * t->popup_row_height + MAX (0, shown - 1) * t->label_gap + (int) (b.widths[0] + b.widths[2]) + t->hairline * 2;
}

static void
list_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyDropdownList *self = OMARCHY_DROPDOWN_LIST (w);
  OmarchyDropdown *owner = self->owner;
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  OmarchyBorder b = dropdown_popup_border ();
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), p->radius, &p->popups.background, &b);
  float x0 = b.widths[3] + p->hairline, x1 = W - b.widths[1] - p->hairline;
  int line = omarchy_line_height (p->body);
  gtk_snapshot_push_clip (s, &GRAPHENE_RECT_INIT (x0, b.widths[0] + p->hairline, x1 - x0, H - b.widths[0] - b.widths[2] - 2 * p->hairline));
  for (guint i = 0; i < owner->values->len; i++) {
    float y = list_row_y ((int) i);
    gboolean cur = (int) i == owner->current;
    if (cur)
      omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (x0, y, x1 - x0, p->popup_row_height), 0, &p->hover.fill);
    g_autoptr (PangoLayout) l = omarchy_layout (w, g_ptr_array_index (owner->labels, i), p->body, FALSE);
    pango_layout_set_width (l, (int) ((x1 - x0 - 2 * p->control_padding_x) * PANGO_SCALE));
    pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
    omarchy_draw_layout (s, l, x0 + p->control_padding_x, y + roundf ((p->popup_row_height - line) / 2.0f),
                         cur ? &p->hover.text : &p->popups.text);
  }
  gtk_snapshot_pop (s);
}

static void
list_motion (GtkEventControllerMotion *c, double x, double y, OmarchyDropdownList *self)
{
  int i = list_index_at (self, y);
  if (i >= 0 && i != self->owner->current) {
    self->owner->current = i;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
list_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyDropdownList *self = OMARCHY_DROPDOWN_LIST (w);
  int i = list_index_at (self, y);
  if (i >= 0)
    dropdown_choose (self->owner, i);
}

static gboolean
list_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyDropdownList *self = OMARCHY_DROPDOWN_LIST (w);
  OmarchyDropdown *d = self->owner;
  int n = (int) d->values->len;
  if (keyval == GDK_KEY_Escape) {
    omarchy_dropdown_close (d);
  } else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_j) {
    d->current = MIN (n - 1, d->current + 1);
  } else if (keyval == GDK_KEY_Up || keyval == GDK_KEY_k) {
    d->current = MAX (0, d->current - 1);
  } else if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    dropdown_choose (d, d->current);
  } else {
    return FALSE;
  }
  gtk_widget_queue_draw (w);
  return TRUE;
}

static void
omarchy_dropdown_list_init (OmarchyDropdownList *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  gtk_widget_set_focusable (w, TRUE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, list_clicked);
  omarchy_widget_add_keys (w, list_key);
  GtkEventController *motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "motion", G_CALLBACK (list_motion), self);
  gtk_widget_add_controller (w, motion);
}

static void
omarchy_dropdown_list_class_init (OmarchyDropdownListClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  wc->measure = list_measure;
  wc->snapshot = list_snapshot;
  gtk_widget_class_set_css_name (wc, "omarchy-dropdown-list");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_LIST_BOX);
}

/* -- the trigger -- */

static void
dropdown_popup_closed (GtkPopover *pop, OmarchyDropdown *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), d_props[D_PROP_POPUP_OPEN]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
dropdown_toggle (OmarchyDropdown *self)
{
  if (omarchy_dropdown_get_popup_open (self))
    omarchy_dropdown_close (self);
  else
    omarchy_dropdown_open (self);
}

static void
dropdown_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyDropdown *self = OMARCHY_DROPDOWN (w);
  graphene_rect_t r;
  dropdown_trigger_rect (self, &r);
  if (button == GDK_BUTTON_PRIMARY && y >= r.origin.y) {
    gtk_widget_grab_focus (w);
    dropdown_toggle (self);
  }
}

static gboolean
dropdown_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyDropdown *self = OMARCHY_DROPDOWN (w);
  if (omarchy_key_is_activate (keyval) || keyval == GDK_KEY_Down || keyval == GDK_KEY_j) {
    dropdown_toggle (self);
    return TRUE;
  }
  if (keyval == GDK_KEY_Escape && omarchy_dropdown_get_popup_open (self)) {
    omarchy_dropdown_close (self);
    return TRUE;
  }
  return FALSE;
}

static void
omarchy_dropdown_init (OmarchyDropdown *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->label = g_strdup ("");
  self->value = g_strdup ("");
  self->values = g_ptr_array_new_with_free_func (g_free);
  self->labels = g_ptr_array_new_with_free_func (g_free);
  self->current = -1;
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, TRUE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, dropdown_clicked);
  omarchy_widget_add_keys (w, dropdown_key);
}

static void
dropdown_dispose (GObject *o)
{
  OmarchyDropdown *self = OMARCHY_DROPDOWN (o);
  if (self->popup) {
    gtk_widget_unparent (GTK_WIDGET (self->popup));
    self->popup = NULL;
    self->list = NULL;
  }
  G_OBJECT_CLASS (omarchy_dropdown_parent_class)->dispose (o);
}

static void
dropdown_finalize (GObject *o)
{
  OmarchyDropdown *self = OMARCHY_DROPDOWN (o);
  g_free (self->label);
  g_free (self->value);
  g_ptr_array_unref (self->values);
  g_ptr_array_unref (self->labels);
  G_OBJECT_CLASS (omarchy_dropdown_parent_class)->finalize (o);
}

static void
dropdown_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyDropdown *self = OMARCHY_DROPDOWN (o);
  switch (id) {
  case D_PROP_LABEL: omarchy_dropdown_set_label (self, g_value_get_string (v)); break;
  case D_PROP_VALUE: omarchy_dropdown_set_value (self, g_value_get_string (v)); break;
  case D_PROP_CURSOR: omarchy_dropdown_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
dropdown_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyDropdown *self = OMARCHY_DROPDOWN (o);
  switch (id) {
  case D_PROP_LABEL: g_value_set_string (v, self->label); break;
  case D_PROP_VALUE: g_value_set_string (v, self->value); break;
  case D_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case D_PROP_POPUP_OPEN: g_value_set_boolean (v, omarchy_dropdown_get_popup_open (self)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_dropdown_class_init (OmarchyDropdownClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = dropdown_dispose;
  oc->finalize = dropdown_finalize;
  oc->set_property = dropdown_set_property;
  oc->get_property = dropdown_get_property;
  wc->measure = dropdown_measure;
  wc->snapshot = dropdown_snapshot;
  wc->size_allocate = dropdown_size_allocate;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  d_props[D_PROP_LABEL] = g_param_spec_string ("label", NULL, NULL, "", f);
  d_props[D_PROP_VALUE] = g_param_spec_string ("value", NULL, NULL, "", f);
  d_props[D_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  d_props[D_PROP_POPUP_OPEN] = g_param_spec_boolean ("popup-open", NULL, NULL, FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, D_N_PROPS, d_props);
  /**
   * OmarchyDropdown::changed:
   * @self: the dropdown
   * @value: the chosen option's value
   */
  d_signals[D_CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 1, G_TYPE_STRING);
  gtk_widget_class_set_css_name (wc, "omarchy-dropdown");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_COMBO_BOX);
}

/**
 * omarchy_dropdown_new:
 * @label: (nullable): the label above the trigger
 *
 * Returns: (transfer floating): an empty dropdown; add options with
 *   [method@Omarchy.Dropdown.append]
 */
GtkWidget *
omarchy_dropdown_new (const char *label)
{
  return g_object_new (OMARCHY_TYPE_DROPDOWN, "label", label ? label : "", NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyDropdown, dropdown, label, d_props, D_PROP_LABEL, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyDropdown, dropdown, cursor, d_props, D_PROP_CURSOR, FALSE)

void
omarchy_dropdown_append (OmarchyDropdown *self, const char *value, const char *label)
{
  g_ptr_array_add (self->values, g_strdup (value));
  g_ptr_array_add (self->labels, g_strdup (label ? label : value));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  if (self->list)
    gtk_widget_queue_resize (self->list);
}

void
omarchy_dropdown_remove_all (OmarchyDropdown *self)
{
  g_ptr_array_set_size (self->values, 0);
  g_ptr_array_set_size (self->labels, 0);
  self->current = -1;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

const char *
omarchy_dropdown_get_value (OmarchyDropdown *self)
{
  return self->value;
}

void
omarchy_dropdown_set_value (OmarchyDropdown *self, const char *value)
{
  if (!g_strcmp0 (self->value, value))
    return;
  g_free (self->value);
  self->value = g_strdup (value ? value : "");
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), d_props[D_PROP_VALUE]);
}

gboolean
omarchy_dropdown_get_popup_open (OmarchyDropdown *self)
{
  return self->popup && gtk_widget_get_visible (GTK_WIDGET (self->popup));
}

/**
 * omarchy_dropdown_get_current_index:
 * @self: the dropdown
 *
 * Returns: the row the open list's cursor is on, or -1
 */
int
omarchy_dropdown_get_current_index (OmarchyDropdown *self)
{
  return self->current;
}

void
omarchy_dropdown_open (OmarchyDropdown *self)
{
  if (!self->popup) {
    self->list = g_object_new (OMARCHY_TYPE_DROPDOWN_LIST, NULL);
    OMARCHY_DROPDOWN_LIST (self->list)->owner = self;
    self->popup = omarchy_popup_new (GTK_WIDGET (self), self->list);
    g_signal_connect (self->popup, "closed", G_CALLBACK (dropdown_popup_closed), self);
  }
  self->current = MAX (0, dropdown_index_of (self, self->value));
  graphene_rect_t r;
  dropdown_trigger_rect (self, &r);
  gtk_widget_queue_resize (self->list);
  omarchy_popup_show_below (self->popup, GTK_WIDGET (self), &r, omarchy_tokens ()->xxs);
  gtk_widget_grab_focus (self->list);
  g_object_notify_by_pspec (G_OBJECT (self), d_props[D_PROP_POPUP_OPEN]);
}

void
omarchy_dropdown_close (OmarchyDropdown *self)
{
  if (self->popup && gtk_widget_get_visible (GTK_WIDGET (self->popup))) {
    gtk_popover_popdown (self->popup);
    gtk_widget_grab_focus (GTK_WIDGET (self));
  }
}
