/* omarchy-confirm-dialog.c: ConfirmDialog (ConfirmDialog.qml): a scrim over its host and a card
 * with a message, Cancel, and Confirm in urgent. */
#include "omarchy-private.h"

#include <math.h>

/**
 * OmarchyConfirmDialog:
 *
 * A destructive confirmation drawn over whatever it's placed on (put it in a #GtkOverlay over
 * the view it guards, and show it): a scrim, then a card `space(370)` wide with the message in
 * `title`, and Cancel and Confirm (urgent) buttons. Left/Right/h/l/Tab move between them, Return
 * runs the one selected, Escape cancels; hovering selects. ::canceled or ::confirmed.
 */
struct _OmarchyConfirmDialog {
  GtkWidget parent_instance;
  char *message;
  char *cancel_text, *confirm_text;
  int selected;
};

enum { PROP_0, PROP_MESSAGE, PROP_CANCEL_TEXT, PROP_CONFIRM_TEXT, PROP_SELECTED_INDEX, N_PROPS };
static GParamSpec *props[N_PROPS];
enum { CANCELED, CONFIRMED, N_SIGNALS };
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyConfirmDialog, omarchy_confirm_dialog, GTK_TYPE_WIDGET)

typedef struct {
  graphene_rect_t card, buttons[2];
  float inset[4];
  int message_w;
} Geo;

static const char *
cancel_text (OmarchyConfirmDialog *self)
{
  return self->cancel_text ? self->cancel_text : omarchy_t ("common.cancel", "Cancel");
}

static const char *
confirm_text (OmarchyConfirmDialog *self)
{
  return self->confirm_text ? self->confirm_text : omarchy_t ("common.confirm", "Confirm");
}

static PangoLayout *
message_layout (OmarchyConfirmDialog *self, int width)
{
  PangoLayout *l = omarchy_layout (GTK_WIDGET (self), self->message, omarchy_tokens ()->title, FALSE);
  pango_layout_set_width (l, MAX (1, width) * PANGO_SCALE);
  pango_layout_set_wrap (l, PANGO_WRAP_WORD_CHAR);
  return l;
}

static int
layout_height (PangoLayout *l, int line)
{
  if (pango_layout_get_line_count (l) <= 1)
    return line;
  PangoRectangle r;
  pango_layout_get_extents (l, NULL, &r);
  return (int) ceil (r.height / (double) PANGO_SCALE);
}

static Geo
geo (OmarchyConfirmDialog *self, float W, float H)
{
  const OmarchyTokens *t = omarchy_tokens ();
  Geo g;
  float bw = t->normal.border.widths[0];
  float pad = omarchy_space (18);
  for (int i = 0; i < 4; i++)
    g.inset[i] = bw + pad;
  float cw = MIN (W - omarchy_space (32), omarchy_space (370));
  g.message_w = (int) (cw - g.inset[1] - g.inset[3]);
  g_autoptr (PangoLayout) l = message_layout (self, g.message_w);
  int mh = layout_height (l, omarchy_line_height (t->title));
  float ch = g.inset[0] + g.inset[2] + mh + omarchy_space (20) + omarchy_space (34);
  g.card = GRAPHENE_RECT_INIT (roundf ((W - cw) / 2), roundf ((H - ch) / 2), cw, ch);
  float bwid = omarchy_space (88), bh = omarchy_space (34), gap = omarchy_space (10);
  float right = g.card.origin.x + cw - g.inset[1], bottom = g.card.origin.y + ch - g.inset[2];
  gboolean rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;
  if (rtl) {
    float left = g.card.origin.x + g.inset[3];
    g.buttons[1] = GRAPHENE_RECT_INIT (left, bottom - bh, bwid, bh);
    g.buttons[0] = GRAPHENE_RECT_INIT (left + bwid + gap, bottom - bh, bwid, bh);
  } else {
    g.buttons[1] = GRAPHENE_RECT_INIT (right - bwid, bottom - bh, bwid, bh);
    g.buttons[0] = GRAPHENE_RECT_INIT (right - 2 * bwid - gap, bottom - bh, bwid, bh);
  }
  return g;
}

static void
measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyConfirmDialog *self = OMARCHY_CONFIRM_DIALOG (w);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    /* It fills its host, as Qt's does: ask for little. */
    *min = *nat = omarchy_space (200);
  } else {
    float width = for_size >= 0 ? for_size : omarchy_space (370) + omarchy_space (32);
    Geo g = geo (self, width, 0);
    *min = *nat = (int) ceil (g.card.size.height) + omarchy_space (32);
  }
}

static void
snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyConfirmDialog *self = OMARCHY_CONFIRM_DIALOG (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  GdkRGBA scrim = omarchy_rgba_alpha (&p->background, 0.7 * p->background.alpha);
  gtk_snapshot_append_color (s, &scrim, &GRAPHENE_RECT_INIT (0, 0, W, H));
  Geo g = geo (self, W, H);
  OmarchyBorder cb = omarchy_border_flat (&p->accent, p->normal.border.widths[0]);
  omarchy_draw_surface (s, &g.card, p->radius, &p->background, &cb);
  g_autoptr (PangoLayout) l = message_layout (self, g.message_w);
  if (gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL)
    pango_layout_set_alignment (l, PANGO_ALIGN_RIGHT);
  omarchy_draw_layout (s, l, g.card.origin.x + g.inset[3], g.card.origin.y + g.inset[0], &p->foreground);
  for (int i = 0; i < 2; i++) {
    gboolean sel = self->selected == i, destructive = i == 1;
    GdkRGBA fill = { 0 };
    if (sel)
      fill = destructive ? omarchy_rgba_alpha (&p->urgent, 0.22) : omarchy_rgba_alpha (&p->foreground, 0.08);
    GdkRGBA bc = destructive ? (sel ? p->urgent : omarchy_rgba_alpha (&p->urgent, 0.56))
                             : (sel ? p->accent : omarchy_rgba_alpha (&p->foreground, 0.38));
    OmarchyBorder b = omarchy_border_flat (&bc, p->normal.border.widths[0]);
    omarchy_draw_surface (s, &g.buttons[i], p->radius, &fill, &b);
    GdkRGBA ink = destructive ? (sel ? p->urgent : p->foreground) : (sel ? p->accent : p->foreground);
    g_autoptr (PangoLayout) bl = omarchy_layout (w, i == 0 ? cancel_text (self) : confirm_text (self), p->caption, FALSE);
    int tw;
    omarchy_layout_size (bl, &tw, NULL);
    int th = omarchy_line_height (p->caption);
    omarchy_draw_layout (s, bl, g.buttons[i].origin.x + roundf ((g.buttons[i].size.width - tw) / 2),
                         g.buttons[i].origin.y + roundf ((g.buttons[i].size.height - th) / 2), &ink);
  }
}

static int
button_at (OmarchyConfirmDialog *self, double x, double y)
{
  Geo g = geo (self, gtk_widget_get_width (GTK_WIDGET (self)), gtk_widget_get_height (GTK_WIDGET (self)));
  for (int i = 0; i < 2; i++)
    if (graphene_rect_contains_point (&g.buttons[i], &GRAPHENE_POINT_INIT (x, y)))
      return i;
  if (!graphene_rect_contains_point (&g.card, &GRAPHENE_POINT_INIT (x, y)))
    return -2; /* the scrim */
  return -1;
}

static void
clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyConfirmDialog *self = OMARCHY_CONFIRM_DIALOG (w);
  int i = button_at (self, x, y);
  if (i == 1)
    g_signal_emit (self, signals[CONFIRMED], 0);
  else if (i == 0 || i == -2)
    g_signal_emit (self, signals[CANCELED], 0);
}

static void
motion (GtkEventControllerMotion *c, double x, double y, OmarchyConfirmDialog *self)
{
  int i = button_at (self, x, y);
  if (i >= 0)
    omarchy_confirm_dialog_set_selected_index (self, i);
}

static gboolean
key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyConfirmDialog *self = OMARCHY_CONFIRM_DIALOG (w);
  switch (keyval) {
  case GDK_KEY_Escape:
    g_signal_emit (self, signals[CANCELED], 0);
    return TRUE;
  case GDK_KEY_Left: case GDK_KEY_Right: case GDK_KEY_h: case GDK_KEY_l: case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab:
    omarchy_confirm_dialog_set_selected_index (self, self->selected == 0 ? 1 : 0);
    return TRUE;
  case GDK_KEY_Return: case GDK_KEY_KP_Enter:
    g_signal_emit (self, self->selected == 0 ? signals[CANCELED] : signals[CONFIRMED], 0);
    return TRUE;
  default:
    return FALSE;
  }
}

static void
omarchy_confirm_dialog_init (OmarchyConfirmDialog *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->message = g_strdup ("");
  self->selected = 1;
  gtk_widget_set_focusable (w, TRUE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, clicked);
  gtk_widget_set_cursor_from_name (w, NULL);
  omarchy_widget_add_keys (w, key);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "motion", G_CALLBACK (motion), self);
  gtk_widget_add_controller (w, m);
}

static void
finalize (GObject *o)
{
  OmarchyConfirmDialog *self = OMARCHY_CONFIRM_DIALOG (o);
  g_free (self->message);
  g_free (self->cancel_text);
  g_free (self->confirm_text);
  G_OBJECT_CLASS (omarchy_confirm_dialog_parent_class)->finalize (o);
}

static void
set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyConfirmDialog *self = OMARCHY_CONFIRM_DIALOG (o);
  switch (id) {
  case PROP_MESSAGE: omarchy_confirm_dialog_set_message (self, g_value_get_string (v)); break;
  case PROP_CANCEL_TEXT: omarchy_confirm_dialog_set_cancel_text (self, g_value_get_string (v)); break;
  case PROP_CONFIRM_TEXT: omarchy_confirm_dialog_set_confirm_text (self, g_value_get_string (v)); break;
  case PROP_SELECTED_INDEX: omarchy_confirm_dialog_set_selected_index (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyConfirmDialog *self = OMARCHY_CONFIRM_DIALOG (o);
  switch (id) {
  case PROP_MESSAGE: g_value_set_string (v, self->message); break;
  case PROP_CANCEL_TEXT: g_value_set_string (v, cancel_text (self)); break;
  case PROP_CONFIRM_TEXT: g_value_set_string (v, confirm_text (self)); break;
  case PROP_SELECTED_INDEX: g_value_set_int (v, self->selected); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_confirm_dialog_class_init (OmarchyConfirmDialogClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = finalize;
  oc->set_property = set_property;
  oc->get_property = get_property;
  wc->measure = measure;
  wc->snapshot = snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  props[PROP_MESSAGE] = g_param_spec_string ("message", NULL, NULL, "", f);
  /**
   * OmarchyConfirmDialog:cancel-text:
   *
   * Defaults to the catalog's `common.cancel` in the display language.
   */
  props[PROP_CANCEL_TEXT] = g_param_spec_string ("cancel-text", NULL, NULL, NULL, f);
  /**
   * OmarchyConfirmDialog:confirm-text:
   *
   * Defaults to the catalog's `common.confirm` in the display language.
   */
  props[PROP_CONFIRM_TEXT] = g_param_spec_string ("confirm-text", NULL, NULL, NULL, f);
  props[PROP_SELECTED_INDEX] = g_param_spec_int ("selected-index", NULL, NULL, 0, 1, 1, f);
  g_object_class_install_properties (oc, N_PROPS, props);
  signals[CANCELED] = g_signal_new ("canceled", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);
  signals[CONFIRMED] = g_signal_new ("confirmed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                     G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-confirm-dialog");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_ALERT_DIALOG);
}

/**
 * omarchy_confirm_dialog_new:
 * @message: (nullable): the question
 *
 * Returns: (transfer floating): a confirmation, Confirm selected
 */
GtkWidget *
omarchy_confirm_dialog_new (const char *message)
{
  return g_object_new (OMARCHY_TYPE_CONFIRM_DIALOG, "message", message ? message : "", NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyConfirmDialog, confirm_dialog, message, props, PROP_MESSAGE, TRUE)

void
omarchy_confirm_dialog_set_cancel_text (OmarchyConfirmDialog *self, const char *text)
{
  g_free (self->cancel_text);
  self->cancel_text = g_strdup (text);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
omarchy_confirm_dialog_set_confirm_text (OmarchyConfirmDialog *self, const char *text)
{
  g_free (self->confirm_text);
  self->confirm_text = g_strdup (text);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

int
omarchy_confirm_dialog_get_selected_index (OmarchyConfirmDialog *self)
{
  return self->selected;
}

void
omarchy_confirm_dialog_set_selected_index (OmarchyConfirmDialog *self, int index)
{
  index = CLAMP (index, 0, 1);
  if (index == self->selected)
    return;
  self->selected = index;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_INDEX]);
}
