/* omarchy-navigation.c: Keycap (Keycap.qml), KeyCombo (KeyCombo.qml), ShortcutSheet
 * (ShortcutSheet.qml), Toolbar and ToolbarSeparator (Toolbar.qml, ToolbarSeparator.qml), Tabs
 * (Tabs.qml) and IconRail (IconRail.qml), drawn as the Qt kit draws them. */
#include "omarchy-navigation-private.h"

#include <math.h>
#include <string.h>

/* ================================================================================ shared === */

double
omarchy_nav_ink_center (GtkWidget *w, const char *text, int px, gboolean bold)
{
  /* Tight bounds in whole pixels would shift the middle; a hundred times larger keeps them to
   * the outline (OpticalGlyph's metricsScale). */
  g_autoptr (PangoLayout) l = omarchy_layout (w, text, MAX (1, px) * 100, bold);
  PangoRectangle ink;
  pango_layout_get_extents (l, &ink, NULL);
  return (ink.x + ink.width / 2.0) / PANGO_SCALE / 100.0;
}

void
omarchy_nav_draw_glyph (GtkWidget *w, GtkSnapshot *s, const char *text, int px, float x, float y, float width,
                        float height, const GdkRGBA *color)
{
  if (!text || !*text)
    return;
  g_autoptr (PangoLayout) l = omarchy_layout (w, text, px, FALSE);
  double c = omarchy_nav_ink_center (w, text, px, FALSE);
  omarchy_draw_layout (s, l, (float) (x + width / 2.0 - c), y + roundf ((height - omarchy_line_height (px)) / 2), color);
}

static float
qt_center (float size)
{
  int i = (int) size;
  return i % 2 ? (size + 1) / 2 : size / 2;
}

float
omarchy_nav_center (float outer, float inner)
{
  return qt_center (outer) - qt_center (inner);
}

double
omarchy_nav_text_advance (GtkWidget *w, const char *text, int px, gboolean bold)
{
  if (!text || !*text)
    return 0;
  g_autoptr (PangoLayout) l = omarchy_layout (w, text, px, bold);
  PangoRectangle logical;
  pango_layout_get_extents (l, NULL, &logical);
  return logical.width / (double) PANGO_SCALE;
}

int
omarchy_nav_text_width (GtkWidget *w, const char *text, int px, gboolean bold)
{
  if (!text || !*text)
    return 0;
  g_autoptr (PangoLayout) l = omarchy_layout (w, text, px, bold);
  PangoRectangle logical;
  pango_layout_get_extents (l, NULL, &logical);
  return (int) ceil (logical.width / (double) PANGO_SCALE - 1e-3);
}

static gboolean
slide_tick (GtkWidget *w, GdkFrameClock *clock, gpointer data)
{
  OmarchyNavSlide *slide = data;
  double d = omarchy_tokens ()->motion_press * 1000.0;
  double x = d > 0 ? (gdk_frame_clock_get_frame_time (clock) - slide->start) / d : 1;
  if (x >= 1) {
    slide->value = slide->to;
  } else {
    double e = omarchy_cubic_bezier (omarchy_tokens ()->ease_out, MAX (0, x));
    slide->value = slide->from + (slide->to - slide->from) * e;
  }
  gtk_widget_queue_draw (w);
  if (x >= 1) {
    slide->tick = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

void
omarchy_nav_slide (GtkWidget *w, OmarchyNavSlide *slide, double to)
{
  if (!slide->set || omarchy_tokens ()->reduce_motion || !gtk_widget_get_mapped (w)) {
    omarchy_nav_slide_stop (w, slide);
    slide->from = slide->to = slide->value = to;
    slide->set = TRUE;
    gtk_widget_queue_draw (w);
    return;
  }
  slide->from = slide->value;
  slide->to = to;
  slide->start = g_get_monotonic_time ();
  if (!slide->tick)
    slide->tick = gtk_widget_add_tick_callback (w, slide_tick, slide, NULL);
}

void
omarchy_nav_slide_stop (GtkWidget *w, OmarchyNavSlide *slide)
{
  if (slide->tick) {
    gtk_widget_remove_tick_callback (w, slide->tick);
    slide->tick = 0;
  }
  slide->value = slide->to;
}

/* ================================================================================ Keycap === */

/**
 * OmarchyKeycap:
 *
 * One key, drawn as a small cap: `space(18)` tall, the small radius, the normal control fill and
 * border, the label in `caption` centered by its ink (so arrows and ⇧ sit in the middle like
 * letters do), at least square. Key names are normalized as every binding does
 * (omarchy_keycap_normalize()).
 */
struct _OmarchyKeycap {
  GtkWidget parent_instance;
  char *key, *label, *foreground;
};

enum { K_PROP_0, K_PROP_KEY, K_PROP_LABEL, K_PROP_FOREGROUND, K_N_PROPS };
static GParamSpec *k_props[K_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyKeycap, omarchy_keycap, GTK_TYPE_WIDGET)

/**
 * omarchy_keycap_normalize:
 * @key: a key name ("ctrl", "shift", "Enter", "k")
 *
 * The label a key name draws as: modifiers read "Super", "Ctrl", "Alt", "⇧"; Enter, Backspace
 * and the arrows are glyphs; one character is upper case; anything else is shown as given.
 *
 * Returns: (transfer full): the label
 */
char *
omarchy_keycap_normalize (const char *key)
{
  static const char *const names[][2] = {
    { "super", "Super" }, { "meta", "Super" }, { "win", "Super" }, { "cmd", "Super" }, { "logo", "Super" },
    { "ctrl", "Ctrl" }, { "control", "Ctrl" }, { "alt", "Alt" }, { "option", "Alt" }, { "shift", "⇧" },
    { "enter", "⏎" }, { "return", "⏎" }, { "backspace", "⌫" }, { "esc", "Esc" }, { "escape", "Esc" },
    { "tab", "Tab" }, { "space", "Space" }, { "up", "↑" }, { "down", "↓" }, { "left", "←" }, { "right", "→" },
    { "pageup", "PgUp" }, { "pagedown", "PgDn" }, { "delete", "Del" }, { "del", "Del" }, { "home", "Home" },
    { "end", "End" },
  };
  g_autofree char *s = g_strstrip (g_strdup (key ? key : ""));
  g_autofree char *lower = g_utf8_strdown (s, -1);
  for (gsize i = 0; i < G_N_ELEMENTS (names); i++)
    if (!strcmp (lower, names[i][0]))
      return g_strdup (names[i][1]);
  if (g_utf8_strlen (s, -1) == 1)
    return g_utf8_strup (s, -1);
  return g_steal_pointer (&s);
}

static void
keycap_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyKeycap *self = OMARCHY_KEYCAP (w);
  const OmarchyTokens *t = omarchy_tokens ();
  int h = omarchy_space (18);
  if (o == GTK_ORIENTATION_VERTICAL) {
    *min = *nat = h;
    return;
  }
  int advance = omarchy_nav_text_width (w, self->label, t->caption, FALSE);
  *min = *nat = MAX (h, (int) ceilf (advance + t->sm * 2 + t->normal.border.widths[3] + t->normal.border.widths[1]));
}

static void
keycap_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyKeycap *self = OMARCHY_KEYCAP (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), MIN (4, p->radius), &p->normal.fill, &p->normal.border);
  GdkRGBA fg = omarchy_role_color (self->foreground);
  omarchy_nav_draw_glyph (w, s, self->label, p->caption, 0, 0, W, H, &fg);
}

static void
omarchy_keycap_init (OmarchyKeycap *self)
{
  self->key = g_strdup ("");
  self->label = g_strdup ("");
  self->foreground = g_strdup ("foreground");
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
keycap_finalize (GObject *o)
{
  OmarchyKeycap *self = OMARCHY_KEYCAP (o);
  g_free (self->key);
  g_free (self->label);
  g_free (self->foreground);
  G_OBJECT_CLASS (omarchy_keycap_parent_class)->finalize (o);
}

static void
keycap_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyKeycap *self = OMARCHY_KEYCAP (o);
  switch (id) {
  case K_PROP_KEY: omarchy_keycap_set_key (self, g_value_get_string (v)); break;
  case K_PROP_FOREGROUND: omarchy_keycap_set_foreground (self, g_value_get_string (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
keycap_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyKeycap *self = OMARCHY_KEYCAP (o);
  switch (id) {
  case K_PROP_KEY: g_value_set_string (v, self->key); break;
  case K_PROP_LABEL: g_value_set_string (v, self->label); break;
  case K_PROP_FOREGROUND: g_value_set_string (v, self->foreground); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_keycap_class_init (OmarchyKeycapClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = keycap_finalize;
  oc->set_property = keycap_set_property;
  oc->get_property = keycap_get_property;
  wc->measure = keycap_measure;
  wc->snapshot = keycap_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyKeycap:key:
   *
   * The key's name, any case ("ctrl", "Shift", "k").
   */
  k_props[K_PROP_KEY] = g_param_spec_string ("key", NULL, NULL, "", f);
  /**
   * OmarchyKeycap:label:
   *
   * What the cap shows: #OmarchyKeycap:key normalized.
   */
  k_props[K_PROP_LABEL] = g_param_spec_string ("label", NULL, NULL, "", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * OmarchyKeycap:foreground:
   *
   * The label's color role (`foreground`, `dim`, `accent`, …).
   */
  k_props[K_PROP_FOREGROUND] = g_param_spec_string ("foreground", NULL, NULL, "foreground", f);
  g_object_class_install_properties (oc, K_N_PROPS, k_props);
  gtk_widget_class_set_css_name (wc, "omarchy-keycap");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GENERIC);
}

/**
 * omarchy_keycap_new:
 * @key: (nullable): the key's name
 *
 * Returns: (transfer floating): a keycap
 */
GtkWidget *
omarchy_keycap_new (const char *key)
{
  return g_object_new (OMARCHY_TYPE_KEYCAP, "key", key ? key : "", NULL);
}

const char *
omarchy_keycap_get_key (OmarchyKeycap *self)
{
  return self->key;
}

void
omarchy_keycap_set_key (OmarchyKeycap *self, const char *key)
{
  if (!g_strcmp0 (self->key, key))
    return;
  g_free (self->key);
  self->key = g_strdup (key ? key : "");
  g_free (self->label);
  self->label = omarchy_keycap_normalize (self->key);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_LABEL, self->label, -1);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), k_props[K_PROP_KEY]);
  g_object_notify_by_pspec (G_OBJECT (self), k_props[K_PROP_LABEL]);
}

const char *
omarchy_keycap_get_label (OmarchyKeycap *self)
{
  return self->label;
}

OMARCHY_STRING_ACCESSORS (OmarchyKeycap, keycap, foreground, k_props, K_PROP_FOREGROUND, FALSE)

/* ============================================================================== KeyCombo === */

/**
 * OmarchyKeyCombo:
 *
 * A key combination as #OmarchyKeycap caps `xxs` apart: "Ctrl+Shift+S" draws Ctrl, ⇧, S. A lone
 * "+" is the plus key ("Ctrl++").
 */
struct _OmarchyKeyCombo {
  GtkWidget parent_instance;
  char *keys;
};

enum { KC_PROP_0, KC_PROP_KEYS, KC_N_PROPS };
static GParamSpec *kc_props[KC_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyKeyCombo, omarchy_key_combo, GTK_TYPE_WIDGET)

/**
 * omarchy_key_combo_split:
 * @combo: a combination ("Ctrl+Shift+S", "Ctrl++")
 *
 * Returns: (transfer full) (array zero-terminated=1): the keys, in order
 */
char **
omarchy_key_combo_split (const char *combo)
{
  g_auto (GStrv) parts = g_strsplit (combo ? combo : "", "+", -1);
  GStrvBuilder *b = g_strv_builder_new ();
  guint n = g_strv_length (parts);
  for (guint i = 0; i < n; i++) {
    if (*parts[i]) {
      g_strv_builder_take (b, g_strstrip (g_strdup (parts[i])));
    } else if (i + 1 < n && !*parts[i + 1]) {
      g_strv_builder_add (b, "+");
      i++;
    }
  }
  char **out = g_strv_builder_end (b);
  g_strv_builder_unref (b);
  return out;
}

static void
combo_restyle (OmarchyKeyCombo *self)
{
  GtkLayoutManager *lm = gtk_widget_get_layout_manager (GTK_WIDGET (self));
  gtk_box_layout_set_spacing (GTK_BOX_LAYOUT (lm), omarchy_tokens ()->xxs);
}

static void
omarchy_key_combo_init (OmarchyKeyCombo *self)
{
  self->keys = g_strdup ("");
  combo_restyle (self);
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (combo_restyle), self, G_CONNECT_SWAPPED);
}

static void
combo_dispose (GObject *o)
{
  omarchy_widget_dispose_children (GTK_WIDGET (o));
  G_OBJECT_CLASS (omarchy_key_combo_parent_class)->dispose (o);
}

static void
combo_finalize (GObject *o)
{
  g_free (OMARCHY_KEY_COMBO (o)->keys);
  G_OBJECT_CLASS (omarchy_key_combo_parent_class)->finalize (o);
}

static void
combo_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  if (id == KC_PROP_KEYS)
    omarchy_key_combo_set_keys (OMARCHY_KEY_COMBO (o), g_value_get_string (v));
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
combo_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  if (id == KC_PROP_KEYS)
    g_value_set_string (v, OMARCHY_KEY_COMBO (o)->keys);
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
omarchy_key_combo_class_init (OmarchyKeyComboClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = combo_dispose;
  oc->finalize = combo_finalize;
  oc->set_property = combo_set_property;
  oc->get_property = combo_get_property;
  kc_props[KC_PROP_KEYS] = g_param_spec_string ("keys", NULL, NULL, "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, KC_N_PROPS, kc_props);
  gtk_widget_class_set_layout_manager_type (wc, GTK_TYPE_BOX_LAYOUT);
  gtk_widget_class_set_css_name (wc, "omarchy-key-combo");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_key_combo_new:
 * @keys: (nullable): the combination ("Ctrl+Shift+P")
 *
 * Returns: (transfer floating): a key combination
 */
GtkWidget *
omarchy_key_combo_new (const char *keys)
{
  return g_object_new (OMARCHY_TYPE_KEY_COMBO, "keys", keys ? keys : "", NULL);
}

const char *
omarchy_key_combo_get_keys (OmarchyKeyCombo *self)
{
  return self->keys;
}

void
omarchy_key_combo_set_keys (OmarchyKeyCombo *self, const char *keys)
{
  if (!g_strcmp0 (self->keys, keys))
    return;
  g_free (self->keys);
  self->keys = g_strdup (keys ? keys : "");
  omarchy_widget_dispose_children (GTK_WIDGET (self));
  g_auto (GStrv) parts = omarchy_key_combo_split (self->keys);
  for (char **k = parts; *k; k++) {
    GtkWidget *cap = omarchy_keycap_new (*k);
    gtk_widget_set_valign (cap, GTK_ALIGN_CENTER);
    gtk_widget_set_parent (cap, GTK_WIDGET (self));
  }
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_LABEL, self->keys, -1);
  g_object_notify_by_pspec (G_OBJECT (self), kc_props[KC_PROP_KEYS]);
}

/* ========================================================================= ShortcutSheet === */

/**
 * OmarchyShortcutSheet:
 *
 * Keyboard shortcuts, grouped: each group is a section header over rows of an
 * #OmarchyKeyCombo and what it does (`body`, foreground), the combos in a column as wide as the
 * group's widest, `xxl` before the descriptions, rows `md` apart. Groups fill
 * #OmarchyShortcutSheet:columns columns left to right in turn, `panelGap × 2` apart, and stack
 * `panelGap` apart within a column. It is the sheet's content: put it on a card or in a dialog.
 */
typedef struct {
  char *title;
  GPtrArray *combos, *descriptions;
} SheetGroup;

struct _OmarchyShortcutSheet {
  GtkWidget parent_instance;
  GPtrArray *groups;
  int columns;
};

enum { SS_PROP_0, SS_PROP_COLUMNS, SS_N_PROPS };
static GParamSpec *ss_props[SS_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyShortcutSheet, omarchy_shortcut_sheet, GTK_TYPE_WIDGET)

static void
sheet_group_free (gpointer data)
{
  SheetGroup *g = data;
  g_free (g->title);
  g_ptr_array_unref (g->combos);
  g_ptr_array_unref (g->descriptions);
  g_free (g);
}

static void
sheet_rebuild (OmarchyShortcutSheet *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  GtkWidget *w = GTK_WIDGET (self);
  omarchy_widget_dispose_children (w);
  gtk_box_layout_set_spacing (GTK_BOX_LAYOUT (gtk_widget_get_layout_manager (w)), t->panel_gap * 2);
  int n = (int) self->groups->len;
  int columns = MAX (1, MIN (self->columns, n));
  for (int c = 0; c < columns; c++) {
    GtkWidget *column = gtk_box_new (GTK_ORIENTATION_VERTICAL, t->panel_gap);
    gtk_widget_set_valign (column, GTK_ALIGN_START);
    for (int i = c; i < n; i += MAX (1, self->columns)) {
      SheetGroup *g = g_ptr_array_index (self->groups, i);
      GtkWidget *group = gtk_box_new (GTK_ORIENTATION_VERTICAL, t->md);
      GtkWidget *header = omarchy_section_header_new (g->title);
      gtk_widget_set_halign (header, GTK_ALIGN_START);
      gtk_box_append (GTK_BOX (group), header);
      GtkWidget *grid = gtk_grid_new ();
      gtk_grid_set_column_spacing (GTK_GRID (grid), t->xxl);
      gtk_grid_set_row_spacing (GTK_GRID (grid), t->md);
      gtk_widget_set_halign (grid, GTK_ALIGN_START);
      for (guint r = 0; r < g->combos->len; r++) {
        GtkWidget *combo = omarchy_key_combo_new (g_ptr_array_index (g->combos, r));
        gtk_widget_set_halign (combo, GTK_ALIGN_START);
        gtk_widget_set_valign (combo, GTK_ALIGN_CENTER);
        gtk_grid_attach (GTK_GRID (grid), combo, 0, (int) r, 1, 1);
        GtkWidget *label = omarchy_label_new (g_ptr_array_index (g->descriptions, r));
        gtk_widget_set_halign (label, GTK_ALIGN_START);
        gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
        gtk_grid_attach (GTK_GRID (grid), label, 1, (int) r, 1, 1);
      }
      gtk_box_append (GTK_BOX (group), grid);
      gtk_box_append (GTK_BOX (column), group);
    }
    gtk_widget_set_parent (column, w);
  }
}

static void
omarchy_shortcut_sheet_init (OmarchyShortcutSheet *self)
{
  self->groups = g_ptr_array_new_with_free_func (sheet_group_free);
  self->columns = 2;
  sheet_rebuild (self);
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (sheet_rebuild), self, G_CONNECT_SWAPPED);
}

static void
sheet_dispose (GObject *o)
{
  omarchy_widget_dispose_children (GTK_WIDGET (o));
  G_OBJECT_CLASS (omarchy_shortcut_sheet_parent_class)->dispose (o);
}

static void
sheet_finalize (GObject *o)
{
  g_ptr_array_unref (OMARCHY_SHORTCUT_SHEET (o)->groups);
  G_OBJECT_CLASS (omarchy_shortcut_sheet_parent_class)->finalize (o);
}

static void
sheet_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  if (id == SS_PROP_COLUMNS)
    omarchy_shortcut_sheet_set_columns (OMARCHY_SHORTCUT_SHEET (o), g_value_get_int (v));
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
sheet_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  if (id == SS_PROP_COLUMNS)
    g_value_set_int (v, OMARCHY_SHORTCUT_SHEET (o)->columns);
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
omarchy_shortcut_sheet_class_init (OmarchyShortcutSheetClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = sheet_dispose;
  oc->finalize = sheet_finalize;
  oc->set_property = sheet_set_property;
  oc->get_property = sheet_get_property;
  ss_props[SS_PROP_COLUMNS] = g_param_spec_int ("columns", NULL, NULL, 1, G_MAXINT, 2, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, SS_N_PROPS, ss_props);
  gtk_widget_class_set_layout_manager_type (wc, GTK_TYPE_BOX_LAYOUT);
  gtk_widget_class_set_css_name (wc, "omarchy-shortcut-sheet");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GROUP);
}

/**
 * omarchy_shortcut_sheet_new:
 *
 * Returns: (transfer floating): an empty sheet; add groups with
 *   omarchy_shortcut_sheet_add_group() and their rows with omarchy_shortcut_sheet_add_shortcut()
 */
GtkWidget *
omarchy_shortcut_sheet_new (void)
{
  return g_object_new (OMARCHY_TYPE_SHORTCUT_SHEET, NULL);
}

/**
 * omarchy_shortcut_sheet_add_group:
 * @self: the sheet
 * @title: the group's header
 *
 * Starts a group; omarchy_shortcut_sheet_add_shortcut() adds rows to the newest group.
 */
void
omarchy_shortcut_sheet_add_group (OmarchyShortcutSheet *self, const char *title)
{
  SheetGroup *g = g_new0 (SheetGroup, 1);
  g->title = g_strdup (title ? title : "");
  g->combos = g_ptr_array_new_with_free_func (g_free);
  g->descriptions = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (self->groups, g);
  sheet_rebuild (self);
}

/**
 * omarchy_shortcut_sheet_add_shortcut:
 * @self: the sheet
 * @combo: the combination ("Ctrl+S")
 * @description: what it does
 *
 * Adds a row to the newest group (a group with no title when there is none yet).
 */
void
omarchy_shortcut_sheet_add_shortcut (OmarchyShortcutSheet *self, const char *combo, const char *description)
{
  if (self->groups->len == 0)
    omarchy_shortcut_sheet_add_group (self, "");
  SheetGroup *g = g_ptr_array_index (self->groups, self->groups->len - 1);
  g_ptr_array_add (g->combos, g_strdup (combo ? combo : ""));
  g_ptr_array_add (g->descriptions, g_strdup (description ? description : ""));
  sheet_rebuild (self);
}

void
omarchy_shortcut_sheet_remove_all (OmarchyShortcutSheet *self)
{
  g_ptr_array_set_size (self->groups, 0);
  sheet_rebuild (self);
}

int
omarchy_shortcut_sheet_get_columns (OmarchyShortcutSheet *self)
{
  return self->columns;
}

void
omarchy_shortcut_sheet_set_columns (OmarchyShortcutSheet *self, int columns)
{
  columns = MAX (1, columns);
  if (self->columns == columns)
    return;
  self->columns = columns;
  sheet_rebuild (self);
  g_object_notify_by_pspec (G_OBJECT (self), ss_props[SS_PROP_COLUMNS]);
}

/* ====================================================================== ToolbarSeparator === */

/**
 * OmarchyToolbarSeparator:
 *
 * The vertical rule between groups in a toolbar: 1 px in `track`, `space(16)` tall, `sm` clear
 * on each side.
 */
struct _OmarchyToolbarSeparator {
  GtkWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (OmarchyToolbarSeparator, omarchy_toolbar_separator, GTK_TYPE_WIDGET)

static void
tsep_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  *min = *nat = o == GTK_ORIENTATION_HORIZONTAL ? omarchy_tokens ()->sm * 2 + 1 : omarchy_space (16);
}

static void
tsep_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  const OmarchyTokens *p = omarchy_paint ();
  gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (p->sm, 0, 1, gtk_widget_get_height (w)));
}

static void
omarchy_toolbar_separator_init (OmarchyToolbarSeparator *self)
{
  omarchy_widget_follow (GTK_WIDGET (self));
  gtk_widget_set_valign (GTK_WIDGET (self), GTK_ALIGN_CENTER);
}

static void
omarchy_toolbar_separator_class_init (OmarchyToolbarSeparatorClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  wc->measure = tsep_measure;
  wc->snapshot = tsep_snapshot;
  gtk_widget_class_set_css_name (wc, "omarchy-toolbar-separator");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_SEPARATOR);
}

/**
 * omarchy_toolbar_separator_new:
 *
 * Returns: (transfer floating): a toolbar rule
 */
GtkWidget *
omarchy_toolbar_separator_new (void)
{
  return g_object_new (OMARCHY_TYPE_TOOLBAR_SEPARATOR, NULL);
}

/* =============================================================================== Toolbar === */

/**
 * OmarchyToolbar:
 *
 * An app toolbar: buttons in three slots (leading at the left, center centered on the whole
 * bar, trailing at the right), items `xxs` apart and vertically centered, groups split by
 * #OmarchyToolbarSeparator rules, an optional overflow "…" icon button after the trailing slot
 * (::overflow-triggered), `sm` inside its edges, and a 1 px `track` rule along the bottom when
 * #OmarchyToolbar:divided. Builder children go to the leading slot; `<child type="center">` and
 * `<child type="trailing">` to the others.
 */
struct _OmarchyToolbar {
  GtkWidget parent_instance;
  GtkWidget *leading, *center, *end, *trailing, *more;
  gboolean overflow, overflow_cursor, divided;
};

enum { TB_PROP_0, TB_PROP_OVERFLOW, TB_PROP_OVERFLOW_CURSOR, TB_PROP_DIVIDED, TB_N_PROPS };
static GParamSpec *tb_props[TB_N_PROPS];
enum { TB_OVERFLOW_TRIGGERED, TB_N_SIGNALS };
static guint tb_signals[TB_N_SIGNALS];

static void toolbar_buildable_init (GtkBuildableIface *iface);
G_DEFINE_FINAL_TYPE_WITH_CODE (OmarchyToolbar, omarchy_toolbar, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, toolbar_buildable_init))

static void
toolbar_restyle (OmarchyToolbar *self)
{
  int xxs = omarchy_tokens ()->xxs;
  gtk_box_set_spacing (GTK_BOX (self->leading), xxs);
  gtk_box_set_spacing (GTK_BOX (self->center), xxs);
  gtk_box_set_spacing (GTK_BOX (self->end), xxs);
  gtk_box_set_spacing (GTK_BOX (self->trailing), xxs);
}

static void
toolbar_sizes (OmarchyToolbar *self, int w[3], int h[3])
{
  GtkWidget *slots[3] = { self->leading, self->center, self->end };
  for (int i = 0; i < 3; i++) {
    gtk_widget_measure (slots[i], GTK_ORIENTATION_HORIZONTAL, -1, NULL, &w[i], NULL, NULL);
    gtk_widget_measure (slots[i], GTK_ORIENTATION_VERTICAL, -1, NULL, &h[i], NULL, NULL);
  }
}

static void
toolbar_measure (GtkWidget *wd, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyToolbar *self = OMARCHY_TOOLBAR (wd);
  const OmarchyTokens *t = omarchy_tokens ();
  int w[3], h[3];
  toolbar_sizes (self, w, h);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *nat = w[0] + w[1] + w[2] + t->sm * 2 + t->huge * 2;
    *min = w[0] + w[1] + w[2] + t->sm * 2;
  } else {
    *min = *nat = MAX (h[0], MAX (h[1], h[2])) + t->sm * 2 + (self->divided ? 1 : 0);
  }
}

static void
toolbar_allocate (GtkWidget *wd, int width, int height, int baseline)
{
  OmarchyToolbar *self = OMARCHY_TOOLBAR (wd);
  const OmarchyTokens *t = omarchy_tokens ();
  int w[3], h[3];
  toolbar_sizes (self, w, h);
  int pad = t->sm, content = MAX (h[0], MAX (h[1], h[2]));
  gboolean rtl = gtk_widget_get_direction (wd) == GTK_TEXT_DIR_RTL;
  int x[3] = { pad, (int) lround ((width - w[1]) / 2.0), width - pad - w[2] };
  if (rtl) {
    x[0] = width - pad - w[0];
    x[2] = pad;
  }
  GtkWidget *slots[3] = { self->leading, self->center, self->end };
  for (int i = 0; i < 3; i++) {
    GtkAllocation a = { x[i], pad + (int) lround ((content - h[i]) / 2.0), w[i], h[i] };
    gtk_widget_size_allocate (slots[i], &a, -1);
  }
}

static void
toolbar_snapshot (GtkWidget *wd, GtkSnapshot *s)
{
  OmarchyToolbar *self = OMARCHY_TOOLBAR (wd);
  const OmarchyTokens *p = omarchy_paint ();
  if (self->divided)
    gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, gtk_widget_get_height (wd) - 1, gtk_widget_get_width (wd), 1));
  GTK_WIDGET_CLASS (omarchy_toolbar_parent_class)->snapshot (wd, s);
}

static void
toolbar_more_clicked (GtkWidget *b, OmarchyToolbar *self)
{
  g_signal_emit (self, tb_signals[TB_OVERFLOW_TRIGGERED], 0);
}

static GtkWidget *
toolbar_box (void)
{
  return gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
}

static void
omarchy_toolbar_init (OmarchyToolbar *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->divided = TRUE;
  self->leading = toolbar_box ();
  self->center = toolbar_box ();
  self->end = toolbar_box ();
  self->trailing = toolbar_box ();
  gtk_widget_set_parent (self->leading, w);
  gtk_widget_set_parent (self->center, w);
  gtk_widget_set_parent (self->end, w);
  gtk_box_append (GTK_BOX (self->end), self->trailing);
  self->more = omarchy_icon_button_new ("\xef\x85\x81"); /* U+F141 */
  gtk_widget_set_valign (self->more, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->more, FALSE);
  g_signal_connect (self->more, "clicked", G_CALLBACK (toolbar_more_clicked), self);
  gtk_box_append (GTK_BOX (self->end), self->more);
  toolbar_restyle (self);
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (toolbar_restyle), self, G_CONNECT_SWAPPED);
  omarchy_widget_follow (w);
}

static void
toolbar_dispose (GObject *o)
{
  OmarchyToolbar *self = OMARCHY_TOOLBAR (o);
  g_clear_pointer (&self->leading, gtk_widget_unparent);
  g_clear_pointer (&self->center, gtk_widget_unparent);
  g_clear_pointer (&self->end, gtk_widget_unparent);
  self->trailing = self->more = NULL;
  G_OBJECT_CLASS (omarchy_toolbar_parent_class)->dispose (o);
}

static void
toolbar_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyToolbar *self = OMARCHY_TOOLBAR (o);
  switch (id) {
  case TB_PROP_OVERFLOW: omarchy_toolbar_set_overflow (self, g_value_get_boolean (v)); break;
  case TB_PROP_OVERFLOW_CURSOR: omarchy_toolbar_set_overflow_cursor (self, g_value_get_boolean (v)); break;
  case TB_PROP_DIVIDED: omarchy_toolbar_set_divided (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
toolbar_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyToolbar *self = OMARCHY_TOOLBAR (o);
  switch (id) {
  case TB_PROP_OVERFLOW: g_value_set_boolean (v, self->overflow); break;
  case TB_PROP_OVERFLOW_CURSOR: g_value_set_boolean (v, self->overflow_cursor); break;
  case TB_PROP_DIVIDED: g_value_set_boolean (v, self->divided); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static GtkBuildableIface *toolbar_parent_buildable;

static void
toolbar_add_child (GtkBuildable *b, GtkBuilder *builder, GObject *child, const char *type)
{
  OmarchyToolbar *self = OMARCHY_TOOLBAR (b);
  if (!GTK_IS_WIDGET (child))
    toolbar_parent_buildable->add_child (b, builder, child, type);
  else if (!g_strcmp0 (type, "center"))
    omarchy_toolbar_append_center (self, GTK_WIDGET (child));
  else if (!g_strcmp0 (type, "trailing"))
    omarchy_toolbar_append_trailing (self, GTK_WIDGET (child));
  else
    omarchy_toolbar_append_leading (self, GTK_WIDGET (child));
}

static void
toolbar_buildable_init (GtkBuildableIface *iface)
{
  toolbar_parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = toolbar_add_child;
}

static void
omarchy_toolbar_class_init (OmarchyToolbarClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = toolbar_dispose;
  oc->set_property = toolbar_set_property;
  oc->get_property = toolbar_get_property;
  wc->measure = toolbar_measure;
  wc->size_allocate = toolbar_allocate;
  wc->snapshot = toolbar_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyToolbar:overflow:
   *
   * Show the overflow "…" icon button after the trailing slot.
   */
  tb_props[TB_PROP_OVERFLOW] = g_param_spec_boolean ("overflow", NULL, NULL, FALSE, f);
  /**
   * OmarchyToolbar:overflow-cursor:
   *
   * Force the cursor look on the overflow button (Qt's overflowHasCursor).
   */
  tb_props[TB_PROP_OVERFLOW_CURSOR] = g_param_spec_boolean ("overflow-cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyToolbar:divided:
   *
   * The 1 px `track` rule along the bottom.
   */
  tb_props[TB_PROP_DIVIDED] = g_param_spec_boolean ("divided", NULL, NULL, TRUE, f);
  g_object_class_install_properties (oc, TB_N_PROPS, tb_props);
  /**
   * OmarchyToolbar::overflow-triggered:
   *
   * The overflow button was clicked (or Return/Space on it).
   */
  tb_signals[TB_OVERFLOW_TRIGGERED] = g_signal_new ("overflow-triggered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                                    NULL, NULL, NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-toolbar");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_TOOLBAR);
}

/**
 * omarchy_toolbar_new:
 *
 * Returns: (transfer floating): an empty toolbar
 */
GtkWidget *
omarchy_toolbar_new (void)
{
  return g_object_new (OMARCHY_TYPE_TOOLBAR, NULL);
}

static void
toolbar_append (GtkWidget *box, GtkWidget *child)
{
  gtk_widget_set_valign (child, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), child);
}

/**
 * omarchy_toolbar_append_leading:
 * @self: the toolbar
 * @child: (transfer floating): an item for the left slot
 */
void
omarchy_toolbar_append_leading (OmarchyToolbar *self, GtkWidget *child)
{
  toolbar_append (self->leading, child);
}

/**
 * omarchy_toolbar_append_center:
 * @self: the toolbar
 * @child: (transfer floating): an item for the slot centered on the bar
 */
void
omarchy_toolbar_append_center (OmarchyToolbar *self, GtkWidget *child)
{
  toolbar_append (self->center, child);
}

/**
 * omarchy_toolbar_append_trailing:
 * @self: the toolbar
 * @child: (transfer floating): an item for the right slot
 */
void
omarchy_toolbar_append_trailing (OmarchyToolbar *self, GtkWidget *child)
{
  toolbar_append (self->trailing, child);
}

/**
 * omarchy_toolbar_append_separator:
 * @self: the toolbar
 *
 * Adds an #OmarchyToolbarSeparator to the leading slot.
 */
void
omarchy_toolbar_append_separator (OmarchyToolbar *self)
{
  toolbar_append (self->leading, omarchy_toolbar_separator_new ());
}

/**
 * omarchy_toolbar_remove:
 * @self: the toolbar
 * @child: an item in any slot
 */
void
omarchy_toolbar_remove (OmarchyToolbar *self, GtkWidget *child)
{
  GtkWidget *parent = gtk_widget_get_parent (child);
  if (parent == self->leading || parent == self->center || parent == self->trailing)
    gtk_box_remove (GTK_BOX (parent), child);
}

gboolean
omarchy_toolbar_get_overflow (OmarchyToolbar *self)
{
  return self->overflow;
}

void
omarchy_toolbar_set_overflow (OmarchyToolbar *self, gboolean overflow)
{
  if (self->overflow == !!overflow)
    return;
  self->overflow = !!overflow;
  gtk_widget_set_visible (self->more, self->overflow);
  g_object_notify_by_pspec (G_OBJECT (self), tb_props[TB_PROP_OVERFLOW]);
}

gboolean
omarchy_toolbar_get_overflow_cursor (OmarchyToolbar *self)
{
  return self->overflow_cursor;
}

void
omarchy_toolbar_set_overflow_cursor (OmarchyToolbar *self, gboolean cursor)
{
  if (self->overflow_cursor == !!cursor)
    return;
  self->overflow_cursor = !!cursor;
  omarchy_icon_button_set_cursor (OMARCHY_ICON_BUTTON (self->more), self->overflow_cursor);
  g_object_notify_by_pspec (G_OBJECT (self), tb_props[TB_PROP_OVERFLOW_CURSOR]);
}

OMARCHY_BOOL_ACCESSORS (OmarchyToolbar, toolbar, divided, tb_props, TB_PROP_DIVIDED, TRUE)

/**
 * omarchy_toolbar_get_overflow_button:
 * @self: the toolbar
 *
 * Returns: (transfer none): the overflow "…" button (to anchor a menu to)
 */
GtkWidget *
omarchy_toolbar_get_overflow_button (OmarchyToolbar *self)
{
  return self->more;
}

/* ================================================================================== Tabs === */

/**
 * OmarchyTabs:
 *
 * Text tabs `xxs` apart over a 1 px `track` rule the full width of the control. Each tab is
 * `⌈label⌉ + 2 × controlPaddingX` wide and `line(body) + 2 × controlPaddingY` tall, with small
 * corners. The selected tab is bold in the selected text color, with an `xxs` accent underline
 * that slides between tabs (motion.press, easeOut). A tab under the pointer, the keyboard cursor
 * (#OmarchyTabs:cursor-index) or the focus takes the hover fill and full foreground; the rest are
 * dim. For a small choice inside a form, use #OmarchyButtonGroup.
 *
 * Keyboard (focusable): Left/Right or h/l move the selection, Home/End jump. Choosing sets
 * #OmarchyTabs:current-index and emits ::activated.
 */
struct _OmarchyTabs {
  GtkWidget parent_instance;
  GPtrArray *labels;
  int current, cursor, hover;
  OmarchyNavSlide underline;
};

enum { TA_PROP_0, TA_PROP_CURRENT_INDEX, TA_PROP_CURSOR_INDEX, TA_N_PROPS };
static GParamSpec *ta_props[TA_N_PROPS];
enum { TA_ACTIVATED, TA_N_SIGNALS };
static guint ta_signals[TA_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyTabs, omarchy_tabs, GTK_TYPE_WIDGET)

static int
tab_width (OmarchyTabs *self, int i)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return omarchy_nav_text_width (GTK_WIDGET (self), g_ptr_array_index (self->labels, i), t->body, i == self->current)
         + t->control_padding_x * 2;
}

/* The tabs' left edges and widths, mirrored in RTL. */
static void
tabs_geometry (OmarchyTabs *self, int *x, int *w)
{
  const OmarchyTokens *t = omarchy_tokens ();
  gboolean rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;
  int W = gtk_widget_get_width (GTK_WIDGET (self)), at = 0;
  for (guint i = 0; i < self->labels->len; i++) {
    w[i] = tab_width (self, (int) i);
    x[i] = rtl ? W - at - w[i] : at;
    at += w[i] + t->xxs;
  }
}

static int
tabs_height (void)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return omarchy_line_height (t->body) + t->control_padding_y * 2;
}

static int
tabs_index_at (OmarchyTabs *self, double px, double py)
{
  int n = (int) self->labels->len;
  if (n == 0 || py < 0 || py >= tabs_height ())
    return -1;
  int *x = g_newa (int, n), *w = g_newa (int, n);
  tabs_geometry (self, x, w);
  for (int i = 0; i < n; i++)
    if (px >= x[i] && px < x[i] + w[i])
      return i;
  return -1;
}

static void
tabs_measure (GtkWidget *wd, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyTabs *self = OMARCHY_TABS (wd);
  if (o == GTK_ORIENTATION_VERTICAL) {
    *min = *nat = tabs_height ();
    return;
  }
  int sum = 0;
  for (guint i = 0; i < self->labels->len; i++)
    sum += tab_width (self, (int) i) + (i ? omarchy_tokens ()->xxs : 0);
  *min = *nat = sum;
}

static void
tabs_draw_fill (GtkSnapshot *s, const graphene_rect_t *r, float radius, const GdkRGBA *c)
{
  if (c->alpha <= 0 || r->size.width <= 0 || r->size.height <= 0)
    return;
  float rad = CLAMP (radius, 0, MIN (r->size.width, r->size.height) / 2);
  if (rad <= 0) {
    gtk_snapshot_append_color (s, c, r);
    return;
  }
  const graphene_size_t top = { rad, rad }, flat = { 0, 0 };
  GskRoundedRect rr;
  gsk_rounded_rect_init (&rr, r, &top, &top, &flat, &flat);
  gtk_snapshot_push_rounded_clip (s, &rr);
  gtk_snapshot_append_color (s, c, r);
  gtk_snapshot_pop (s);
}

static void
tabs_snapshot (GtkWidget *wd, GtkSnapshot *s)
{
  OmarchyTabs *self = OMARCHY_TABS (wd);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (wd), H = gtk_widget_get_height (wd);
  int n = (int) self->labels->len;
  if (n == 0) {
    gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, H - 1, W, 1));
    return;
  }
  int *x = g_newa (int, n), *w = g_newa (int, n);
  tabs_geometry (self, x, w);
  int th = tabs_height (), line = omarchy_line_height (p->body);
  float r_small = omarchy_radius_small ();
  gboolean focus = omarchy_widget_focus_shown (wd), pressed = omarchy_widget_pressed (wd);
  for (int i = 0; i < n; i++) {
    gboolean selected = i == self->current;
    gboolean hot = i == self->hover || i == self->cursor || (focus && selected);
    graphene_rect_t r = GRAPHENE_RECT_INIT (x[i], 0, w[i], th);
    if (pressed && i == self->hover)
      tabs_draw_fill (s, &r, r_small, &p->pressed);
    else if (hot)
      tabs_draw_fill (s, &r, r_small, &p->hover.fill);
    g_autoptr (PangoLayout) l = omarchy_layout (wd, g_ptr_array_index (self->labels, i), p->body, selected);
    float tw = (float) omarchy_nav_text_advance (wd, g_ptr_array_index (self->labels, i), p->body, selected);
    const GdkRGBA *c = selected ? &p->selected.text : hot ? &p->foreground : &p->text_dim;
    omarchy_draw_layout (s, l, x[i] + omarchy_nav_center (w[i], tw), omarchy_nav_center (th, line), c);
  }
  gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, H - 1, W, 1));
  if (self->current >= 0 && self->current < n) {
    /* The underline, between the tabs it slides from and to. */
    double v = CLAMP (self->underline.set ? self->underline.value : self->current, 0, n - 1);
    int a = (int) floor (v), b = MIN (n - 1, a + 1);
    double f = v - a;
    float ux = x[a] + (x[b] - x[a]) * f, uw = w[a] + (w[b] - w[a]) * f;
    gtk_snapshot_append_color (s, &p->accent, &GRAPHENE_RECT_INIT (ux, H - p->xxs, uw, p->xxs));
  }
}

static void
tabs_motion (GtkEventControllerMotion *c, double x, double y, OmarchyTabs *self)
{
  int i = tabs_index_at (self, x, y);
  if (i != self->hover) {
    self->hover = i;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
tabs_leave (GtkEventControllerMotion *c, OmarchyTabs *self)
{
  self->hover = -1;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
tabs_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyTabs *self = OMARCHY_TABS (w);
  int i = tabs_index_at (self, x, y);
  if (button != GDK_BUTTON_PRIMARY || i < 0)
    return;
  gtk_widget_grab_focus (w);
  omarchy_tabs_select (self, i);
}

static gboolean
tabs_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyTabs *self = OMARCHY_TABS (w);
  guint k = omarchy_reading_key (w, keyval, TRUE);
  if (k == GDK_KEY_Left || k == GDK_KEY_KP_Left)
    omarchy_tabs_select (self, self->current - 1);
  else if (k == GDK_KEY_Right || k == GDK_KEY_KP_Right)
    omarchy_tabs_select (self, self->current + 1);
  else if (k == GDK_KEY_Home || k == GDK_KEY_KP_Home)
    omarchy_tabs_select (self, 0);
  else if (k == GDK_KEY_End || k == GDK_KEY_KP_End)
    omarchy_tabs_select (self, (int) self->labels->len - 1);
  else
    return FALSE;
  return TRUE;
}

static void
tabs_map (GtkWidget *w)
{
  OmarchyTabs *self = OMARCHY_TABS (w);
  GTK_WIDGET_CLASS (omarchy_tabs_parent_class)->map (w);
  self->underline.set = FALSE;
  omarchy_nav_slide (w, &self->underline, self->current);
}

static void
omarchy_tabs_init (OmarchyTabs *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->labels = g_ptr_array_new_with_free_func (g_free);
  self->cursor = -1;
  self->hover = -1;
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, tabs_clicked);
  omarchy_widget_add_keys (w, tabs_key);
  GtkEventController *motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "enter", G_CALLBACK (tabs_motion), self);
  g_signal_connect (motion, "motion", G_CALLBACK (tabs_motion), self);
  g_signal_connect (motion, "leave", G_CALLBACK (tabs_leave), self);
  gtk_widget_add_controller (w, motion);
}

static void
tabs_dispose (GObject *o)
{
  OmarchyTabs *self = OMARCHY_TABS (o);
  omarchy_nav_slide_stop (GTK_WIDGET (o), &self->underline);
  G_OBJECT_CLASS (omarchy_tabs_parent_class)->dispose (o);
}

static void
tabs_finalize (GObject *o)
{
  g_ptr_array_unref (OMARCHY_TABS (o)->labels);
  G_OBJECT_CLASS (omarchy_tabs_parent_class)->finalize (o);
}

static void
tabs_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyTabs *self = OMARCHY_TABS (o);
  switch (id) {
  case TA_PROP_CURRENT_INDEX: omarchy_tabs_set_current_index (self, g_value_get_int (v)); break;
  case TA_PROP_CURSOR_INDEX: omarchy_tabs_set_cursor_index (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
tabs_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyTabs *self = OMARCHY_TABS (o);
  switch (id) {
  case TA_PROP_CURRENT_INDEX: g_value_set_int (v, self->current); break;
  case TA_PROP_CURSOR_INDEX: g_value_set_int (v, self->cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_tabs_class_init (OmarchyTabsClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = tabs_dispose;
  oc->finalize = tabs_finalize;
  oc->set_property = tabs_set_property;
  oc->get_property = tabs_get_property;
  wc->measure = tabs_measure;
  wc->snapshot = tabs_snapshot;
  wc->map = tabs_map;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyTabs:current-index:
   *
   * The selected tab.
   */
  ta_props[TA_PROP_CURRENT_INDEX] = g_param_spec_int ("current-index", NULL, NULL, -1, G_MAXINT, 0, f);
  /**
   * OmarchyTabs:cursor-index:
   *
   * The tab a panel's keyboard cursor is on (the hover look), or -1.
   */
  ta_props[TA_PROP_CURSOR_INDEX] = g_param_spec_int ("cursor-index", NULL, NULL, -1, G_MAXINT, -1, f);
  g_object_class_install_properties (oc, TA_N_PROPS, ta_props);
  /**
   * OmarchyTabs::activated:
   * @self: the tabs
   * @index: the tab chosen
   *
   * A click or a key chose another tab.
   */
  ta_signals[TA_ACTIVATED] = g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                           NULL, G_TYPE_NONE, 1, G_TYPE_INT);
  gtk_widget_class_set_css_name (wc, "omarchy-tabs");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_TAB_LIST);
}

/**
 * omarchy_tabs_new:
 * @labels: (nullable) (array zero-terminated=1): the tabs' labels
 *
 * Returns: (transfer floating): tabs, the first selected
 */
GtkWidget *
omarchy_tabs_new (const char *const *labels)
{
  OmarchyTabs *self = g_object_new (OMARCHY_TYPE_TABS, NULL);
  for (const char *const *l = labels; l && *l; l++)
    omarchy_tabs_append (self, *l);
  return GTK_WIDGET (self);
}

void
omarchy_tabs_append (OmarchyTabs *self, const char *label)
{
  g_ptr_array_add (self->labels, g_strdup (label ? label : ""));
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
omarchy_tabs_remove_all (OmarchyTabs *self)
{
  g_ptr_array_set_size (self->labels, 0);
  self->hover = -1;
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

guint
omarchy_tabs_get_n_tabs (OmarchyTabs *self)
{
  return self->labels->len;
}

/**
 * omarchy_tabs_get_label:
 * @self: the tabs
 * @index: a tab
 *
 * Returns: (nullable): its label
 */
const char *
omarchy_tabs_get_label (OmarchyTabs *self, guint index)
{
  return index < self->labels->len ? g_ptr_array_index (self->labels, index) : NULL;
}

int
omarchy_tabs_get_current_index (OmarchyTabs *self)
{
  return self->current;
}

void
omarchy_tabs_set_current_index (OmarchyTabs *self, int index)
{
  if (self->current == index)
    return;
  self->current = index;
  omarchy_nav_slide (GTK_WIDGET (self), &self->underline, MAX (0, index));
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), ta_props[TA_PROP_CURRENT_INDEX]);
}

int
omarchy_tabs_get_cursor_index (OmarchyTabs *self)
{
  return self->cursor;
}

void
omarchy_tabs_set_cursor_index (OmarchyTabs *self, int index)
{
  if (self->cursor == index)
    return;
  self->cursor = index;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), ta_props[TA_PROP_CURSOR_INDEX]);
}

/**
 * omarchy_tabs_select:
 * @self: the tabs
 * @index: a tab (clamped to the tabs)
 *
 * Chooses a tab as a click does: sets #OmarchyTabs:current-index and emits ::activated when it
 * changed.
 */
void
omarchy_tabs_select (OmarchyTabs *self, int index)
{
  int n = (int) self->labels->len;
  if (n == 0)
    return;
  index = CLAMP (index, 0, n - 1);
  if (index == self->current)
    return;
  omarchy_tabs_set_current_index (self, index);
  g_signal_emit (self, ta_signals[TA_ACTIVATED], 0, index);
}

/* ============================================================================== IconRail === */

/**
 * OmarchyIconRail:
 *
 * A vertical rail of icon tabs, as an app's side-panel switcher: square items
 * `controlHeight + 2 × sm` on a side, `sm` apart and `sm` inside the rail, glyphs `iconLarge`
 * centered by ink, `radius` corners, a 1 px `track` rule down the trailing edge. The selected
 * glyph is the accent, with an accent bar (`xxs` × `iconLarge`) at the leading edge that slides
 * between items. Under the pointer, the keyboard cursor (#OmarchyIconRail:cursor-index) or the
 * focus an item takes the hover fill; the rest are dim.
 *
 * Keyboard (focusable): Up/Down or k/j move the selection, Home/End jump. Choosing sets
 * #OmarchyIconRail:current-index and emits ::activated.
 */
struct _OmarchyIconRail {
  GtkWidget parent_instance;
  GPtrArray *glyphs, *labels;
  int current, cursor, hover;
  OmarchyNavSlide bar;
};

enum { IR_PROP_0, IR_PROP_CURRENT_INDEX, IR_PROP_CURSOR_INDEX, IR_PROP_ITEM_SIZE, IR_N_PROPS };
static GParamSpec *ir_props[IR_N_PROPS];
enum { IR_ACTIVATED, IR_N_SIGNALS };
static guint ir_signals[IR_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyIconRail, omarchy_icon_rail, GTK_TYPE_WIDGET)

int
omarchy_icon_rail_get_item_size (OmarchyIconRail *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return t->control_height + t->sm * 2;
}

static int
rail_item_y (OmarchyIconRail *self, int i)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return t->sm + i * (omarchy_icon_rail_get_item_size (self) + t->sm);
}

static int
rail_index_at (OmarchyIconRail *self, double px, double py)
{
  const OmarchyTokens *t = omarchy_tokens ();
  int item = omarchy_icon_rail_get_item_size (self);
  gboolean rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;
  int x0 = rtl ? gtk_widget_get_width (GTK_WIDGET (self)) - t->sm - item : t->sm;
  if (px < x0 || px >= x0 + item || py < t->sm)
    return -1;
  int i = (int) ((py - t->sm) / (item + t->sm));
  if (i >= (int) self->glyphs->len || py >= rail_item_y (self, i) + item)
    return -1;
  return i;
}

static void
rail_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyIconRail *self = OMARCHY_ICON_RAIL (w);
  const OmarchyTokens *t = omarchy_tokens ();
  int item = omarchy_icon_rail_get_item_size (self), n = (int) self->glyphs->len;
  if (o == GTK_ORIENTATION_HORIZONTAL)
    *min = *nat = item + t->sm * 2 + 1;
  else
    *min = *nat = n * item + MAX (0, n - 1) * t->sm + t->sm * 2;
}

static void
rail_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyIconRail *self = OMARCHY_ICON_RAIL (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  int item = omarchy_icon_rail_get_item_size (self), n = (int) self->glyphs->len;
  float x0 = rtl ? W - p->sm - item : p->sm;
  gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (rtl ? 0 : W - 1, 0, 1, H));
  gboolean focus = omarchy_widget_focus_shown (w), pressed = omarchy_widget_pressed (w);
  for (int i = 0; i < n; i++) {
    gboolean selected = i == self->current;
    gboolean hot = i == self->hover || i == self->cursor || (focus && selected);
    graphene_rect_t r = GRAPHENE_RECT_INIT (x0, rail_item_y (self, i), item, item);
    if (pressed && i == self->hover)
      omarchy_draw_fill (s, &r, p->radius, &p->pressed);
    else if (hot)
      omarchy_draw_fill (s, &r, p->radius, &p->hover.fill);
    const GdkRGBA *c = selected ? &p->accent : hot ? &p->foreground : &p->text_dim;
    omarchy_nav_draw_glyph (w, s, g_ptr_array_index (self->glyphs, i), p->icon_large, r.origin.x, r.origin.y, item, item, c);
  }
  if (self->current >= 0 && self->current < n) {
    double v = self->bar.set ? self->bar.value : self->current;
    float y = p->sm + v * (item + p->sm) + roundf ((item - p->icon_large) / 2.0f);
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (rtl ? W - p->xxs : 0, y, p->xxs, p->icon_large), MIN (1, p->radius), &p->accent);
  }
}

static void
rail_motion (GtkEventControllerMotion *c, double x, double y, OmarchyIconRail *self)
{
  int i = rail_index_at (self, x, y);
  if (i != self->hover) {
    self->hover = i;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
rail_leave (GtkEventControllerMotion *c, OmarchyIconRail *self)
{
  self->hover = -1;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
rail_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyIconRail *self = OMARCHY_ICON_RAIL (w);
  int i = rail_index_at (self, x, y);
  if (button != GDK_BUTTON_PRIMARY || i < 0)
    return;
  gtk_widget_grab_focus (w);
  omarchy_icon_rail_select (self, i);
}

static gboolean
rail_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyIconRail *self = OMARCHY_ICON_RAIL (w);
  if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up || keyval == GDK_KEY_k)
    omarchy_icon_rail_select (self, self->current - 1);
  else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down || keyval == GDK_KEY_j)
    omarchy_icon_rail_select (self, self->current + 1);
  else if (keyval == GDK_KEY_Home || keyval == GDK_KEY_KP_Home)
    omarchy_icon_rail_select (self, 0);
  else if (keyval == GDK_KEY_End || keyval == GDK_KEY_KP_End)
    omarchy_icon_rail_select (self, (int) self->glyphs->len - 1);
  else
    return FALSE;
  return TRUE;
}

static void
rail_map (GtkWidget *w)
{
  OmarchyIconRail *self = OMARCHY_ICON_RAIL (w);
  GTK_WIDGET_CLASS (omarchy_icon_rail_parent_class)->map (w);
  self->bar.set = FALSE;
  omarchy_nav_slide (w, &self->bar, self->current);
}

static void
omarchy_icon_rail_init (OmarchyIconRail *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->glyphs = g_ptr_array_new_with_free_func (g_free);
  self->labels = g_ptr_array_new_with_free_func (g_free);
  self->cursor = -1;
  self->hover = -1;
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, rail_clicked);
  omarchy_widget_add_keys (w, rail_key);
  GtkEventController *motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "enter", G_CALLBACK (rail_motion), self);
  g_signal_connect (motion, "motion", G_CALLBACK (rail_motion), self);
  g_signal_connect (motion, "leave", G_CALLBACK (rail_leave), self);
  gtk_widget_add_controller (w, motion);
}

static void
rail_dispose (GObject *o)
{
  OmarchyIconRail *self = OMARCHY_ICON_RAIL (o);
  omarchy_nav_slide_stop (GTK_WIDGET (o), &self->bar);
  G_OBJECT_CLASS (omarchy_icon_rail_parent_class)->dispose (o);
}

static void
rail_finalize (GObject *o)
{
  OmarchyIconRail *self = OMARCHY_ICON_RAIL (o);
  g_ptr_array_unref (self->glyphs);
  g_ptr_array_unref (self->labels);
  G_OBJECT_CLASS (omarchy_icon_rail_parent_class)->finalize (o);
}

static void
rail_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyIconRail *self = OMARCHY_ICON_RAIL (o);
  switch (id) {
  case IR_PROP_CURRENT_INDEX: omarchy_icon_rail_set_current_index (self, g_value_get_int (v)); break;
  case IR_PROP_CURSOR_INDEX: omarchy_icon_rail_set_cursor_index (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
rail_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyIconRail *self = OMARCHY_ICON_RAIL (o);
  switch (id) {
  case IR_PROP_CURRENT_INDEX: g_value_set_int (v, self->current); break;
  case IR_PROP_CURSOR_INDEX: g_value_set_int (v, self->cursor); break;
  case IR_PROP_ITEM_SIZE: g_value_set_int (v, omarchy_icon_rail_get_item_size (self)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_icon_rail_class_init (OmarchyIconRailClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = rail_dispose;
  oc->finalize = rail_finalize;
  oc->set_property = rail_set_property;
  oc->get_property = rail_get_property;
  wc->measure = rail_measure;
  wc->snapshot = rail_snapshot;
  wc->map = rail_map;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  ir_props[IR_PROP_CURRENT_INDEX] = g_param_spec_int ("current-index", NULL, NULL, -1, G_MAXINT, 0, f);
  /**
   * OmarchyIconRail:cursor-index:
   *
   * The item a panel's keyboard cursor is on (the hover look), or -1.
   */
  ir_props[IR_PROP_CURSOR_INDEX] = g_param_spec_int ("cursor-index", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyIconRail:item-size:
   *
   * An item's side: `controlHeight + 2 × sm`.
   */
  ir_props[IR_PROP_ITEM_SIZE] = g_param_spec_int ("item-size", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (oc, IR_N_PROPS, ir_props);
  /**
   * OmarchyIconRail::activated:
   * @self: the iconrail
   * @index: the item chosen
   *
   * A click or a key chose another item.
   */
  ir_signals[IR_ACTIVATED] = g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                           NULL, G_TYPE_NONE, 1, G_TYPE_INT);
  gtk_widget_class_set_css_name (wc, "omarchy-icon-rail");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_TAB_LIST);
}

/**
 * omarchy_icon_rail_new:
 *
 * Returns: (transfer floating): an empty rail; add items with omarchy_icon_rail_append()
 */
GtkWidget *
omarchy_icon_rail_new (void)
{
  return g_object_new (OMARCHY_TYPE_ICON_RAIL, NULL);
}

/**
 * omarchy_icon_rail_append:
 * @self: the rail
 * @glyph: the item's Nerd Font glyph
 * @label: (nullable): what it switches to ("Search"), for assistive technology
 */
void
omarchy_icon_rail_append (OmarchyIconRail *self, const char *glyph, const char *label)
{
  g_ptr_array_add (self->glyphs, g_strdup (glyph ? glyph : ""));
  g_ptr_array_add (self->labels, g_strdup (label ? label : ""));
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
omarchy_icon_rail_remove_all (OmarchyIconRail *self)
{
  g_ptr_array_set_size (self->glyphs, 0);
  g_ptr_array_set_size (self->labels, 0);
  self->hover = -1;
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

guint
omarchy_icon_rail_get_n_items (OmarchyIconRail *self)
{
  return self->glyphs->len;
}

/**
 * omarchy_icon_rail_get_label:
 * @self: the rail
 * @index: an item
 *
 * Returns: (nullable): its label
 */
const char *
omarchy_icon_rail_get_label (OmarchyIconRail *self, guint index)
{
  return index < self->labels->len ? g_ptr_array_index (self->labels, index) : NULL;
}

int
omarchy_icon_rail_get_current_index (OmarchyIconRail *self)
{
  return self->current;
}

void
omarchy_icon_rail_set_current_index (OmarchyIconRail *self, int index)
{
  if (self->current == index)
    return;
  self->current = index;
  omarchy_nav_slide (GTK_WIDGET (self), &self->bar, MAX (0, index));
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), ir_props[IR_PROP_CURRENT_INDEX]);
}

int
omarchy_icon_rail_get_cursor_index (OmarchyIconRail *self)
{
  return self->cursor;
}

void
omarchy_icon_rail_set_cursor_index (OmarchyIconRail *self, int index)
{
  if (self->cursor == index)
    return;
  self->cursor = index;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), ir_props[IR_PROP_CURSOR_INDEX]);
}

/**
 * omarchy_icon_rail_select:
 * @self: the rail
 * @index: an item (clamped to the items)
 *
 * Chooses an item as a click does: sets #OmarchyIconRail:current-index and emits ::activated
 * when it changed.
 */
void
omarchy_icon_rail_select (OmarchyIconRail *self, int index)
{
  int n = (int) self->glyphs->len;
  if (n == 0)
    return;
  index = CLAMP (index, 0, n - 1);
  if (index == self->current)
    return;
  omarchy_icon_rail_set_current_index (self, index);
  g_signal_emit (self, ir_signals[IR_ACTIVATED], 0, index);
}
