/* omarchy-button.c: Button (Button.qml), IconButton (PanelActionButton.qml) and ButtonGroup
 * (ButtonGroup.qml), drawn as the Qt kit draws them. */
#include "omarchy-private.h"

#include <math.h>

/* A state color recolored onto another base (an urgent button's hover fill: urgent at the hover
 * fill's alpha). */
static GdkRGBA
recolor (const GdkRGBA *state, const GdkRGBA *base)
{
  return (GdkRGBA) { base->red, base->green, base->blue, state->alpha };
}

static OmarchyBorder
recolor_border (const OmarchyBorder *b, const GdkRGBA *base)
{
  OmarchyBorder out = *b;
  out.color = recolor (&b->color, base);
  out.n_stops = 0;
  return out;
}

/* ================================================================================ Button === */

/**
 * OmarchyButton:
 *
 * The button: a label and an optional glyph. Borderless at rest unless
 * #OmarchyButton:bordered; the hover-cursor fill and border under the pointer, keyboard focus or
 * #OmarchyButton:cursor; #OmarchyButton:selected fills and bolds the label. Return, Enter and
 * Space click it while focused.
 */
struct _OmarchyButton {
  GtkWidget parent_instance;
  char *label;
  char *icon;
  gboolean bordered, selected, active, cursor, destructive, left_align;
  gboolean has_foreground;
  GdkRGBA foreground;
};

enum { B_PROP_0, B_PROP_LABEL, B_PROP_ICON, B_PROP_BORDERED, B_PROP_SELECTED, B_PROP_ACTIVE, B_PROP_CURSOR,
       B_PROP_DESTRUCTIVE, B_PROP_LEFT_ALIGN, B_N_PROPS };
static GParamSpec *b_props[B_N_PROPS];
enum { B_CLICKED, B_RIGHT_CLICKED, B_N_SIGNALS };
static guint b_signals[B_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyButton, omarchy_button, GTK_TYPE_WIDGET)

static void
button_reserve (OmarchyButton *self, float out[4])
{
  const OmarchyTokens *t = omarchy_tokens ();
  const OmarchyBorder *specs[] = { &t->focus.border, &t->hover.border, &t->selected.border,
                                   self->bordered ? &t->normal.border : NULL };
  omarchy_border_reserve (out, specs, 4);
}

typedef struct {
  int icon_w, icon_h, icon_x;
  int label_w, label_h;
  int content_w, row_h;
} ButtonContent;

static void
button_content (OmarchyButton *self, ButtonContent *c, PangoLayout **icon_out, PangoLayout **label_out)
{
  const OmarchyTokens *t = omarchy_tokens ();
  *c = (ButtonContent) { 0 };
  PangoLayout *icon = NULL, *label = NULL;
  if (self->icon && *self->icon) {
    icon = omarchy_layout (GTK_WIDGET (self), self->icon, t->icon, FALSE);
    c->icon_x = omarchy_glyph_box (icon, &c->icon_w, NULL);
    c->icon_h = omarchy_line_height (t->icon);
  }
  if (self->label && *self->label) {
    label = omarchy_layout (GTK_WIDGET (self), self->label, t->body, self->selected);
    PangoRectangle logical;
    pango_layout_get_extents (label, NULL, &logical);
    c->label_w = (int) ceil (logical.width / (double) PANGO_SCALE - 1e-3);
    c->label_h = omarchy_line_height (t->body);
  }
  c->content_w = (icon ? c->icon_w + (label ? t->control_gap : 0) : 0) + (label ? c->label_w : 0);
  c->row_h = label ? c->label_h : c->icon_h;
  if (icon_out)
    *icon_out = icon;
  else if (icon)
    g_object_unref (icon);
  if (label_out)
    *label_out = label;
  else if (label)
    g_object_unref (label);
}

static void
button_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyButton *self = OMARCHY_BUTTON (w);
  const OmarchyTokens *t = omarchy_tokens ();
  float r[4];
  button_reserve (self, r);
  ButtonContent c;
  button_content (self, &c, NULL, NULL);
  if (o == GTK_ORIENTATION_HORIZONTAL)
    *min = *nat = (int) ceil (c.content_w + t->control_padding_x * 2 + r[3] + r[1]);
  else
    *min = *nat = (int) ceil (c.row_h + t->control_padding_y * 2 + r[0] + r[2]);
}

static void
button_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyButton *self = OMARCHY_BUTTON (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gboolean pressed = omarchy_widget_pressed (w);
  gboolean focus = omarchy_widget_focus_shown (w);
  gboolean hot = omarchy_widget_hovered (w) || self->cursor;
  const GdkRGBA *fg = self->destructive ? &p->urgent : self->has_foreground ? &self->foreground : &p->foreground;

  GdkRGBA fill = { 0, 0, 0, 0 };
  if (pressed)
    fill = recolor (&p->pressed, fg);
  else if (focus)
    fill = recolor (&p->focus.fill, fg);
  else if (hot)
    fill = recolor (&p->hover.fill, fg);
  else if (self->selected || self->active)
    fill = recolor (&p->selected.fill, fg);

  OmarchyBorder none = omarchy_border_none ();
  const OmarchyBorder *spec = &none;
  if (focus)
    spec = &p->focus.border;
  else if (hot)
    spec = &p->hover.border;
  else if (self->selected)
    spec = !omarchy_border_is_none (&p->selected.border) ? &p->selected.border : self->bordered ? &p->normal.border : &none;
  else if (self->bordered)
    spec = &p->normal.border;
  OmarchyBorder border = self->destructive ? recolor_border (spec, fg) : *spec;

  if (pressed && !p->reduce_motion) {
    /* Held down, the button gives a little. */
    gtk_snapshot_save (s);
    gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (W / 2, H / 2));
    gtk_snapshot_scale (s, 0.97f, 0.97f);
    gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (-W / 2, -H / 2));
  }
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), p->radius, &fill, &border);

  PangoLayout *icon, *label;
  ButtonContent c;
  button_content (self, &c, &icon, &label);
  float r[4];
  button_reserve (self, r);
  GdkRGBA ink = self->selected ? (self->destructive ? *fg : p->selected.text) : *fg;
  if (!gtk_widget_is_sensitive (w))
    ink = p->text_faint;
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  float x = self->left_align ? (rtl ? W - r[1] - p->control_padding_x - c.content_w : r[3] + p->control_padding_x)
                             : roundf ((W - c.content_w) / 2);
  float row_y = (H - c.row_h) / 2;
  /* The label shrinks to fit, then elides, when the button is held narrower than it. */
  float room = W - p->control_padding_x - r[3] - r[1] - (icon ? c.icon_w + (label ? p->control_gap : 0) : 0);
  if (label && c.label_w > room && room > 0) {
    pango_layout_set_width (label, (int) (room * PANGO_SCALE));
    pango_layout_set_ellipsize (label, PANGO_ELLIPSIZE_END);
    c.label_w = (int) room;
    x = self->left_align ? x : roundf ((W - (c.content_w - (c.content_w - (icon ? c.icon_w + p->control_gap : 0) - c.label_w))) / 2);
  }
  if (rtl && !self->left_align) {
    /* Mirrored: the label first, then the glyph. */
    if (label)
      omarchy_draw_layout (s, label, x, roundf (row_y + (c.row_h - c.label_h) / 2.0f), &ink);
    if (icon)
      omarchy_draw_layout (s, icon, x + (label ? c.label_w + p->control_gap : 0) + c.icon_x,
                           roundf (row_y + (c.row_h - c.icon_h) / 2.0f), &ink);
  } else {
    if (icon) {
      omarchy_draw_layout (s, icon, x + c.icon_x, roundf (row_y + (c.row_h - c.icon_h) / 2.0f), &ink);
      x += c.icon_w + (label ? p->control_gap : 0);
    }
    if (label)
      omarchy_draw_layout (s, label, x, roundf (row_y + (c.row_h - c.label_h) / 2.0f), &ink);
  }
  g_clear_object (&icon);
  g_clear_object (&label);
  if (pressed && !p->reduce_motion)
    gtk_snapshot_restore (s);
}

static void
button_clicked (GtkWidget *w, guint button, double x, double y)
{
  if (button == GDK_BUTTON_SECONDARY)
    g_signal_emit (w, b_signals[B_RIGHT_CLICKED], 0);
  else
    g_signal_emit (w, b_signals[B_CLICKED], 0);
}

static gboolean
button_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  if (omarchy_key_is_activate (keyval)) {
    g_signal_emit (w, b_signals[B_CLICKED], 0);
    return TRUE;
  }
  return FALSE;
}

static void
omarchy_button_init (OmarchyButton *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->label = g_strdup ("");
  self->icon = g_strdup ("");
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  GtkGesture *g = omarchy_widget_add_click (w, button_clicked);
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (g), 0);
  omarchy_widget_add_keys (w, button_key);
}

static void
button_finalize (GObject *o)
{
  OmarchyButton *self = OMARCHY_BUTTON (o);
  g_free (self->label);
  g_free (self->icon);
  G_OBJECT_CLASS (omarchy_button_parent_class)->finalize (o);
}

static void
button_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyButton *self = OMARCHY_BUTTON (o);
  switch (id) {
  case B_PROP_LABEL: omarchy_button_set_label (self, g_value_get_string (v)); break;
  case B_PROP_ICON: omarchy_button_set_icon (self, g_value_get_string (v)); break;
  case B_PROP_BORDERED: omarchy_button_set_bordered (self, g_value_get_boolean (v)); break;
  case B_PROP_SELECTED: omarchy_button_set_selected (self, g_value_get_boolean (v)); break;
  case B_PROP_ACTIVE: omarchy_button_set_active (self, g_value_get_boolean (v)); break;
  case B_PROP_CURSOR: omarchy_button_set_cursor (self, g_value_get_boolean (v)); break;
  case B_PROP_DESTRUCTIVE: omarchy_button_set_destructive (self, g_value_get_boolean (v)); break;
  case B_PROP_LEFT_ALIGN: omarchy_button_set_left_align (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
button_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyButton *self = OMARCHY_BUTTON (o);
  switch (id) {
  case B_PROP_LABEL: g_value_set_string (v, self->label); break;
  case B_PROP_ICON: g_value_set_string (v, self->icon); break;
  case B_PROP_BORDERED: g_value_set_boolean (v, self->bordered); break;
  case B_PROP_SELECTED: g_value_set_boolean (v, self->selected); break;
  case B_PROP_ACTIVE: g_value_set_boolean (v, self->active); break;
  case B_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case B_PROP_DESTRUCTIVE: g_value_set_boolean (v, self->destructive); break;
  case B_PROP_LEFT_ALIGN: g_value_set_boolean (v, self->left_align); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_button_class_init (OmarchyButtonClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = button_finalize;
  oc->set_property = button_set_property;
  oc->get_property = button_get_property;
  wc->measure = button_measure;
  wc->snapshot = button_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  b_props[B_PROP_LABEL] = g_param_spec_string ("label", NULL, NULL, "", f);
  /**
   * OmarchyButton:icon:
   *
   * A glyph before the label: a Nerd Font codepoint (``), as the shell uses.
   */
  b_props[B_PROP_ICON] = g_param_spec_string ("icon", NULL, NULL, "", f);
  /**
   * OmarchyButton:bordered:
   *
   * The normal border at rest (plain buttons are borderless until hovered).
   */
  b_props[B_PROP_BORDERED] = g_param_spec_boolean ("bordered", NULL, NULL, FALSE, f);
  /**
   * OmarchyButton:selected:
   *
   * The persistent chosen state: selected fill, bold label in the selected text color.
   */
  b_props[B_PROP_SELECTED] = g_param_spec_boolean ("selected", NULL, NULL, FALSE, f);
  /**
   * OmarchyButton:active:
   *
   * The selected fill without the bold label.
   */
  b_props[B_PROP_ACTIVE] = g_param_spec_boolean ("active", NULL, NULL, FALSE, f);
  /**
   * OmarchyButton:cursor:
   *
   * Force the hover-cursor look, as a keyboard-driven panel's cursor does.
   */
  b_props[B_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyButton:destructive:
   *
   * Drawn in urgent: for destructive actions only.
   */
  b_props[B_PROP_DESTRUCTIVE] = g_param_spec_boolean ("destructive", NULL, NULL, FALSE, f);
  b_props[B_PROP_LEFT_ALIGN] = g_param_spec_boolean ("left-align", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, B_N_PROPS, b_props);
  /**
   * OmarchyButton::clicked:
   *
   * A primary click, or Return, Enter or Space while focused.
   */
  b_signals[B_CLICKED] = g_signal_new ("clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0,
                                       NULL, NULL, NULL, G_TYPE_NONE, 0);
  b_signals[B_RIGHT_CLICKED] = g_signal_new ("right-clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                             NULL, NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_activate_signal (wc, b_signals[B_CLICKED]);
  gtk_widget_class_set_css_name (wc, "omarchy-button");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_BUTTON);
}

/**
 * omarchy_button_new:
 * @label: (nullable): the label
 *
 * Returns: (transfer floating): a plain button
 */
GtkWidget *
omarchy_button_new (const char *label)
{
  return g_object_new (OMARCHY_TYPE_BUTTON, "label", label ? label : "", NULL);
}

OMARCHY_BOOL_ACCESSORS (OmarchyButton, button, bordered, b_props, B_PROP_BORDERED, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyButton, button, selected, b_props, B_PROP_SELECTED, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyButton, button, active, b_props, B_PROP_ACTIVE, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyButton, button, cursor, b_props, B_PROP_CURSOR, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyButton, button, destructive, b_props, B_PROP_DESTRUCTIVE, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyButton, button, left_align, b_props, B_PROP_LEFT_ALIGN, FALSE)

const char *
omarchy_button_get_label (OmarchyButton *self)
{
  return self->label;
}

void
omarchy_button_set_label (OmarchyButton *self, const char *label)
{
  if (!g_strcmp0 (self->label, label))
    return;
  g_free (self->label);
  self->label = g_strdup (label ? label : "");
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_LABEL, self->label, -1);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), b_props[B_PROP_LABEL]);
}

/**
 * omarchy_button_set_foreground:
 * @self: the button
 * @color: (nullable): the ink and the base of its state fills, or %NULL for the theme's
 *   foreground
 *
 * For a button on a surface with its own text color (a toast on the notifications surface).
 * A fixed color doesn't follow the theme: set it again from #OmarchyTheme::repaint.
 */
void
omarchy_button_set_foreground (OmarchyButton *self, const GdkRGBA *color)
{
  self->has_foreground = color != NULL;
  if (color)
    self->foreground = *color;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

const char *
omarchy_button_get_icon (OmarchyButton *self)
{
  return self->icon;
}

void
omarchy_button_set_icon (OmarchyButton *self, const char *glyph)
{
  if (!g_strcmp0 (self->icon, glyph))
    return;
  g_free (self->icon);
  self->icon = g_strdup (glyph ? glyph : "");
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), b_props[B_PROP_ICON]);
}

/* ============================================================================ IconButton === */

/**
 * OmarchyIconButton:
 *
 * A square, odd-sized button with one glyph: `max(space(22), icon + sm × 2) | 1`.
 */
struct _OmarchyIconButton {
  GtkWidget parent_instance;
  char *icon;
  gboolean bordered, cursor, destructive;
};

enum { I_PROP_0, I_PROP_ICON, I_PROP_BORDERED, I_PROP_CURSOR, I_PROP_DESTRUCTIVE, I_N_PROPS };
static GParamSpec *i_props[I_N_PROPS];
enum { I_CLICKED, I_N_SIGNALS };
static guint i_signals[I_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyIconButton, omarchy_icon_button, GTK_TYPE_WIDGET)

static int
icon_button_size (void)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return ((int) lround (MAX (omarchy_space (22), t->icon + t->sm * 2))) | 1;
}

static void
icon_button_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  *min = *nat = icon_button_size ();
}

static void
icon_button_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyIconButton *self = OMARCHY_ICON_BUTTON (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gboolean enabled = gtk_widget_is_sensitive (w);
  gboolean focus = omarchy_widget_focus_shown (w);
  gboolean hot = (omarchy_widget_hovered (w) || self->cursor) && enabled;
  const GdkRGBA *hover = self->destructive ? &p->urgent : &p->foreground;
  GdkRGBA fill = focus ? recolor (&p->focus.fill, hover) : hot ? recolor (&p->hover.fill, hover) : (GdkRGBA) { 0 };
  OmarchyBorder border = focus ? recolor_border (&p->focus.border, hover)
                         : (hot && self->bordered) ? recolor_border (&p->hover.border, hover)
                         : self->bordered ? p->normal.border : omarchy_border_none ();
  if (focus && !self->destructive)
    border = p->focus.border;
  else if (hot && self->bordered && !self->destructive)
    border = p->hover.border;
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), omarchy_radius_small (), &fill, &border);
  if (self->icon && *self->icon) {
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->icon, p->icon, FALSE);
    double cx = omarchy_ink_center_x (w, self->icon, p->icon, FALSE);
    int gh = omarchy_line_height (p->icon);
    float gx = roundf ((float) (W / 2.0 - cx));
    float gy = roundf ((H - gh) / 2.0f);
    GdkRGBA ink = enabled ? (hot ? *hover : p->foreground) : p->text_faint;
    omarchy_draw_layout (s, l, gx, gy, &ink);
  }
}

static void
icon_button_clicked (GtkWidget *w, guint button, double x, double y)
{
  if (button == GDK_BUTTON_PRIMARY)
    g_signal_emit (w, i_signals[I_CLICKED], 0);
}

static gboolean
icon_button_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  if (omarchy_key_is_activate (keyval)) {
    g_signal_emit (w, i_signals[I_CLICKED], 0);
    return TRUE;
  }
  return FALSE;
}

static void
omarchy_icon_button_init (OmarchyIconButton *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->icon = g_strdup ("");
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, icon_button_clicked);
  omarchy_widget_add_keys (w, icon_button_key);
}

static void
icon_button_finalize (GObject *o)
{
  g_free (OMARCHY_ICON_BUTTON (o)->icon);
  G_OBJECT_CLASS (omarchy_icon_button_parent_class)->finalize (o);
}

static void
icon_button_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyIconButton *self = OMARCHY_ICON_BUTTON (o);
  switch (id) {
  case I_PROP_ICON: omarchy_icon_button_set_icon (self, g_value_get_string (v)); break;
  case I_PROP_BORDERED: omarchy_icon_button_set_bordered (self, g_value_get_boolean (v)); break;
  case I_PROP_CURSOR: omarchy_icon_button_set_cursor (self, g_value_get_boolean (v)); break;
  case I_PROP_DESTRUCTIVE: omarchy_icon_button_set_destructive (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
icon_button_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyIconButton *self = OMARCHY_ICON_BUTTON (o);
  switch (id) {
  case I_PROP_ICON: g_value_set_string (v, self->icon); break;
  case I_PROP_BORDERED: g_value_set_boolean (v, self->bordered); break;
  case I_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case I_PROP_DESTRUCTIVE: g_value_set_boolean (v, self->destructive); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_icon_button_class_init (OmarchyIconButtonClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = icon_button_finalize;
  oc->set_property = icon_button_set_property;
  oc->get_property = icon_button_get_property;
  wc->measure = icon_button_measure;
  wc->snapshot = icon_button_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  i_props[I_PROP_ICON] = g_param_spec_string ("icon", NULL, NULL, "", f);
  i_props[I_PROP_BORDERED] = g_param_spec_boolean ("bordered", NULL, NULL, FALSE, f);
  i_props[I_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyIconButton:destructive:
   *
   * Hover in urgent: forget, unpair, delete.
   */
  i_props[I_PROP_DESTRUCTIVE] = g_param_spec_boolean ("destructive", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, I_N_PROPS, i_props);
  i_signals[I_CLICKED] = g_signal_new ("clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0,
                                       NULL, NULL, NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_activate_signal (wc, i_signals[I_CLICKED]);
  gtk_widget_class_set_css_name (wc, "omarchy-icon-button");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_BUTTON);
}

/**
 * omarchy_icon_button_new:
 * @glyph: (nullable): a Nerd Font glyph
 *
 * Returns: (transfer floating): an icon button
 */
GtkWidget *
omarchy_icon_button_new (const char *glyph)
{
  return g_object_new (OMARCHY_TYPE_ICON_BUTTON, "icon", glyph ? glyph : "", NULL);
}

OMARCHY_BOOL_ACCESSORS (OmarchyIconButton, icon_button, bordered, i_props, I_PROP_BORDERED, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyIconButton, icon_button, cursor, i_props, I_PROP_CURSOR, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyIconButton, icon_button, destructive, i_props, I_PROP_DESTRUCTIVE, FALSE)

const char *
omarchy_icon_button_get_icon (OmarchyIconButton *self)
{
  return self->icon;
}

void
omarchy_icon_button_set_icon (OmarchyIconButton *self, const char *glyph)
{
  if (!g_strcmp0 (self->icon, glyph))
    return;
  g_free (self->icon);
  self->icon = g_strdup (glyph ? glyph : "");
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), i_props[I_PROP_ICON]);
}

/* =========================================================================== ButtonGroup === */

/**
 * OmarchyButtonGroup:
 *
 * Bordered buttons `md` apart, one selected: a segmented choice. One Tab stop; Left/Right (and
 * h/l) walk the chips, Return or Space chooses. Choosing sets #OmarchyButtonGroup:value and
 * emits ::changed.
 */
struct _OmarchyButtonGroup {
  GtkWidget parent_instance;
  GPtrArray *values;
  char *value;
  int cursor_index;
  int focused_index;
};

enum { G_PROP_0, G_PROP_VALUE, G_PROP_CURSOR_INDEX, G_N_PROPS };
static GParamSpec *g_props[G_N_PROPS];
enum { G_CHANGED, G_N_SIGNALS };
static guint g_signals[G_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyButtonGroup, omarchy_button_group, GTK_TYPE_WIDGET)

static void
group_sync (OmarchyButtonGroup *self)
{
  gboolean focused = gtk_widget_has_focus (GTK_WIDGET (self));
  int i = 0;
  for (GtkWidget *c = gtk_widget_get_first_child (GTK_WIDGET (self)); c; c = gtk_widget_get_next_sibling (c), i++) {
    omarchy_button_set_selected (OMARCHY_BUTTON (c), !g_strcmp0 (g_ptr_array_index (self->values, i), self->value));
    omarchy_button_set_cursor (OMARCHY_BUTTON (c), self->cursor_index == i || (focused && self->focused_index == i));
  }
}

static int
group_selected_index (OmarchyButtonGroup *self)
{
  for (guint i = 0; i < self->values->len; i++)
    if (!g_strcmp0 (g_ptr_array_index (self->values, i), self->value))
      return (int) i;
  return -1;
}

static void
group_choose (OmarchyButtonGroup *self, int index)
{
  if (index < 0 || index >= (int) self->values->len)
    return;
  const char *v = g_ptr_array_index (self->values, index);
  if (gtk_widget_has_focus (GTK_WIDGET (self)))
    self->focused_index = index;
  omarchy_button_group_set_value (self, v);
  g_signal_emit (self, g_signals[G_CHANGED], 0, v);
}

static void
chip_clicked (OmarchyButton *chip, OmarchyButtonGroup *self)
{
  int i = 0;
  for (GtkWidget *c = gtk_widget_get_first_child (GTK_WIDGET (self)); c; c = gtk_widget_get_next_sibling (c), i++)
    if (c == GTK_WIDGET (chip))
      group_choose (self, i);
}

static gboolean
group_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyButtonGroup *self = OMARCHY_BUTTON_GROUP (w);
  guint k = omarchy_reading_key (w, keyval, TRUE);
  int n = (int) self->values->len;
  if (k == GDK_KEY_Left) {
    self->focused_index = MAX (0, (self->focused_index < 0 ? 0 : self->focused_index) - 1);
  } else if (k == GDK_KEY_Right) {
    self->focused_index = MIN (n - 1, (self->focused_index < 0 ? 0 : self->focused_index) + 1);
  } else if (omarchy_key_is_activate (keyval)) {
    group_choose (self, self->focused_index);
  } else {
    return FALSE;
  }
  group_sync (self);
  return TRUE;
}

static void
group_focus_changed (GtkWidget *w, GParamSpec *pspec, gpointer data)
{
  OmarchyButtonGroup *self = OMARCHY_BUTTON_GROUP (w);
  if (gtk_widget_has_focus (w)) {
    int idx = group_selected_index (self);
    self->focused_index = idx < 0 ? 0 : idx;
  } else {
    self->focused_index = -1;
  }
  group_sync (self);
}

static void
group_restyle (OmarchyButtonGroup *self)
{
  GtkLayoutManager *lm = gtk_widget_get_layout_manager (GTK_WIDGET (self));
  gtk_box_layout_set_spacing (GTK_BOX_LAYOUT (lm), omarchy_tokens ()->md);
}

static void
omarchy_button_group_init (OmarchyButtonGroup *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->values = g_ptr_array_new_with_free_func (g_free);
  self->value = g_strdup ("");
  self->cursor_index = -1;
  self->focused_index = -1;
  gtk_widget_set_focusable (w, TRUE);
  group_restyle (self);
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (group_restyle), self, G_CONNECT_SWAPPED);
  g_signal_connect (w, "notify::has-focus", G_CALLBACK (group_focus_changed), NULL);
  omarchy_widget_add_keys (w, group_key);
}

static void
group_dispose (GObject *o)
{
  omarchy_widget_dispose_children (GTK_WIDGET (o));
  G_OBJECT_CLASS (omarchy_button_group_parent_class)->dispose (o);
}

static void
group_finalize (GObject *o)
{
  OmarchyButtonGroup *self = OMARCHY_BUTTON_GROUP (o);
  g_ptr_array_unref (self->values);
  g_free (self->value);
  G_OBJECT_CLASS (omarchy_button_group_parent_class)->finalize (o);
}

static void
group_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyButtonGroup *self = OMARCHY_BUTTON_GROUP (o);
  switch (id) {
  case G_PROP_VALUE: omarchy_button_group_set_value (self, g_value_get_string (v)); break;
  case G_PROP_CURSOR_INDEX: omarchy_button_group_set_cursor_index (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
group_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyButtonGroup *self = OMARCHY_BUTTON_GROUP (o);
  switch (id) {
  case G_PROP_VALUE: g_value_set_string (v, self->value); break;
  case G_PROP_CURSOR_INDEX: g_value_set_int (v, self->cursor_index); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_button_group_class_init (OmarchyButtonGroupClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = group_dispose;
  oc->finalize = group_finalize;
  oc->set_property = group_set_property;
  oc->get_property = group_get_property;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  g_props[G_PROP_VALUE] = g_param_spec_string ("value", NULL, NULL, "", f);
  /**
   * OmarchyButtonGroup:cursor-index:
   *
   * The chip a panel's keyboard cursor is on, or -1.
   */
  g_props[G_PROP_CURSOR_INDEX] = g_param_spec_int ("cursor-index", NULL, NULL, -1, G_MAXINT, -1, f);
  g_object_class_install_properties (oc, G_N_PROPS, g_props);
  /**
   * OmarchyButtonGroup::changed:
   * @self: the buttongroup
   * @value: the chosen option's value
   */
  g_signals[G_CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 1, G_TYPE_STRING);
  gtk_widget_class_set_layout_manager_type (wc, GTK_TYPE_BOX_LAYOUT);
  gtk_widget_class_set_css_name (wc, "omarchy-button-group");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_RADIO_GROUP);
}

/**
 * omarchy_button_group_new:
 *
 * Returns: (transfer floating): an empty group; add options with
 *   [method@Omarchy.ButtonGroup.append]
 */
GtkWidget *
omarchy_button_group_new (void)
{
  return g_object_new (OMARCHY_TYPE_BUTTON_GROUP, NULL);
}

/**
 * omarchy_button_group_append_icon:
 * @self: the group
 * @value: what ::changed reports
 * @label: what the chip shows
 * @glyph: (nullable): a glyph before the label
 */
void
omarchy_button_group_append_icon (OmarchyButtonGroup *self, const char *value, const char *label, const char *glyph)
{
  GtkWidget *chip = omarchy_button_new (label);
  omarchy_button_set_bordered (OMARCHY_BUTTON (chip), TRUE);
  if (glyph)
    omarchy_button_set_icon (OMARCHY_BUTTON (chip), glyph);
  gtk_widget_set_focusable (chip, FALSE);
  gtk_widget_set_valign (chip, GTK_ALIGN_CENTER);
  g_signal_connect (chip, "clicked", G_CALLBACK (chip_clicked), self);
  gtk_widget_set_parent (chip, GTK_WIDGET (self));
  g_ptr_array_add (self->values, g_strdup (value));
  group_sync (self);
}

/**
 * omarchy_button_group_append:
 * @self: the group
 * @value: what ::changed reports
 * @label: what the chip shows
 */
void
omarchy_button_group_append (OmarchyButtonGroup *self, const char *value, const char *label)
{
  omarchy_button_group_append_icon (self, value, label, NULL);
}

void
omarchy_button_group_remove_all (OmarchyButtonGroup *self)
{
  omarchy_widget_dispose_children (GTK_WIDGET (self));
  g_ptr_array_set_size (self->values, 0);
}

const char *
omarchy_button_group_get_value (OmarchyButtonGroup *self)
{
  return self->value;
}

void
omarchy_button_group_set_value (OmarchyButtonGroup *self, const char *value)
{
  if (!g_strcmp0 (self->value, value))
    return;
  g_free (self->value);
  self->value = g_strdup (value ? value : "");
  group_sync (self);
  g_object_notify_by_pspec (G_OBJECT (self), g_props[G_PROP_VALUE]);
}

int
omarchy_button_group_get_cursor_index (OmarchyButtonGroup *self)
{
  return self->cursor_index;
}

void
omarchy_button_group_set_cursor_index (OmarchyButtonGroup *self, int index)
{
  if (self->cursor_index == index)
    return;
  self->cursor_index = index;
  group_sync (self);
  g_object_notify_by_pspec (G_OBJECT (self), g_props[G_PROP_CURSOR_INDEX]);
}

/**
 * omarchy_button_group_get_focused_index:
 * @self: the group
 *
 * Returns: the chip Left/Right have walked to while the group has focus, or -1
 */
int
omarchy_button_group_get_focused_index (OmarchyButtonGroup *self)
{
  return self->focused_index;
}
