/* omarchy-forms-dialog.c: Dialog (Dialog.qml): a scrim over its host and a card with a title, a
 * message, any body, and a secondary and a primary button. */
#include "omarchy-forms-private.h"

#include <math.h>

/**
 * OmarchyDialog:
 *
 * A modal drawn over whatever it covers: put it in a #GtkOverlay over the view it guards and
 * set #OmarchyDialog:opened. The `scrim` fills the host; the card is centered,
 * `min(host − space(32), space(370))` wide, `space(18)` padding inside a `menu.selectedText`
 * border, on `palette.background`: the title (`title`, bold), the message (`body`) and the body
 * (children added with [method@Omarchy.Dialog.append]) `space(10)` apart, then `space(20)`
 * below, the secondary and primary buttons (`space(88)` × `space(34)`, square) at the bottom
 * right. The selected button is filled with `menu.selectedBackground` and drawn in
 * `menu.selectedText`; a destructive primary is urgent instead.
 *
 * Escape rejects, Return takes the selected button (the primary by default), Left/Right/h/l/Tab
 * move the selection, hovering a button selects it, a press on the scrim rejects. ::rejected
 * or ::accepted; the app closes it.
 */
struct _OmarchyDialog {
  GtkWidget parent_instance;
  GtkWidget *body;
  char *title, *message, *secondary_text, *primary_text;
  gboolean opened, destructive;
  int selected;
};

enum {
  PROP_0, PROP_OPENED, PROP_TITLE, PROP_MESSAGE, PROP_SECONDARY_TEXT, PROP_PRIMARY_TEXT, PROP_DESTRUCTIVE,
  PROP_SELECTED_INDEX, N_PROPS
};
static GParamSpec *props[N_PROPS];
enum { REJECTED, ACCEPTED, N_SIGNALS };
static guint signals[N_SIGNALS];

static void dialog_buildable_init (GtkBuildableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (OmarchyDialog, omarchy_dialog, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, dialog_buildable_init))

static GtkBuildableIface *parent_buildable;

typedef struct {
  graphene_rect_t card, buttons[2], body;
  float inset[4];
  int content_w, title_h, message_h, body_h;
  float title_y, message_y;
} Geo;

static const char *
secondary_text (OmarchyDialog *self)
{
  return self->secondary_text ? self->secondary_text : omarchy_t ("common.cancel", "Cancel");
}

static const char *
primary_text (OmarchyDialog *self)
{
  return self->primary_text ? self->primary_text : omarchy_t ("common.confirm", "Confirm");
}

static PangoLayout *
wrapped (OmarchyDialog *self, const char *text, int px, gboolean bold, int width)
{
  PangoLayout *l = omarchy_layout (GTK_WIDGET (self), text, px, bold);
  pango_layout_set_width (l, MAX (1, width) * PANGO_SCALE);
  pango_layout_set_wrap (l, PANGO_WRAP_WORD_CHAR);
  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    pango_layout_set_alignment (l, PANGO_ALIGN_RIGHT);
  return l;
}

static gboolean
body_visible (OmarchyDialog *self)
{
  for (GtkWidget *c = gtk_widget_get_first_child (self->body); c; c = gtk_widget_get_next_sibling (c))
    if (gtk_widget_get_visible (c))
      return TRUE;
  return FALSE;
}

static float
dialog_button_width (GtkWidget *w, const char *text)
{
  const OmarchyTokens *t = omarchy_tokens ();
  g_autoptr (PangoLayout) l = omarchy_layout (w, text, t->caption, FALSE);
  PangoRectangle logical;
  pango_layout_get_extents (l, NULL, &logical);
  float tw = ceilf (logical.width / (float) PANGO_SCALE);
  return MAX ((float) omarchy_space (88), tw + t->control_padding_x * 2);
}

static Geo
geo (OmarchyDialog *self, float W, float H)
{
  const OmarchyTokens *t = omarchy_tokens ();
  Geo g = { 0 };
  float bw = t->normal.border.widths[0], pad = omarchy_space (18), gap = omarchy_space (10);
  for (int i = 0; i < 4; i++)
    g.inset[i] = bw + pad;
  float cw = MIN (W - omarchy_space (32), omarchy_space (370));
  g.content_w = MAX (1, (int) (cw - g.inset[1] - g.inset[3]));
  float y = 0;
  int items = 0;
  if (self->title && *self->title) {
    g_autoptr (PangoLayout) l = wrapped (self, self->title, t->title, TRUE, g.content_w);
    g.title_h = omarchy_forms_text_height (l, t->title);
    g.title_y = y;
    y += g.title_h;
    items++;
  }
  if (self->message && *self->message) {
    g_autoptr (PangoLayout) l = wrapped (self, self->message, t->body, FALSE, g.content_w);
    g.message_h = omarchy_forms_text_height (l, t->body);
    y += items ? gap : 0;
    g.message_y = y;
    y += g.message_h;
    items++;
  }
  float body_y = y;
  if (body_visible (self)) {
    gtk_widget_measure (self->body, GTK_ORIENTATION_VERTICAL, g.content_w, NULL, &g.body_h, NULL, NULL);
    body_y = y + (items ? gap : 0);
    y = body_y + g.body_h;
  }
  float ch = g.inset[0] + g.inset[2] + y + omarchy_space (20) + omarchy_space (34);
  /* Qt's centerIn lands a half pixel up. */
  g.card = GRAPHENE_RECT_INIT (floorf ((W - cw) / 2), floorf ((H - ch) / 2), cw, ch);
  g.title_y += g.card.origin.y + g.inset[0];
  g.message_y += g.card.origin.y + g.inset[0];
  g.body = GRAPHENE_RECT_INIT (g.card.origin.x + g.inset[3], g.card.origin.y + g.inset[0] + body_y, g.content_w, g.body_h);
  float bw0 = dialog_button_width (GTK_WIDGET (self), secondary_text (self));
  float bw1 = dialog_button_width (GTK_WIDGET (self), primary_text (self));
  float bh = omarchy_space (34);
  float right = g.card.origin.x + cw - g.inset[1], bottom = g.card.origin.y + ch - g.inset[2];
  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL) {
    float left = g.card.origin.x + g.inset[3];
    g.buttons[1] = GRAPHENE_RECT_INIT (left, bottom - bh, bw1, bh);
    g.buttons[0] = GRAPHENE_RECT_INIT (left + bw1 + gap, bottom - bh, bw0, bh);
  } else {
    g.buttons[1] = GRAPHENE_RECT_INIT (right - bw1, bottom - bh, bw1, bh);
    g.buttons[0] = GRAPHENE_RECT_INIT (right - bw1 - gap - bw0, bottom - bh, bw0, bh);
  }
  return g;
}

static void
dialog_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyDialog *self = OMARCHY_DIALOG (w);
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
dialog_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyDialog *self = OMARCHY_DIALOG (w);
  Geo g = geo (self, width, height);
  gtk_widget_size_allocate (self->body,
                            &(GtkAllocation) { (int) g.body.origin.x, (int) g.body.origin.y, (int) g.body.size.width,
                                               (int) g.body.size.height },
                            -1);
}

static void
dialog_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyDialog *self = OMARCHY_DIALOG (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gtk_snapshot_append_color (s, &p->scrim, &GRAPHENE_RECT_INIT (0, 0, W, H));
  Geo g = geo (self, W, H);
  float bw = p->normal.border.widths[0];
  OmarchyBorder cb = omarchy_border_flat (&p->menu_selected_text, bw);
  omarchy_draw_surface (s, &g.card, p->radius, &p->background, &cb);
  float x = g.card.origin.x + g.inset[3];
  if (self->title && *self->title) {
    g_autoptr (PangoLayout) l = wrapped (self, self->title, p->title, TRUE, g.content_w);
    omarchy_draw_layout (s, l, x, g.title_y, &p->foreground);
  }
  if (self->message && *self->message) {
    g_autoptr (PangoLayout) l = wrapped (self, self->message, p->body, FALSE, g.content_w);
    omarchy_draw_layout (s, l, x, g.message_y, &p->foreground);
  }
  gtk_widget_snapshot_child (w, self->body, s);
  for (int i = 0; i < 2; i++) {
    gboolean sel = self->selected == i, urgent = i == 1 && self->destructive;
    GdkRGBA fill = { 0 };
    if (sel)
      fill = urgent ? omarchy_rgba_alpha (&p->urgent, 0.22) : p->menu_selected_background;
    GdkRGBA bc = urgent ? (sel ? p->urgent : omarchy_rgba_alpha (&p->urgent, 0.56))
                        : (sel ? p->menu_selected_text : omarchy_rgba_alpha (&p->foreground, 0.38));
    OmarchyBorder b = omarchy_border_flat (&bc, bw);
    omarchy_draw_surface (s, &g.buttons[i], p->radius, &fill, &b);
    const GdkRGBA *ink = urgent ? (sel ? &p->urgent : &p->foreground) : (sel ? &p->menu_selected_text : &p->foreground);
    g_autoptr (PangoLayout) bl = omarchy_layout (w, i == 0 ? secondary_text (self) : primary_text (self), p->caption, FALSE);
    int tw;
    omarchy_layout_size (bl, &tw, NULL);
    int th = omarchy_line_height (p->caption);
    omarchy_draw_layout (s, bl, g.buttons[i].origin.x + roundf ((g.buttons[i].size.width - tw) / 2),
                         g.buttons[i].origin.y + roundf ((g.buttons[i].size.height - th) / 2), ink);
  }
}

/* A button's index, -1 on the card, -2 on the scrim. */
static int
dialog_hit (OmarchyDialog *self, double x, double y)
{
  Geo g = geo (self, gtk_widget_get_width (GTK_WIDGET (self)), gtk_widget_get_height (GTK_WIDGET (self)));
  graphene_point_t pt = GRAPHENE_POINT_INIT (x, y);
  for (int i = 0; i < 2; i++)
    if (graphene_rect_contains_point (&g.buttons[i], &pt))
      return i;
  return graphene_rect_contains_point (&g.card, &pt) ? -1 : -2;
}

static void
dialog_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyDialog *self = OMARCHY_DIALOG (w);
  if (button != GDK_BUTTON_PRIMARY || !self->opened)
    return;
  int i = dialog_hit (self, x, y);
  if (i == 1)
    g_signal_emit (self, signals[ACCEPTED], 0);
  else if (i == 0 || i == -2)
    g_signal_emit (self, signals[REJECTED], 0);
}

static void
dialog_motion (GtkEventControllerMotion *c, double x, double y, OmarchyDialog *self)
{
  int i = dialog_hit (self, x, y);
  if (i >= 0)
    omarchy_dialog_set_selected_index (self, i);
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), i >= 0 ? "pointer" : NULL);
}

/**
 * omarchy_dialog_handle_key:
 * @self: the dialog
 * @keyval: the key
 * @state: its modifiers
 *
 * Run a key as the open dialog would (for a panel whose own key handler has the keys):
 * Escape rejects, Return takes the selected button, Left/Right/h/l/Tab move the selection.
 *
 * Returns: %TRUE when the key meant something
 */
gboolean
omarchy_dialog_handle_key (OmarchyDialog *self, guint keyval, GdkModifierType state)
{
  if (!self->opened)
    return FALSE;
  switch (keyval) {
  case GDK_KEY_Escape:
    g_signal_emit (self, signals[REJECTED], 0);
    return TRUE;
  case GDK_KEY_Left: case GDK_KEY_Right: case GDK_KEY_KP_Left: case GDK_KEY_KP_Right: case GDK_KEY_h: case GDK_KEY_l:
  case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: case GDK_KEY_KP_Tab:
    omarchy_dialog_set_selected_index (self, self->selected == 0 ? 1 : 0);
    return TRUE;
  case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_ISO_Enter:
    g_signal_emit (self, self->selected == 0 ? signals[REJECTED] : signals[ACCEPTED], 0);
    return TRUE;
  default:
    return FALSE;
  }
}

static gboolean
dialog_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  return omarchy_dialog_handle_key (OMARCHY_DIALOG (w), keyval, state);
}

static void dialog_restyle (GtkWidget *w);

static void
omarchy_dialog_init (OmarchyDialog *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->title = g_strdup ("");
  self->message = g_strdup ("");
  self->selected = 1;
  self->body = gtk_box_new (GTK_ORIENTATION_VERTICAL, omarchy_space (10));
  gtk_widget_set_parent (self->body, w);
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (dialog_restyle), w, G_CONNECT_SWAPPED);
  gtk_widget_set_visible (w, FALSE);
  gtk_widget_set_focusable (w, TRUE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, dialog_clicked);
  gtk_widget_set_cursor_from_name (w, NULL);
  omarchy_widget_add_keys (w, dialog_key);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "motion", G_CALLBACK (dialog_motion), self);
  gtk_widget_add_controller (w, m);
}

static void
dialog_dispose (GObject *o)
{
  OmarchyDialog *self = OMARCHY_DIALOG (o);
  g_clear_pointer (&self->body, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_dialog_parent_class)->dispose (o);
}

static void
dialog_finalize (GObject *o)
{
  OmarchyDialog *self = OMARCHY_DIALOG (o);
  g_free (self->title);
  g_free (self->message);
  g_free (self->secondary_text);
  g_free (self->primary_text);
  G_OBJECT_CLASS (omarchy_dialog_parent_class)->finalize (o);
}

static void
dialog_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyDialog *self = OMARCHY_DIALOG (o);
  switch (id) {
  case PROP_OPENED: omarchy_dialog_set_opened (self, g_value_get_boolean (v)); break;
  case PROP_TITLE: omarchy_dialog_set_title (self, g_value_get_string (v)); break;
  case PROP_MESSAGE: omarchy_dialog_set_message (self, g_value_get_string (v)); break;
  case PROP_SECONDARY_TEXT: omarchy_dialog_set_secondary_text (self, g_value_get_string (v)); break;
  case PROP_PRIMARY_TEXT: omarchy_dialog_set_primary_text (self, g_value_get_string (v)); break;
  case PROP_DESTRUCTIVE: omarchy_dialog_set_destructive (self, g_value_get_boolean (v)); break;
  case PROP_SELECTED_INDEX: omarchy_dialog_set_selected_index (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
dialog_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyDialog *self = OMARCHY_DIALOG (o);
  switch (id) {
  case PROP_OPENED: g_value_set_boolean (v, self->opened); break;
  case PROP_TITLE: g_value_set_string (v, self->title); break;
  case PROP_MESSAGE: g_value_set_string (v, self->message); break;
  case PROP_SECONDARY_TEXT: g_value_set_string (v, secondary_text (self)); break;
  case PROP_PRIMARY_TEXT: g_value_set_string (v, primary_text (self)); break;
  case PROP_DESTRUCTIVE: g_value_set_boolean (v, self->destructive); break;
  case PROP_SELECTED_INDEX: g_value_set_int (v, self->selected); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
dialog_restyle (GtkWidget *w)
{
  gtk_box_set_spacing (GTK_BOX (OMARCHY_DIALOG (w)->body), omarchy_space (10));
}

static void
omarchy_dialog_class_init (OmarchyDialogClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = dialog_dispose;
  oc->finalize = dialog_finalize;
  oc->set_property = dialog_set_property;
  oc->get_property = dialog_get_property;
  wc->measure = dialog_measure;
  wc->size_allocate = dialog_allocate;
  wc->snapshot = dialog_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyDialog:opened:
   *
   * Shown (and holding the keys). A new dialog is closed.
   */
  props[PROP_OPENED] = g_param_spec_boolean ("opened", NULL, NULL, FALSE, f);
  props[PROP_TITLE] = g_param_spec_string ("title", NULL, NULL, "", f);
  props[PROP_MESSAGE] = g_param_spec_string ("message", NULL, NULL, "", f);
  /**
   * OmarchyDialog:secondary-text:
   *
   * The secondary button; defaults to the catalog's `common.cancel` in the display language.
   */
  props[PROP_SECONDARY_TEXT] = g_param_spec_string ("secondary-text", NULL, NULL, NULL, f);
  /**
   * OmarchyDialog:primary-text:
   *
   * The primary button; defaults to the catalog's `common.confirm` in the display language.
   */
  props[PROP_PRIMARY_TEXT] = g_param_spec_string ("primary-text", NULL, NULL, NULL, f);
  /**
   * OmarchyDialog:destructive:
   *
   * The primary button is urgent.
   */
  props[PROP_DESTRUCTIVE] = g_param_spec_boolean ("destructive", NULL, NULL, FALSE, f);
  /**
   * OmarchyDialog:selected-index:
   *
   * 0 for the secondary button, 1 for the primary (Return takes it).
   */
  props[PROP_SELECTED_INDEX] = g_param_spec_int ("selected-index", NULL, NULL, 0, 1, 1, f);
  g_object_class_install_properties (oc, N_PROPS, props);
  /**
   * OmarchyDialog::rejected:
   *
   * Escape, the secondary button, or a press on the scrim.
   */
  signals[REJECTED] = g_signal_new ("rejected", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);
  /**
   * OmarchyDialog::accepted:
   *
   * The primary button, or Return with it selected.
   */
  signals[ACCEPTED] = g_signal_new ("accepted", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-dialog");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_DIALOG);
}

static void
dialog_add_child (GtkBuildable *b, GtkBuilder *builder, GObject *child, const char *type)
{
  if (GTK_IS_WIDGET (child))
    omarchy_dialog_append (OMARCHY_DIALOG (b), GTK_WIDGET (child));
  else
    parent_buildable->add_child (b, builder, child, type);
}

static void
dialog_buildable_init (GtkBuildableIface *iface)
{
  parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = dialog_add_child;
}

/**
 * omarchy_dialog_new:
 * @title: (nullable): the title
 * @message: (nullable): the message under it
 *
 * Returns: (transfer floating): a dialog, closed, the primary button selected
 */
GtkWidget *
omarchy_dialog_new (const char *title, const char *message)
{
  return g_object_new (OMARCHY_TYPE_DIALOG, "title", title ? title : "", "message", message ? message : "", NULL);
}

gboolean
omarchy_dialog_get_opened (OmarchyDialog *self)
{
  return self->opened;
}

void
omarchy_dialog_set_opened (OmarchyDialog *self, gboolean opened)
{
  if (self->opened == !!opened)
    return;
  self->opened = !!opened;
  gtk_widget_set_visible (GTK_WIDGET (self), self->opened);
  if (self->opened)
    gtk_widget_grab_focus (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_OPENED]);
}

OMARCHY_STRING_ACCESSORS (OmarchyDialog, dialog, title, props, PROP_TITLE, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyDialog, dialog, message, props, PROP_MESSAGE, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyDialog, dialog, destructive, props, PROP_DESTRUCTIVE, FALSE)

const char *
omarchy_dialog_get_secondary_text (OmarchyDialog *self)
{
  return secondary_text (self);
}

/**
 * omarchy_dialog_set_secondary_text:
 * @self: the dialog
 * @text: (nullable): the label, or %NULL for the translated "Cancel"
 */
void
omarchy_dialog_set_secondary_text (OmarchyDialog *self, const char *text)
{
  g_free (self->secondary_text);
  self->secondary_text = g_strdup (text);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SECONDARY_TEXT]);
}

const char *
omarchy_dialog_get_primary_text (OmarchyDialog *self)
{
  return primary_text (self);
}

/**
 * omarchy_dialog_set_primary_text:
 * @self: the dialog
 * @text: (nullable): the label, or %NULL for the translated "Confirm"
 */
void
omarchy_dialog_set_primary_text (OmarchyDialog *self, const char *text)
{
  g_free (self->primary_text);
  self->primary_text = g_strdup (text);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRIMARY_TEXT]);
}

int
omarchy_dialog_get_selected_index (OmarchyDialog *self)
{
  return self->selected;
}

void
omarchy_dialog_set_selected_index (OmarchyDialog *self, int index)
{
  index = CLAMP (index, 0, 1);
  if (index == self->selected)
    return;
  self->selected = index;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_INDEX]);
}

/**
 * omarchy_dialog_append:
 * @self: the dialog
 * @child: (transfer floating): a widget for the body, under the message
 */
void
omarchy_dialog_append (OmarchyDialog *self, GtkWidget *child)
{
  gtk_box_append (GTK_BOX (self->body), child);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
omarchy_dialog_remove (OmarchyDialog *self, GtkWidget *child)
{
  gtk_box_remove (GTK_BOX (self->body), child);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}
