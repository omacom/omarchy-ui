/* omarchy-feedback-messages.c: EmptyState (EmptyState.qml), Banner (Banner.qml) and Toast
 * (Toast.qml), drawn as the Qt kit draws them, built from the kit's labels and buttons. */
#include "omarchy-private.h"

#include <math.h>
#include <string.h>

/* An OmarchyLabel at a token size, in a role, optionally wrapping and centered. */
static GtkWidget *
text_label (const char *size, const char *role, gboolean wrap)
{
  GtkWidget *l = omarchy_label_new ("");
  omarchy_label_set_size (OMARCHY_LABEL (l), size);
  omarchy_label_set_role (OMARCHY_LABEL (l), role);
  omarchy_label_set_wrap (OMARCHY_LABEL (l), wrap);
  return l;
}

/* A Nerd Font glyph boxed as Qt's Text boxes one: the advance widened by ink that overhangs it,
 * the font's line height tall (OmarchyLabel boxes text by its logical width). */
#define FEEDBACK_TYPE_GLYPH (feedback_glyph_get_type ())
G_DECLARE_FINAL_TYPE (FeedbackGlyph, feedback_glyph, FEEDBACK, GLYPH, GtkWidget)

struct _FeedbackGlyph {
  GtkWidget parent_instance;
  char *text;
  const char *size; /* a static token name */
  const char *role;
  gboolean has_color;
  GdkRGBA color;
};

G_DEFINE_FINAL_TYPE (FeedbackGlyph, feedback_glyph, GTK_TYPE_WIDGET)

static int
glyph_px (FeedbackGlyph *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  if (!g_strcmp0 (self->size, "iconLarge"))
    return t->icon_large;
  if (!g_strcmp0 (self->size, "displayLarge"))
    return t->display_large;
  return t->icon;
}

static void
glyph_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  FeedbackGlyph *self = FEEDBACK_GLYPH (w);
  int px = glyph_px (self);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->text, px, FALSE);
    int gw;
    omarchy_glyph_box (l, &gw, NULL);
    *min = *nat = gw;
  } else {
    *min = *nat = omarchy_line_height (px);
  }
}

static void
glyph_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  FeedbackGlyph *self = FEEDBACK_GLYPH (w);
  if (!self->text || !*self->text)
    return;
  g_autoptr (PangoLayout) l = omarchy_layout (w, self->text, glyph_px (self), FALSE);
  int gw;
  int dx = omarchy_glyph_box (l, &gw, NULL);
  GdkRGBA c = self->has_color ? self->color : omarchy_role_color (self->role);
  omarchy_draw_layout (s, l, roundf ((gtk_widget_get_width (w) - gw) / 2.0f) + dx, 0, &c);
}

static void
feedback_glyph_init (FeedbackGlyph *self)
{
  self->text = g_strdup ("");
  self->size = "icon";
  self->role = "foreground";
  omarchy_widget_follow (GTK_WIDGET (self));
  gtk_accessible_update_state (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
}

static void
glyph_finalize (GObject *o)
{
  g_free (FEEDBACK_GLYPH (o)->text);
  G_OBJECT_CLASS (feedback_glyph_parent_class)->finalize (o);
}

static void
feedback_glyph_class_init (FeedbackGlyphClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = glyph_finalize;
  GTK_WIDGET_CLASS (klass)->measure = glyph_measure;
  GTK_WIDGET_CLASS (klass)->snapshot = glyph_snapshot;
  gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "omarchy-glyph");
  gtk_widget_class_set_accessible_role (GTK_WIDGET_CLASS (klass), GTK_ACCESSIBLE_ROLE_PRESENTATION);
}

static GtkWidget *
glyph_new (const char *size, const char *role)
{
  FeedbackGlyph *g = g_object_new (FEEDBACK_TYPE_GLYPH, NULL);
  g->size = size;
  g->role = role;
  return GTK_WIDGET (g);
}

static const char *
glyph_get_text (GtkWidget *w)
{
  return FEEDBACK_GLYPH (w)->text;
}

static void
glyph_set_text (GtkWidget *w, const char *text)
{
  FeedbackGlyph *self = FEEDBACK_GLYPH (w);
  g_free (self->text);
  self->text = g_strdup (text ? text : "");
  gtk_widget_set_visible (w, text && *text);
  gtk_widget_queue_resize (w);
}

/* size and role: static strings */
static void
glyph_set_look (GtkWidget *w, const char *size, const char *role)
{
  FeedbackGlyph *self = FEEDBACK_GLYPH (w);
  self->size = size;
  self->role = role;
  gtk_widget_queue_resize (w);
}

static void
glyph_set_color (GtkWidget *w, const GdkRGBA *color)
{
  FeedbackGlyph *self = FEEDBACK_GLYPH (w);
  self->has_color = color != NULL;
  if (color)
    self->color = *color;
  gtk_widget_queue_draw (w);
}

/* Recolor src's rgb onto base when src was resolved against the foreground (a state fill
 * "for" another text color, as Style.hoverFillFor(text, accent) does); else src as it is. */
static GdkRGBA
state_for (const GdkRGBA *src, const GdkRGBA *base)
{
  const OmarchyTokens *p = omarchy_paint ();
  const GdkRGBA *fg = &p->foreground;
  if (fabsf (src->red - fg->red) < 0.002f && fabsf (src->green - fg->green) < 0.002f
      && fabsf (src->blue - fg->blue) < 0.002f)
    return (GdkRGBA) { base->red, base->green, base->blue, src->alpha };
  return *src;
}

/* ============================================================================ EmptyState === */

/**
 * OmarchyEmptyState:
 *
 * What a list or panel shows when it has nothing: a centered column of a glyph, a title, an
 * optional hint and an optional bordered action button, in the width it is given (title and
 * hint wrap). #OmarchyEmptyState:size "panel" (the default) is a shell panel's quiet line:
 * `iconLarge` glyph and `body` title in textDim, `bodySmall` hint in textFaint, `sm` apart.
 * "overlay" is what the clipboard and emoji pickers show: a `displayLarge` glyph in the menu's
 * selected text at 0.8 opacity, a `title` at 0.7, a `body` hint in textDim, `lg` apart.
 */
struct _OmarchyEmptyState {
  GtkWidget parent_instance;
  GtkWidget *box;
  GtkWidget *glyph, *title, *hint, *action;
  char *size;
  gboolean cursor;
};

enum { E_PROP_0, E_PROP_TITLE, E_PROP_GLYPH, E_PROP_HINT, E_PROP_ACTION_TEXT, E_PROP_SIZE, E_PROP_CURSOR, E_N_PROPS };
static GParamSpec *e_props[E_N_PROPS];
enum { E_ACTION_CLICKED, E_N_SIGNALS };
static guint e_signals[E_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyEmptyState, omarchy_empty_state, GTK_TYPE_WIDGET)

static gboolean
empty_overlay (OmarchyEmptyState *self)
{
  return !g_strcmp0 (self->size, "overlay");
}

static void
empty_repaint (OmarchyEmptyState *self)
{
  if (empty_overlay (self))
    glyph_set_color (self->glyph, &omarchy_paint ()->menu_selected_text);
  else
    glyph_set_color (self->glyph, NULL);
}

static void
empty_restyle (OmarchyEmptyState *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  gboolean overlay = empty_overlay (self);
  gtk_box_set_spacing (GTK_BOX (self->box), overlay ? t->lg : t->sm);
  glyph_set_look (self->glyph, overlay ? "displayLarge" : "iconLarge", "dim");
  gtk_widget_set_opacity (self->glyph, overlay ? 0.8 : 1);
  omarchy_label_set_size (OMARCHY_LABEL (self->title), overlay ? "title" : "body");
  omarchy_label_set_role (OMARCHY_LABEL (self->title), overlay ? "foreground" : "dim");
  gtk_widget_set_opacity (self->title, overlay ? 0.7 : 1);
  omarchy_label_set_size (OMARCHY_LABEL (self->hint), overlay ? "body" : "bodySmall");
  omarchy_label_set_role (OMARCHY_LABEL (self->hint), overlay ? "dim" : "faint");
  empty_repaint (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
empty_action_clicked (OmarchyEmptyState *self)
{
  g_signal_emit (self, e_signals[E_ACTION_CLICKED], 0);
}

static void
omarchy_empty_state_init (OmarchyEmptyState *self)
{
  self->size = g_strdup ("panel");
  self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_parent (self->box, GTK_WIDGET (self));

  self->glyph = glyph_new ("iconLarge", "dim");
  gtk_widget_set_halign (self->glyph, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->glyph, FALSE);
  self->title = text_label ("body", "dim", TRUE);
  omarchy_label_set_xalign (OMARCHY_LABEL (self->title), 0.5f);
  self->hint = text_label ("bodySmall", "faint", TRUE);
  omarchy_label_set_xalign (OMARCHY_LABEL (self->hint), 0.5f);
  gtk_widget_set_visible (self->hint, FALSE);
  self->action = omarchy_button_new ("");
  omarchy_button_set_bordered (OMARCHY_BUTTON (self->action), TRUE);
  gtk_widget_set_halign (self->action, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->action, FALSE);
  g_signal_connect_swapped (self->action, "clicked", G_CALLBACK (empty_action_clicked), self);

  gtk_box_append (GTK_BOX (self->box), self->glyph);
  gtk_box_append (GTK_BOX (self->box), self->title);
  gtk_box_append (GTK_BOX (self->box), self->hint);
  gtk_box_append (GTK_BOX (self->box), self->action);

  empty_restyle (self);
  OmarchyTheme *theme = omarchy_theme_get_default ();
  g_signal_connect_object (theme, "restyle", G_CALLBACK (empty_restyle), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (theme, "repaint", G_CALLBACK (empty_repaint), self, G_CONNECT_SWAPPED);
}

static void
empty_dispose (GObject *o)
{
  OmarchyEmptyState *self = OMARCHY_EMPTY_STATE (o);
  g_clear_pointer (&self->box, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_empty_state_parent_class)->dispose (o);
}

static void
empty_finalize (GObject *o)
{
  g_free (OMARCHY_EMPTY_STATE (o)->size);
  G_OBJECT_CLASS (omarchy_empty_state_parent_class)->finalize (o);
}

static void
empty_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyEmptyState *self = OMARCHY_EMPTY_STATE (o);
  switch (id) {
  case E_PROP_TITLE: omarchy_empty_state_set_title (self, g_value_get_string (v)); break;
  case E_PROP_GLYPH: omarchy_empty_state_set_glyph (self, g_value_get_string (v)); break;
  case E_PROP_HINT: omarchy_empty_state_set_hint (self, g_value_get_string (v)); break;
  case E_PROP_ACTION_TEXT: omarchy_empty_state_set_action_text (self, g_value_get_string (v)); break;
  case E_PROP_SIZE: omarchy_empty_state_set_size (self, g_value_get_string (v)); break;
  case E_PROP_CURSOR: omarchy_empty_state_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
empty_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyEmptyState *self = OMARCHY_EMPTY_STATE (o);
  switch (id) {
  case E_PROP_TITLE: g_value_set_string (v, omarchy_empty_state_get_title (self)); break;
  case E_PROP_GLYPH: g_value_set_string (v, omarchy_empty_state_get_glyph (self)); break;
  case E_PROP_HINT: g_value_set_string (v, omarchy_empty_state_get_hint (self)); break;
  case E_PROP_ACTION_TEXT: g_value_set_string (v, omarchy_empty_state_get_action_text (self)); break;
  case E_PROP_SIZE: g_value_set_string (v, self->size); break;
  case E_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_empty_state_class_init (OmarchyEmptyStateClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = empty_dispose;
  oc->finalize = empty_finalize;
  oc->set_property = empty_set_property;
  oc->get_property = empty_get_property;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  e_props[E_PROP_TITLE] = g_param_spec_string ("title", NULL, NULL, "", f);
  /**
   * OmarchyEmptyState:glyph:
   *
   * A Nerd Font glyph above the title, or empty for none.
   */
  e_props[E_PROP_GLYPH] = g_param_spec_string ("glyph", NULL, NULL, "", f);
  /**
   * OmarchyEmptyState:hint:
   *
   * A quieter line under the title, or empty for none.
   */
  e_props[E_PROP_HINT] = g_param_spec_string ("hint", NULL, NULL, "", f);
  /**
   * OmarchyEmptyState:action-text:
   *
   * A bordered button under the text, or empty for none. It emits ::action-clicked.
   */
  e_props[E_PROP_ACTION_TEXT] = g_param_spec_string ("action-text", NULL, NULL, "", f);
  /**
   * OmarchyEmptyState:size:
   *
   * "panel" (the default) or "overlay".
   */
  e_props[E_PROP_SIZE] = g_param_spec_string ("size", NULL, NULL, "panel", f);
  /**
   * OmarchyEmptyState:cursor:
   *
   * Force the action's hover-cursor look, for keyboard-driven panels.
   */
  e_props[E_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, E_N_PROPS, e_props);
  /**
   * OmarchyEmptyState::action-clicked:
   *
   * The action button was clicked, or activated with Return, Enter or Space.
   */
  e_signals[E_ACTION_CLICKED] = g_signal_new ("action-clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                              NULL, NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_layout_manager_type (wc, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (wc, "omarchy-empty-state");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_empty_state_new:
 * @title: (nullable): what there's none of ("No networks found")
 *
 * Returns: (transfer floating): an empty state
 */
GtkWidget *
omarchy_empty_state_new (const char *title)
{
  return g_object_new (OMARCHY_TYPE_EMPTY_STATE, "title", title ? title : "", NULL);
}

const char *
omarchy_empty_state_get_title (OmarchyEmptyState *self)
{
  return omarchy_label_get_text (OMARCHY_LABEL (self->title));
}

void
omarchy_empty_state_set_title (OmarchyEmptyState *self, const char *title)
{
  if (!g_strcmp0 (omarchy_empty_state_get_title (self), title ? title : ""))
    return;
  omarchy_label_set_text (OMARCHY_LABEL (self->title), title ? title : "");
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_LABEL, title ? title : "", -1);
  g_object_notify_by_pspec (G_OBJECT (self), e_props[E_PROP_TITLE]);
}

const char *
omarchy_empty_state_get_glyph (OmarchyEmptyState *self)
{
  return glyph_get_text (self->glyph);
}

void
omarchy_empty_state_set_glyph (OmarchyEmptyState *self, const char *glyph)
{
  if (!g_strcmp0 (omarchy_empty_state_get_glyph (self), glyph ? glyph : ""))
    return;
  glyph_set_text (self->glyph, glyph);
  g_object_notify_by_pspec (G_OBJECT (self), e_props[E_PROP_GLYPH]);
}

const char *
omarchy_empty_state_get_hint (OmarchyEmptyState *self)
{
  return omarchy_label_get_text (OMARCHY_LABEL (self->hint));
}

void
omarchy_empty_state_set_hint (OmarchyEmptyState *self, const char *hint)
{
  if (!g_strcmp0 (omarchy_empty_state_get_hint (self), hint ? hint : ""))
    return;
  omarchy_label_set_text (OMARCHY_LABEL (self->hint), hint ? hint : "");
  gtk_widget_set_visible (self->hint, hint && *hint);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, hint ? hint : "", -1);
  g_object_notify_by_pspec (G_OBJECT (self), e_props[E_PROP_HINT]);
}

const char *
omarchy_empty_state_get_action_text (OmarchyEmptyState *self)
{
  return omarchy_button_get_label (OMARCHY_BUTTON (self->action));
}

void
omarchy_empty_state_set_action_text (OmarchyEmptyState *self, const char *text)
{
  if (!g_strcmp0 (omarchy_empty_state_get_action_text (self), text ? text : ""))
    return;
  omarchy_button_set_label (OMARCHY_BUTTON (self->action), text ? text : "");
  gtk_widget_set_visible (self->action, text && *text);
  g_object_notify_by_pspec (G_OBJECT (self), e_props[E_PROP_ACTION_TEXT]);
}

const char *
omarchy_empty_state_get_size (OmarchyEmptyState *self)
{
  return self->size;
}

void
omarchy_empty_state_set_size (OmarchyEmptyState *self, const char *size)
{
  if (!size || !*size)
    size = "panel";
  if (!g_strcmp0 (self->size, size))
    return;
  g_free (self->size);
  self->size = g_strdup (size);
  empty_restyle (self);
  g_object_notify_by_pspec (G_OBJECT (self), e_props[E_PROP_SIZE]);
}

gboolean
omarchy_empty_state_get_cursor (OmarchyEmptyState *self)
{
  return self->cursor;
}

void
omarchy_empty_state_set_cursor (OmarchyEmptyState *self, gboolean cursor)
{
  if (self->cursor == !!cursor)
    return;
  self->cursor = !!cursor;
  omarchy_button_set_cursor (OMARCHY_BUTTON (self->action), self->cursor);
  g_object_notify_by_pspec (G_OBJECT (self), e_props[E_PROP_CURSOR]);
}

/* ================================================================================ Banner === */

/**
 * OmarchyBanner:
 *
 * An inline message in the page: the tone's glyph, the text (wrapping, taking the slack), an
 * optional bordered action button and an optional dismiss icon button (`󰅖`). Flat: the
 * controls' normal fill with a border in the tone, never a saturated background.
 * #OmarchyBanner:tone is "info" (accent, the default; "neutral" draws as info), "success",
 * "warning" or "error" (urgent). The banner doesn't hide itself: the app answers ::dismissed.
 */
struct _OmarchyBanner {
  GtkWidget parent_instance;
  GtkWidget *row;
  GtkWidget *mark, *label, *action, *dismiss;
  char *tone;
  char *glyph;
  gboolean dismissible, cursor;
};

enum { N_PROP_0, N_PROP_TEXT, N_PROP_TONE, N_PROP_GLYPH, N_PROP_ACTION_TEXT, N_PROP_DISMISSIBLE, N_PROP_CURSOR,
       N_N_PROPS };
static GParamSpec *n_props[N_N_PROPS];
enum { N_ACTION_CLICKED, N_DISMISSED, N_N_SIGNALS };
static guint n_signals[N_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyBanner, omarchy_banner, GTK_TYPE_WIDGET)

static const char *
banner_role (OmarchyBanner *self)
{
  if (!g_strcmp0 (self->tone, "success"))
    return "success";
  if (!g_strcmp0 (self->tone, "warning"))
    return "warning";
  if (!g_strcmp0 (self->tone, "error"))
    return "urgent";
  return "accent";
}

static const char *
banner_glyph (OmarchyBanner *self)
{
  if (self->glyph && *self->glyph)
    return self->glyph;
  if (!g_strcmp0 (self->tone, "success"))
    return "\U000F012C";
  if (!g_strcmp0 (self->tone, "warning"))
    return "\U000F002A";
  if (!g_strcmp0 (self->tone, "error"))
    return "\U000F015A";
  return "\U000F02FD";
}

static gboolean
banner_trailing (OmarchyBanner *self)
{
  return gtk_widget_get_visible (self->action) || self->dismissible;
}

static float
banner_border_width (void)
{
  return MAX (1, omarchy_border_max (&omarchy_tokens ()->normal.border));
}

/* Border + padding: top, end, bottom, start. */
static void
banner_insets (OmarchyBanner *self, float in[4])
{
  const OmarchyTokens *t = omarchy_tokens ();
  float b = banner_border_width ();
  in[0] = b + t->sm;
  in[1] = b + (banner_trailing (self) ? t->sm : t->control_padding_x);
  in[2] = b + t->sm;
  in[3] = b + t->control_padding_x;
}

static void
banner_sync (OmarchyBanner *self)
{
  glyph_set_text (self->mark, banner_glyph (self));
  glyph_set_look (self->mark, "icon", banner_role (self));
  gtk_widget_set_visible (self->dismiss, self->dismissible);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
banner_restyle (OmarchyBanner *self)
{
  gtk_box_set_spacing (GTK_BOX (self->row), omarchy_tokens ()->control_gap);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
banner_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyBanner *self = OMARCHY_BANNER (w);
  float in[4];
  banner_insets (self, in);
  int across = (int) (in[1] + in[3]), down = (int) (in[0] + in[2]);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    gtk_widget_measure (self->row, o, -1, min, nat, NULL, NULL);
    *min += across;
    *nat += across;
  } else {
    gtk_widget_measure (self->row, o, for_size >= 0 ? MAX (0, for_size - across) : -1, min, nat, NULL, NULL);
    const OmarchyTokens *t = omarchy_tokens ();
    float bw = MAX (omarchy_border_max (&t->normal.border), omarchy_border_max (&t->hover.border));
    int button_min_h = (int) ceil (omarchy_line_height (t->body) + t->control_padding_y * 2 + bw * 2);
    int floor_h = MAX (t->control_height, button_min_h);
    *min = MAX (floor_h, *min) + down;
    *nat = MAX (floor_h, *nat) + down;
  }
}

static GtkSizeRequestMode
height_for_width (GtkWidget *w)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
banner_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyBanner *self = OMARCHY_BANNER (w);
  float in[4];
  banner_insets (self, in);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  int rw = MAX (0, width - (int) (in[1] + in[3]));
  int inner = MAX (0, height - (int) (in[0] + in[2]));
  GtkAllocation a = { (int) (rtl ? in[1] : in[3]), (int) in[0], rw, inner };
  gtk_widget_size_allocate (self->row, &a, -1);
}

static void
banner_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyBanner *self = OMARCHY_BANNER (w);
  const OmarchyTokens *p = omarchy_paint ();
  GdkRGBA tone = omarchy_role_color (banner_role (self));
  OmarchyBorder border = omarchy_border_flat (&tone, banner_border_width ());
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, gtk_widget_get_width (w), gtk_widget_get_height (w)), p->radius,
                        &p->normal.fill, &border);
  GTK_WIDGET_CLASS (omarchy_banner_parent_class)->snapshot (w, s);
}

static void
banner_action_clicked (OmarchyBanner *self)
{
  g_signal_emit (self, n_signals[N_ACTION_CLICKED], 0);
}

static void
banner_dismiss_clicked (OmarchyBanner *self)
{
  g_signal_emit (self, n_signals[N_DISMISSED], 0);
}

static void
omarchy_banner_init (OmarchyBanner *self)
{
  self->tone = g_strdup ("info");
  self->glyph = g_strdup ("");
  self->row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_parent (self->row, GTK_WIDGET (self));

  self->mark = glyph_new ("icon", "accent");
  gtk_widget_set_valign (self->mark, GTK_ALIGN_CENTER);
  self->label = text_label ("body", "foreground", TRUE);
  gtk_widget_set_hexpand (self->label, TRUE);
  gtk_widget_set_valign (self->label, GTK_ALIGN_CENTER);
  self->action = omarchy_button_new ("");
  omarchy_button_set_bordered (OMARCHY_BUTTON (self->action), TRUE);
  gtk_widget_set_valign (self->action, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->action, FALSE);
  g_signal_connect_swapped (self->action, "clicked", G_CALLBACK (banner_action_clicked), self);
  self->dismiss = omarchy_icon_button_new ("\U000F0156");
  gtk_widget_set_valign (self->dismiss, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->dismiss, FALSE);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self->dismiss), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  omarchy_t ("common.dismiss", "Dismiss"), -1);
  g_signal_connect_swapped (self->dismiss, "clicked", G_CALLBACK (banner_dismiss_clicked), self);

  gtk_box_append (GTK_BOX (self->row), self->mark);
  gtk_box_append (GTK_BOX (self->row), self->label);
  gtk_box_append (GTK_BOX (self->row), self->action);
  gtk_box_append (GTK_BOX (self->row), self->dismiss);

  banner_restyle (self);
  banner_sync (self);
  omarchy_widget_follow (GTK_WIDGET (self));
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (banner_restyle), self,
                           G_CONNECT_SWAPPED);
}

static void
banner_dispose (GObject *o)
{
  OmarchyBanner *self = OMARCHY_BANNER (o);
  g_clear_pointer (&self->row, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_banner_parent_class)->dispose (o);
}

static void
banner_finalize (GObject *o)
{
  OmarchyBanner *self = OMARCHY_BANNER (o);
  g_free (self->tone);
  g_free (self->glyph);
  G_OBJECT_CLASS (omarchy_banner_parent_class)->finalize (o);
}

static void
banner_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyBanner *self = OMARCHY_BANNER (o);
  switch (id) {
  case N_PROP_TEXT: omarchy_banner_set_text (self, g_value_get_string (v)); break;
  case N_PROP_TONE: omarchy_banner_set_tone (self, g_value_get_string (v)); break;
  case N_PROP_GLYPH: omarchy_banner_set_glyph (self, g_value_get_string (v)); break;
  case N_PROP_ACTION_TEXT: omarchy_banner_set_action_text (self, g_value_get_string (v)); break;
  case N_PROP_DISMISSIBLE: omarchy_banner_set_dismissible (self, g_value_get_boolean (v)); break;
  case N_PROP_CURSOR: omarchy_banner_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
banner_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyBanner *self = OMARCHY_BANNER (o);
  switch (id) {
  case N_PROP_TEXT: g_value_set_string (v, omarchy_banner_get_text (self)); break;
  case N_PROP_TONE: g_value_set_string (v, self->tone); break;
  case N_PROP_GLYPH: g_value_set_string (v, self->glyph); break;
  case N_PROP_ACTION_TEXT: g_value_set_string (v, omarchy_banner_get_action_text (self)); break;
  case N_PROP_DISMISSIBLE: g_value_set_boolean (v, self->dismissible); break;
  case N_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_banner_class_init (OmarchyBannerClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = banner_dispose;
  oc->finalize = banner_finalize;
  oc->set_property = banner_set_property;
  oc->get_property = banner_get_property;
  wc->measure = banner_measure;
  wc->size_allocate = banner_allocate;
  wc->snapshot = banner_snapshot;
  wc->get_request_mode = height_for_width;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  n_props[N_PROP_TEXT] = g_param_spec_string ("text", NULL, NULL, "", f);
  /**
   * OmarchyBanner:tone:
   *
   * "info" (the default), "success", "warning" or "error": the border's and glyph's color and
   * the glyph (`󰋽`, `󰄬`, `󰀪`, `󰅚`).
   */
  n_props[N_PROP_TONE] = g_param_spec_string ("tone", NULL, NULL, "info", f);
  /**
   * OmarchyBanner:glyph:
   *
   * A Nerd Font glyph instead of the tone's, or empty for the tone's.
   */
  n_props[N_PROP_GLYPH] = g_param_spec_string ("glyph", NULL, NULL, "", f);
  /**
   * OmarchyBanner:action-text:
   *
   * A bordered button after the text, or empty for none. It emits ::action-clicked.
   */
  n_props[N_PROP_ACTION_TEXT] = g_param_spec_string ("action-text", NULL, NULL, "", f);
  /**
   * OmarchyBanner:dismissible:
   *
   * A `󰅖` icon button at the end that emits ::dismissed.
   */
  n_props[N_PROP_DISMISSIBLE] = g_param_spec_boolean ("dismissible", NULL, NULL, FALSE, f);
  /**
   * OmarchyBanner:cursor:
   *
   * Force the action's hover-cursor look, for keyboard-driven panels.
   */
  n_props[N_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, N_N_PROPS, n_props);
  /**
   * OmarchyBanner::action-clicked:
   *
   * The action button was clicked, or activated with Return, Enter or Space.
   */
  n_signals[N_ACTION_CLICKED] = g_signal_new ("action-clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                              NULL, NULL, G_TYPE_NONE, 0);
  /**
   * OmarchyBanner::dismissed:
   *
   * The dismiss button was clicked, or activated with Return, Enter or Space. Hide or remove
   * the banner here.
   */
  n_signals[N_DISMISSED] = g_signal_new ("dismissed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                         NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-banner");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_ALERT);
}

/**
 * omarchy_banner_new:
 * @text: (nullable): the message
 *
 * Returns: (transfer floating): an info banner
 */
GtkWidget *
omarchy_banner_new (const char *text)
{
  return g_object_new (OMARCHY_TYPE_BANNER, "text", text ? text : "", NULL);
}

const char *
omarchy_banner_get_text (OmarchyBanner *self)
{
  return omarchy_label_get_text (OMARCHY_LABEL (self->label));
}

void
omarchy_banner_set_text (OmarchyBanner *self, const char *text)
{
  if (!g_strcmp0 (omarchy_banner_get_text (self), text ? text : ""))
    return;
  omarchy_label_set_text (OMARCHY_LABEL (self->label), text ? text : "");
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_LABEL, text ? text : "", -1);
  g_object_notify_by_pspec (G_OBJECT (self), n_props[N_PROP_TEXT]);
}

const char *
omarchy_banner_get_tone (OmarchyBanner *self)
{
  return self->tone;
}

void
omarchy_banner_set_tone (OmarchyBanner *self, const char *tone)
{
  if (!tone || !*tone)
    tone = "info";
  if (!g_strcmp0 (self->tone, tone))
    return;
  g_free (self->tone);
  self->tone = g_strdup (tone);
  banner_sync (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), n_props[N_PROP_TONE]);
}

const char *
omarchy_banner_get_glyph (OmarchyBanner *self)
{
  return self->glyph;
}

void
omarchy_banner_set_glyph (OmarchyBanner *self, const char *glyph)
{
  if (!g_strcmp0 (self->glyph, glyph ? glyph : ""))
    return;
  g_free (self->glyph);
  self->glyph = g_strdup (glyph ? glyph : "");
  banner_sync (self);
  g_object_notify_by_pspec (G_OBJECT (self), n_props[N_PROP_GLYPH]);
}

const char *
omarchy_banner_get_action_text (OmarchyBanner *self)
{
  return omarchy_button_get_label (OMARCHY_BUTTON (self->action));
}

void
omarchy_banner_set_action_text (OmarchyBanner *self, const char *text)
{
  if (!g_strcmp0 (omarchy_banner_get_action_text (self), text ? text : ""))
    return;
  omarchy_button_set_label (OMARCHY_BUTTON (self->action), text ? text : "");
  gtk_widget_set_visible (self->action, text && *text);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), n_props[N_PROP_ACTION_TEXT]);
}

gboolean
omarchy_banner_get_dismissible (OmarchyBanner *self)
{
  return self->dismissible;
}

void
omarchy_banner_set_dismissible (OmarchyBanner *self, gboolean dismissible)
{
  if (self->dismissible == !!dismissible)
    return;
  self->dismissible = !!dismissible;
  banner_sync (self);
  g_object_notify_by_pspec (G_OBJECT (self), n_props[N_PROP_DISMISSIBLE]);
}

gboolean
omarchy_banner_get_cursor (OmarchyBanner *self)
{
  return self->cursor;
}

void
omarchy_banner_set_cursor (OmarchyBanner *self, gboolean cursor)
{
  if (self->cursor == !!cursor)
    return;
  self->cursor = !!cursor;
  omarchy_button_set_cursor (OMARCHY_BUTTON (self->action), self->cursor);
  g_object_notify_by_pspec (G_OBJECT (self), n_props[N_PROP_CURSOR]);
}

/* ================================================================================= Toast === */

/**
 * OmarchyToast:
 *
 * A transient message inside the app, on the notifications surface the shell's own toasts use
 * (background, border spec, Hyprland rounding): an optional `iconLarge` glyph, a bold title
 * over a wrapping line of text, an optional bordered action button, and a countdown line along
 * the inner bottom edge in `notifications.countdown`. It draws one state: the app owns when it
 * shows, where it stacks and the timer behind #OmarchyToast:countdown.
 */
struct _OmarchyToast {
  GtkWidget parent_instance;
  GtkWidget *row, *words;
  GtkWidget *mark, *title, *text, *action;
  double countdown;
  gboolean cursor;
};

enum { T_PROP_0, T_PROP_TITLE, T_PROP_TEXT, T_PROP_GLYPH, T_PROP_ACTION_TEXT, T_PROP_COUNTDOWN, T_PROP_CURSOR,
       T_N_PROPS };
static GParamSpec *t_props[T_N_PROPS];
enum { T_ACTION_CLICKED, T_N_SIGNALS };
static guint t_signals[T_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyToast, omarchy_toast, GTK_TYPE_WIDGET)

/* Border + padding: top, end, bottom, start. */
static void
toast_insets (float in[4])
{
  const OmarchyTokens *t = omarchy_tokens ();
  const float *bw = t->notifications.border.widths;
  float px = omarchy_space (12), py = omarchy_space (10);
  in[0] = bw[0] + py;
  in[1] = bw[1] + px;
  in[2] = bw[2] + py;
  in[3] = bw[3] + px;
}

static void
toast_repaint (OmarchyToast *self)
{
  const OmarchyTokens *p = omarchy_paint ();
  GdkRGBA body = omarchy_rgba_alpha (&p->notifications.text, 0.78);
  glyph_set_color (self->mark, &p->notifications.text);
  omarchy_label_set_color (OMARCHY_LABEL (self->title), &p->notifications.text);
  omarchy_label_set_color (OMARCHY_LABEL (self->text), &body);
}

static void
toast_restyle (OmarchyToast *self)
{
  gtk_box_set_spacing (GTK_BOX (self->row), omarchy_space (12));
  gtk_box_set_spacing (GTK_BOX (self->words), omarchy_space (3));
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
toast_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyToast *self = OMARCHY_TOAST (w);
  float in[4];
  toast_insets (in);
  int across = (int) (in[1] + in[3]), down = (int) (in[0] + in[2]);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    int rmin, rnat;
    gtk_widget_measure (self->row, o, -1, &rmin, &rnat, NULL, NULL);
    *min = rmin + across;
    *nat = MAX (*min, omarchy_space (360));
  } else {
    gtk_widget_measure (self->row, o, for_size >= 0 ? MAX (0, for_size - across) : -1, min, nat, NULL, NULL);
    *min += down;
    *nat += down;
  }
}

static void
toast_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyToast *self = OMARCHY_TOAST (w);
  float in[4];
  toast_insets (in);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  int rw = MAX (0, width - (int) (in[1] + in[3]));
  int rh;
  gtk_widget_measure (self->row, GTK_ORIENTATION_VERTICAL, rw, NULL, &rh, NULL, NULL);
  float line = self->countdown >= 0 ? MAX (1, omarchy_space (2)) : 0;
  float avail = height - (in[0] + in[2]) - line;
  GtkAllocation a = { (int) (rtl ? in[1] : in[3]), (int) in[0] + (int) roundf ((avail - rh) / 2.0f), rw, rh };
  gtk_widget_size_allocate (self->row, &a, -1);
}

static void
toast_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyToast *self = OMARCHY_TOAST (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  const OmarchyBorder *b = &p->notifications.border;
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), p->radius, &p->notifications.background, b);
  /* top, right, bottom, left */
  float bt = b->widths[0], br = b->widths[1], bb = b->widths[2], bl = b->widths[3];
  if (self->cursor) {
    GdkRGBA tint = state_for (&p->hover.fill, &p->notifications.text);
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (bl, bt, W - bl - br, H - bt - bb),
                       MAX (0, p->radius - omarchy_border_max (b)), &tint);
  }
  GTK_WIDGET_CLASS (omarchy_toast_parent_class)->snapshot (w, s);
  if (self->countdown >= 0) {
    /* Time left, shrinking toward the leading edge; clear of the rounded corners. */
    float line = MAX (1, omarchy_space (2));
    float full = W - bl - br - p->radius * 2;
    float cw = (float) (full * CLAMP (self->countdown, 0, 1));
    gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
    float x = rtl ? W - br - p->radius - cw : bl + p->radius;
    if (cw > 0)
      gtk_snapshot_append_color (s, &p->notifications_countdown, &GRAPHENE_RECT_INIT (x, H - bb - line, cw, line));
  }
}

static void
toast_action_clicked (OmarchyToast *self)
{
  g_signal_emit (self, t_signals[T_ACTION_CLICKED], 0);
}

static void
omarchy_toast_init (OmarchyToast *self)
{
  self->countdown = -1;
  self->row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_parent (self->row, GTK_WIDGET (self));
  self->words = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand (self->words, TRUE);
  gtk_widget_set_valign (self->words, GTK_ALIGN_CENTER);

  self->mark = glyph_new ("iconLarge", "popup");
  gtk_widget_set_valign (self->mark, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->mark, FALSE);
  self->title = text_label ("body", "popup", FALSE);
  omarchy_label_set_bold (OMARCHY_LABEL (self->title), TRUE);
  omarchy_label_set_ellipsize (OMARCHY_LABEL (self->title), TRUE);
  self->text = text_label ("body", "popup", TRUE);
  gtk_widget_set_visible (self->text, FALSE);
  self->action = omarchy_button_new ("");
  omarchy_button_set_bordered (OMARCHY_BUTTON (self->action), TRUE);
  gtk_widget_set_valign (self->action, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->action, FALSE);
  g_signal_connect_swapped (self->action, "clicked", G_CALLBACK (toast_action_clicked), self);

  gtk_box_append (GTK_BOX (self->words), self->title);
  gtk_box_append (GTK_BOX (self->words), self->text);
  gtk_box_append (GTK_BOX (self->row), self->mark);
  gtk_box_append (GTK_BOX (self->row), self->words);
  gtk_box_append (GTK_BOX (self->row), self->action);

  toast_restyle (self);
  toast_repaint (self);
  omarchy_widget_follow (GTK_WIDGET (self));
  OmarchyTheme *theme = omarchy_theme_get_default ();
  g_signal_connect_object (theme, "restyle", G_CALLBACK (toast_restyle), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (theme, "repaint", G_CALLBACK (toast_repaint), self, G_CONNECT_SWAPPED);
}

static void
toast_dispose (GObject *o)
{
  OmarchyToast *self = OMARCHY_TOAST (o);
  g_clear_pointer (&self->row, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_toast_parent_class)->dispose (o);
}

static void
toast_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyToast *self = OMARCHY_TOAST (o);
  switch (id) {
  case T_PROP_TITLE: omarchy_toast_set_title (self, g_value_get_string (v)); break;
  case T_PROP_TEXT: omarchy_toast_set_text (self, g_value_get_string (v)); break;
  case T_PROP_GLYPH: omarchy_toast_set_glyph (self, g_value_get_string (v)); break;
  case T_PROP_ACTION_TEXT: omarchy_toast_set_action_text (self, g_value_get_string (v)); break;
  case T_PROP_COUNTDOWN: omarchy_toast_set_countdown (self, g_value_get_double (v)); break;
  case T_PROP_CURSOR: omarchy_toast_set_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
toast_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyToast *self = OMARCHY_TOAST (o);
  switch (id) {
  case T_PROP_TITLE: g_value_set_string (v, omarchy_toast_get_title (self)); break;
  case T_PROP_TEXT: g_value_set_string (v, omarchy_toast_get_text (self)); break;
  case T_PROP_GLYPH: g_value_set_string (v, omarchy_toast_get_glyph (self)); break;
  case T_PROP_ACTION_TEXT: g_value_set_string (v, omarchy_toast_get_action_text (self)); break;
  case T_PROP_COUNTDOWN: g_value_set_double (v, self->countdown); break;
  case T_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_toast_class_init (OmarchyToastClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = toast_dispose;
  oc->set_property = toast_set_property;
  oc->get_property = toast_get_property;
  wc->measure = toast_measure;
  wc->size_allocate = toast_allocate;
  wc->snapshot = toast_snapshot;
  wc->get_request_mode = height_for_width;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  t_props[T_PROP_TITLE] = g_param_spec_string ("title", NULL, NULL, "", f);
  /**
   * OmarchyToast:text:
   *
   * A line under the title, wrapping, or empty for none.
   */
  t_props[T_PROP_TEXT] = g_param_spec_string ("text", NULL, NULL, "", f);
  /**
   * OmarchyToast:glyph:
   *
   * A Nerd Font glyph before the words, or empty for none.
   */
  t_props[T_PROP_GLYPH] = g_param_spec_string ("glyph", NULL, NULL, "", f);
  /**
   * OmarchyToast:action-text:
   *
   * A bordered button after the words, or empty for none. It emits ::action-clicked.
   */
  t_props[T_PROP_ACTION_TEXT] = g_param_spec_string ("action-text", NULL, NULL, "", f);
  /**
   * OmarchyToast:countdown:
   *
   * Time left, 1 → 0, drawn as a line along the bottom; below 0 (the default) hides it.
   */
  t_props[T_PROP_COUNTDOWN] = g_param_spec_double ("countdown", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, -1, f);
  /**
   * OmarchyToast:cursor:
   *
   * The keyboard's toast: tinted with the hover fill inside the border.
   */
  t_props[T_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, T_N_PROPS, t_props);
  /**
   * OmarchyToast::action-clicked:
   *
   * The action button was clicked, or activated with Return, Enter or Space.
   */
  t_signals[T_ACTION_CLICKED] = g_signal_new ("action-clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                              NULL, NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-toast");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_ALERT);
}

/**
 * omarchy_toast_new:
 * @title: (nullable): the bold first line
 *
 * Returns: (transfer floating): a toast
 */
GtkWidget *
omarchy_toast_new (const char *title)
{
  return g_object_new (OMARCHY_TYPE_TOAST, "title", title ? title : "", NULL);
}

const char *
omarchy_toast_get_title (OmarchyToast *self)
{
  return omarchy_label_get_text (OMARCHY_LABEL (self->title));
}

void
omarchy_toast_set_title (OmarchyToast *self, const char *title)
{
  if (!g_strcmp0 (omarchy_toast_get_title (self), title ? title : ""))
    return;
  omarchy_label_set_text (OMARCHY_LABEL (self->title), title ? title : "");
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_LABEL, title ? title : "", -1);
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_TITLE]);
}

const char *
omarchy_toast_get_text (OmarchyToast *self)
{
  return omarchy_label_get_text (OMARCHY_LABEL (self->text));
}

void
omarchy_toast_set_text (OmarchyToast *self, const char *text)
{
  if (!g_strcmp0 (omarchy_toast_get_text (self), text ? text : ""))
    return;
  omarchy_label_set_text (OMARCHY_LABEL (self->text), text ? text : "");
  gtk_widget_set_visible (self->text, text && *text);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, text ? text : "", -1);
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_TEXT]);
}

const char *
omarchy_toast_get_glyph (OmarchyToast *self)
{
  return glyph_get_text (self->mark);
}

void
omarchy_toast_set_glyph (OmarchyToast *self, const char *glyph)
{
  if (!g_strcmp0 (omarchy_toast_get_glyph (self), glyph ? glyph : ""))
    return;
  glyph_set_text (self->mark, glyph);
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_GLYPH]);
}

const char *
omarchy_toast_get_action_text (OmarchyToast *self)
{
  return omarchy_button_get_label (OMARCHY_BUTTON (self->action));
}

void
omarchy_toast_set_action_text (OmarchyToast *self, const char *text)
{
  if (!g_strcmp0 (omarchy_toast_get_action_text (self), text ? text : ""))
    return;
  omarchy_button_set_label (OMARCHY_BUTTON (self->action), text ? text : "");
  gtk_widget_set_visible (self->action, text && *text);
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_ACTION_TEXT]);
}

double
omarchy_toast_get_countdown (OmarchyToast *self)
{
  return self->countdown;
}

void
omarchy_toast_set_countdown (OmarchyToast *self, double countdown)
{
  if (self->countdown == countdown)
    return;
  gboolean had = self->countdown >= 0;
  self->countdown = countdown;
  if (had != (self->countdown >= 0))
    gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_COUNTDOWN]);
}

gboolean
omarchy_toast_get_cursor (OmarchyToast *self)
{
  return self->cursor;
}

void
omarchy_toast_set_cursor (OmarchyToast *self, gboolean cursor)
{
  if (self->cursor == !!cursor)
    return;
  self->cursor = !!cursor;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), t_props[T_PROP_CURSOR]);
}
