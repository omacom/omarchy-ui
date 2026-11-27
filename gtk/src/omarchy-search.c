/* omarchy-search.c: SearchList (SearchList.qml, its filter line FilterInput.qml) and the
 * SearchItem it lists. */
#include "omarchy-private.h"

#include <math.h>
#include <string.h>

#define SECTION "results"

/* ============================================================================ SearchItem === */

/**
 * OmarchySearchItem:
 *
 * One result of a #OmarchySearchList: its glyph, bold title, dim subtitle and dim value in the
 * row, and the detail text the detail pane shows while the cursor is on it.
 */
struct _OmarchySearchItem {
  GObject parent_instance;
  char *title, *subtitle, *glyph, *value, *detail;
};

enum { I_PROP_0, I_PROP_TITLE, I_PROP_SUBTITLE, I_PROP_GLYPH, I_PROP_VALUE, I_PROP_DETAIL, I_N_PROPS };
static GParamSpec *i_props[I_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchySearchItem, omarchy_search_item, G_TYPE_OBJECT)

static char **
item_field (OmarchySearchItem *self, guint id)
{
  switch (id) {
  case I_PROP_TITLE: return &self->title;
  case I_PROP_SUBTITLE: return &self->subtitle;
  case I_PROP_GLYPH: return &self->glyph;
  case I_PROP_VALUE: return &self->value;
  case I_PROP_DETAIL: return &self->detail;
  default: return NULL;
  }
}

static void
item_set (OmarchySearchItem *self, guint id, const char *v)
{
  char **f = item_field (self, id);
  if (!g_strcmp0 (*f, v ? v : ""))
    return;
  g_free (*f);
  *f = g_strdup (v ? v : "");
  g_object_notify_by_pspec (G_OBJECT (self), i_props[id]);
}

static void
omarchy_search_item_init (OmarchySearchItem *self)
{
  self->title = g_strdup ("");
  self->subtitle = g_strdup ("");
  self->glyph = g_strdup ("");
  self->value = g_strdup ("");
  self->detail = g_strdup ("");
}

static void
item_finalize (GObject *o)
{
  OmarchySearchItem *self = OMARCHY_SEARCH_ITEM (o);
  for (guint id = I_PROP_TITLE; id < I_N_PROPS; id++)
    g_free (*item_field (self, id));
  G_OBJECT_CLASS (omarchy_search_item_parent_class)->finalize (o);
}

static void
item_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  if (id > I_PROP_0 && id < I_N_PROPS)
    item_set (OMARCHY_SEARCH_ITEM (o), id, g_value_get_string (v));
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
item_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  if (id > I_PROP_0 && id < I_N_PROPS)
    g_value_set_string (v, *item_field (OMARCHY_SEARCH_ITEM (o), id));
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
omarchy_search_item_class_init (OmarchySearchItemClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  oc->finalize = item_finalize;
  oc->set_property = item_set_property;
  oc->get_property = item_get_property;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchySearchItem:title:
   *
   * The row's bold title, and the detail pane's heading.
   */
  i_props[I_PROP_TITLE] = g_param_spec_string ("title", NULL, NULL, "", f);
  /**
   * OmarchySearchItem:subtitle:
   *
   * The dim line under the title.
   */
  i_props[I_PROP_SUBTITLE] = g_param_spec_string ("subtitle", NULL, NULL, "", f);
  /**
   * OmarchySearchItem:glyph:
   *
   * The leading Nerd Font glyph.
   */
  i_props[I_PROP_GLYPH] = g_param_spec_string ("glyph", NULL, NULL, "", f);
  /**
   * OmarchySearchItem:value:
   *
   * The dim text at the row's end.
   */
  i_props[I_PROP_VALUE] = g_param_spec_string ("value", NULL, NULL, "", f);
  /**
   * OmarchySearchItem:detail:
   *
   * The detail pane's text while the cursor is on the item.
   */
  i_props[I_PROP_DETAIL] = g_param_spec_string ("detail", NULL, NULL, "", f);
  g_object_class_install_properties (oc, I_N_PROPS, i_props);
}

/**
 * omarchy_search_item_new:
 * @title: (nullable): the title
 *
 * Returns: (transfer full): a search item
 */
OmarchySearchItem *
omarchy_search_item_new (const char *title)
{
  return g_object_new (OMARCHY_TYPE_SEARCH_ITEM, "title", title ? title : "", NULL);
}

#define ITEM_ACCESSORS(field, PROP)                                                           \
  const char *omarchy_search_item_get_##field (OmarchySearchItem *self)                       \
  {                                                                                           \
    g_return_val_if_fail (OMARCHY_IS_SEARCH_ITEM (self), NULL);                               \
    return self->field;                                                                       \
  }                                                                                           \
  void omarchy_search_item_set_##field (OmarchySearchItem *self, const char *v)               \
  {                                                                                           \
    g_return_if_fail (OMARCHY_IS_SEARCH_ITEM (self));                                         \
    item_set (self, PROP, v);                                                                 \
  }

ITEM_ACCESSORS (title, I_PROP_TITLE)
ITEM_ACCESSORS (subtitle, I_PROP_SUBTITLE)
ITEM_ACCESSORS (glyph, I_PROP_GLYPH)
ITEM_ACCESSORS (value, I_PROP_VALUE)
ITEM_ACCESSORS (detail, I_PROP_DETAIL)

/* ============================================================================ SearchList === */

/**
 * OmarchySearchList:
 *
 * The type-ahead overlay the shell builds seven times (clipboard, emojis, the menu, the app
 * grid): a filter line, the matching rows under a keyboard cursor, an optional detail pane
 * beside them, and an empty state when nothing matches. Drawn on the menu surface.
 *
 * Filtering is the caller's: on #OmarchySearchList:query changes, replace the items with what
 * matches (omarchy_search_list_remove_all(), omarchy_search_list_append());
 * omarchy_search_list_matches() is the plain case-insensitive substring test.
 *
 * Keys (the list holds focus): printable keys and Space are typed into the query (so j, k
 * and x are text here), Backspace edits it, input-method text is committed to it; Up/Down,
 * Page Up/Down, Home and End move the cursor; Return and Enter emit ::activated; Delete
 * ::delete-requested; Escape clears the query, or emits ::close-requested when it is empty.
 * A new query puts the cursor back on the first row. The pointer moves the cursor through
 * the #OmarchyCursorModel's gate; a click activates.
 */
struct _OmarchySearchList {
  GtkWidget parent_instance;
  char *query, *placeholder, *empty_glyph, *empty_text, *no_matches;
  char *preedit;
  GPtrArray *items; /* OmarchySearchItem */
  GPtrArray *rows;  /* OmarchyListRow, weak (children of the scroll area) */
  gboolean show_detail;
  int detail_width; /* -1: 45% of the body */
  int last_cursor_index;
  GtkWidget *scroll, *empty, *detail;
  OmarchyCursorModel *cursor;
  GtkIMContext *im;
  GtkEventController *keys;
};

enum {
  L_PROP_0,
  L_PROP_QUERY,
  L_PROP_PLACEHOLDER_TEXT,
  L_PROP_SHOW_DETAIL,
  L_PROP_DETAIL,
  L_PROP_DETAIL_WIDTH,
  L_PROP_EMPTY_GLYPH,
  L_PROP_EMPTY_TEXT,
  L_PROP_NO_MATCHES_TEXT,
  L_PROP_N_ITEMS,
  L_PROP_CURSOR,
  L_PROP_CURSOR_INDEX,
  L_N_PROPS
};
static GParamSpec *l_props[L_N_PROPS];

enum { L_ACTIVATED, L_DELETE_REQUESTED, L_CLOSE_REQUESTED, L_N_SIGNALS };
static guint l_signals[L_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchySearchList, omarchy_search_list, GTK_TYPE_WIDGET)

/* ---- geometry ---------------------------------------------------------------------------- */

typedef struct {
  float in_l, in_t, in_r, in_b;
  int header_y, header_h;
  int body_x, body_y, body_w, body_h;
  int gap, list_w, pane_x, pane_w;
  gboolean detail_shown;
} Geom;

static const char *
placeholder_text (OmarchySearchList *self)
{
  return self->placeholder ? self->placeholder : omarchy_t ("common.search", "Search...");
}

static const char *
no_matches_text (OmarchySearchList *self)
{
  return self->no_matches ? self->no_matches : omarchy_t ("common.noMatchesFor", "No matches for “%1”");
}

static int
header_height (void)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return MAX (omarchy_space (34), t->heading + t->control_padding_y * 2);
}

static void
geom (OmarchySearchList *self, int width, int height, Geom *g)
{
  const OmarchyTokens *t = omarchy_tokens ();
  const OmarchyBorder *b = &t->menu.border;
  g->in_t = b->widths[0] + t->panel_padding;
  g->in_r = b->widths[1] + t->panel_padding;
  g->in_b = b->widths[2] + t->panel_padding;
  g->in_l = b->widths[3] + t->panel_padding;
  g->header_y = (int) g->in_t;
  g->header_h = header_height ();
  g->body_x = (int) g->in_l;
  g->body_y = g->header_y + g->header_h + t->md;
  g->body_w = MAX (0, width - (int) (g->in_l + g->in_r));
  g->body_h = MAX (0, height - (int) g->in_b - g->body_y);
  g->gap = t->xxl;
  g->detail_shown = self->show_detail && self->items->len > 0;
  int detail_w = self->detail_width >= 0 ? self->detail_width : (int) floor (g->body_w * 0.45);
  g->list_w = g->detail_shown ? MAX (0, g->body_w - detail_w - g->gap * 2 - 1) : g->body_w;
  g->pane_x = g->body_x + g->list_w + g->gap * 2 + 1;
  g->pane_w = MAX (0, g->body_x + g->body_w - g->pane_x);
}

/* ---- state ------------------------------------------------------------------------------- */

static void
sync_cursor_index (OmarchySearchList *self)
{
  int index = omarchy_search_list_get_cursor_index (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  if (index == self->last_cursor_index)
    return;
  self->last_cursor_index = index;
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_CURSOR_INDEX]);
}

static void
sync_empty (OmarchySearchList *self)
{
  OmarchyEmptyState *e = OMARCHY_EMPTY_STATE (self->empty);
  omarchy_empty_state_set_glyph (e, self->empty_glyph);
  if (self->query[0]) {
    g_autoptr (GString) s = g_string_new (no_matches_text (self));
    g_string_replace (s, "%1", self->query, 0);
    omarchy_empty_state_set_title (e, s->str);
  } else {
    omarchy_empty_state_set_title (e, self->empty_text);
  }
  gboolean none = self->items->len == 0;
  gtk_widget_set_visible (self->empty, none);
  gtk_widget_set_visible (self->scroll, !none);
  if (self->detail)
    gtk_widget_set_visible (self->detail, self->show_detail && !none);
}

/* Where the input method's candidates open: the end of the filter line. */
static void
place_im (OmarchySearchList *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  Geom g;
  geom (self, gtk_widget_get_width (w), gtk_widget_get_height (w), &g);
  g_autoptr (PangoLayout) l = omarchy_layout (w, self->query, omarchy_tokens ()->heading, FALSE);
  int tw;
  omarchy_layout_size (l, &tw, NULL);
  int lh = omarchy_line_height (omarchy_tokens ()->heading);
  GdkRectangle r = { g.body_x + MIN (tw, g.body_w), g.header_y + (g.header_h - lh) / 2, 1, lh };
  gtk_im_context_set_cursor_location (self->im, &r);
}

static void
row_clicked (OmarchyListRow *row, OmarchySearchList *self)
{
  int index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row), "omarchy-search-index"));
  gtk_widget_grab_focus (GTK_WIDGET (self));
  omarchy_cursor_model_set (self->cursor, SECTION, index);
  g_signal_emit (self, l_signals[L_ACTIVATED], 0, index);
}

static void
add_row (OmarchySearchList *self, OmarchySearchItem *item, int index)
{
  GtkWidget *row = omarchy_list_row_new (item->title);
  OmarchyListRow *r = OMARCHY_LIST_ROW (row);
  omarchy_list_row_set_glyph (r, item->glyph);
  omarchy_list_row_set_subtitle (r, item->subtitle);
  omarchy_list_row_set_value (r, item->value);
  /* The list holds focus; rows take the pointer only. */
  gtk_widget_set_focusable (row, FALSE);
  g_object_set_data (G_OBJECT (row), "omarchy-search-index", GINT_TO_POINTER (index));
  g_signal_connect_object (row, "clicked", G_CALLBACK (row_clicked), self, 0);
  omarchy_scroll_area_append (OMARCHY_SCROLL_AREA (self->scroll), row);
  omarchy_cursor_model_bind (self->cursor, row, SECTION, index);
  g_ptr_array_add (self->rows, row);
}

static void
items_changed (OmarchySearchList *self)
{
  /* CursorModel.settle: keep the cursor on a real row. */
  omarchy_cursor_model_set_count (self->cursor, SECTION, self->items->len);
  sync_empty (self);
  sync_cursor_index (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_N_ITEMS]);
}

static void
cursor_moved (OmarchyCursorModel *m, const char *section, int index, gboolean by_key, OmarchySearchList *self)
{
  if (by_key && index >= 0 && (guint) index < self->rows->len)
    omarchy_scroll_area_ensure_visible (OMARCHY_SCROLL_AREA (self->scroll), g_ptr_array_index (self->rows, index));
}

static void
cursor_activated (OmarchyCursorModel *m, const char *section, int index, OmarchySearchList *self)
{
  if (self->items->len > 0)
    g_signal_emit (self, l_signals[L_ACTIVATED], 0, index);
}

static void
cursor_delete (OmarchyCursorModel *m, const char *section, int index, OmarchySearchList *self)
{
  if (self->items->len > 0)
    g_signal_emit (self, l_signals[L_DELETE_REQUESTED], 0, index);
}

/* Typed text: the line only grows at its end; control characters are never text. */
static void
type_text (OmarchySearchList *self, const char *text)
{
  if (!text || !*text)
    return;
  g_autoptr (GString) s = g_string_new (self->query);
  for (const char *p = text; *p; p = g_utf8_next_char (p)) {
    gunichar c = g_utf8_get_char (p);
    if (c == '\n' || c == '\r' || c == '\t')
      c = ' ';
    if (g_unichar_iscntrl (c))
      continue;
    g_string_append_unichar (s, c);
  }
  omarchy_search_list_set_query (self, s->str);
}

static void
backspace (OmarchySearchList *self, gboolean word)
{
  if (!self->query[0])
    return;
  char *end = self->query + strlen (self->query);
  char *p = g_utf8_find_prev_char (self->query, end);
  if (word) {
    /* Ctrl+Backspace: back over spaces, then over the word. */
    while (p && p > self->query && g_unichar_isspace (g_utf8_get_char (p)))
      p = g_utf8_find_prev_char (self->query, p);
    while (p && p > self->query && !g_unichar_isspace (g_utf8_get_char (g_utf8_find_prev_char (self->query, p))))
      p = g_utf8_find_prev_char (self->query, p);
  }
  g_autofree char *next = g_strndup (self->query, p ? p - self->query : 0);
  omarchy_search_list_set_query (self, next);
}

static void
pasted (GObject *source, GAsyncResult *res, gpointer data)
{
  g_autoptr (GObject) self = data;
  g_autofree char *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source), res, NULL);
  if (text && gtk_widget_get_root (GTK_WIDGET (self)))
    type_text (OMARCHY_SEARCH_LIST (self), text);
}

static gboolean
list_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchySearchList *self = OMARCHY_SEARCH_LIST (w);
  if (keyval == GDK_KEY_Escape) {
    if (self->query[0])
      omarchy_search_list_set_query (self, "");
    else
      g_signal_emit (self, l_signals[L_CLOSE_REQUESTED], 0);
    return TRUE;
  }
  gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
  gboolean chord = (state & (GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK)) != 0;
  if (chord)
    return FALSE; /* shortcuts, never text */
  g_autofree char *typed = omarchy_cursor_model_printable (keyval, state);
  if (typed || ((keyval == GDK_KEY_space || keyval == GDK_KEY_KP_Space) && !ctrl)) {
    type_text (self, typed ? typed : " ");
    return TRUE;
  }
  if (keyval == GDK_KEY_BackSpace) {
    backspace (self, ctrl);
    return TRUE;
  }
  if (ctrl && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
    gdk_clipboard_read_text_async (gtk_widget_get_clipboard (w), NULL, pasted, g_object_ref (self));
    return TRUE;
  }
  if (ctrl && (keyval == GDK_KEY_u || keyval == GDK_KEY_U)) {
    omarchy_search_list_set_query (self, "");
    return TRUE;
  }
  /* Tab leaves for the next control (GTK's focus chain); the one section has nowhere to go. */
  if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab || keyval == GDK_KEY_KP_Tab)
    return FALSE;
  return omarchy_cursor_model_handle_key (self->cursor, keyval, state);
}

static void
im_commit (GtkIMContext *im, const char *text, OmarchySearchList *self)
{
  type_text (self, text);
}

static void
im_preedit (GtkIMContext *im, OmarchySearchList *self)
{
  g_free (self->preedit);
  gtk_im_context_get_preedit_string (im, &self->preedit, NULL, NULL);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
focus_enter (GtkEventControllerFocus *c, OmarchySearchList *self)
{
  gtk_im_context_focus_in (self->im);
  place_im (self);
}

static void
focus_leave (GtkEventControllerFocus *c, OmarchySearchList *self)
{
  gtk_im_context_focus_out (self->im);
  gtk_im_context_reset (self->im);
}

static void
pressed (GtkGestureClick *g, int n, double x, double y, OmarchySearchList *self)
{
  /* A click anywhere keeps typing going to the filter line. */
  gtk_widget_grab_focus (GTK_WIDGET (self));
}

/* ---- layout and drawing ------------------------------------------------------------------ */

static void
list_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchySearchList *self = OMARCHY_SEARCH_LIST (w);
  Geom g;
  geom (self, 0, 0, &g);
  /* Implicitly space(480) × space(360); a size request is the size (Qt's width and height). */
  int rw, rh;
  gtk_widget_get_size_request (w, &rw, &rh);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    int smin = 0, emin = 0;
    gtk_widget_measure (self->scroll, o, -1, &smin, NULL, NULL, NULL);
    gtk_widget_measure (self->empty, o, -1, &emin, NULL, NULL, NULL);
    *min = (int) (g.in_l + g.in_r) + MAX (smin, emin);
    *nat = MAX (*min, rw >= 0 ? rw : omarchy_space (480));
  } else {
    *min = g.body_y + (int) g.in_b;
    *nat = MAX (*min, rh >= 0 ? rh : omarchy_space (360));
  }
}

static void
list_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchySearchList *self = OMARCHY_SEARCH_LIST (w);
  Geom g;
  geom (self, width, height, &g);
  if (gtk_widget_get_visible (self->scroll)) {
    int smin;
    gtk_widget_measure (self->scroll, GTK_ORIENTATION_HORIZONTAL, -1, &smin, NULL, NULL, NULL);
    GtkAllocation a = { g.body_x, g.body_y, MAX (g.list_w, smin), g.body_h };
    gtk_widget_size_allocate (self->scroll, &a, -1);
  }
  if (gtk_widget_get_visible (self->empty)) {
    int emin, eh;
    gtk_widget_measure (self->empty, GTK_ORIENTATION_HORIZONTAL, -1, &emin, NULL, NULL, NULL);
    int ew = MAX (g.body_w, emin);
    gtk_widget_measure (self->empty, GTK_ORIENTATION_VERTICAL, ew, NULL, &eh, NULL, NULL);
    /* anchors.centerIn: the body */
    float cy = ((g.body_h % 2) ? (g.body_h + 1) / 2.0f : g.body_h / 2.0f) - ((eh % 2) ? (eh + 1) / 2.0f : eh / 2.0f);
    GtkAllocation a = { g.body_x, g.body_y + (int) cy, ew, eh };
    gtk_widget_size_allocate (self->empty, &a, -1);
  }
  if (self->detail && gtk_widget_get_visible (self->detail)) {
    int dmin, dh;
    gtk_widget_measure (self->detail, GTK_ORIENTATION_HORIZONTAL, -1, &dmin, NULL, NULL, NULL);
    int dw = MAX (g.pane_w, dmin);
    gtk_widget_measure (self->detail, GTK_ORIENTATION_VERTICAL, dw, NULL, &dh, NULL, NULL);
    GtkAllocation a = { g.pane_x, g.body_y, dw, dh };
    gtk_widget_size_allocate (self->detail, &a, -1);
  }
  place_im (self);
}

static void
draw_filter_line (OmarchySearchList *self, GtkSnapshot *s, const Geom *g)
{
  GtkWidget *w = GTK_WIDGET (self);
  const OmarchyTokens *p = omarchy_paint ();
  int lh = omarchy_line_height (p->heading);
  /* The input's line box, vertically centered in the header (anchors.verticalCenter). */
  float y = g->header_y + ((g->header_h % 2) ? (g->header_h + 1) / 2.0f : g->header_h / 2.0f)
            - ((lh % 2) ? (lh + 1) / 2.0f : lh / 2.0f);
  gboolean has_preedit = self->preedit && self->preedit[0];
  if (!self->query[0] && !has_preedit) {
    g_autoptr (PangoLayout) l = omarchy_layout (w, placeholder_text (self), p->heading, FALSE);
    pango_layout_set_width (l, g->body_w * PANGO_SCALE);
    pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
    omarchy_draw_layout (s, l, g->body_x, y, &p->text_faint);
    return;
  }
  g_autofree char *text = g_strconcat (self->query, has_preedit ? self->preedit : "", NULL);
  g_autoptr (PangoLayout) l = omarchy_layout (w, text, p->heading, FALSE);
  if (has_preedit) {
    PangoAttrList *attrs = pango_attr_list_new ();
    PangoAttribute *u = pango_attr_underline_new (PANGO_UNDERLINE_SINGLE);
    u->start_index = strlen (self->query);
    u->end_index = strlen (text);
    pango_attr_list_insert (attrs, u);
    pango_layout_set_attributes (l, attrs);
    pango_attr_list_unref (attrs);
  }
  int tw;
  omarchy_layout_size (l, &tw, NULL);
  /* The end always shows (the caret's place), as a clipped TextInput scrolls to it. */
  float shift = MIN (0, g->body_w - tw);
  gtk_snapshot_push_clip (s, &GRAPHENE_RECT_INIT (g->body_x, g->header_y, g->body_w, g->header_h));
  omarchy_draw_layout (s, l, g->body_x + shift, y, &p->menu.text);
  gtk_snapshot_pop (s);
}

static void
draw_pane (OmarchySearchList *self, GtkSnapshot *s, const Geom *g)
{
  GtkWidget *w = GTK_WIDGET (self);
  const OmarchyTokens *p = omarchy_paint ();
  /* The 1px track rule the body's height. */
  omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (g->body_x + g->list_w + g->gap, g->body_y, 1, g->body_h), 0, &p->track);
  gtk_snapshot_push_clip (s, &GRAPHENE_RECT_INIT (g->pane_x, g->body_y, g->pane_w, g->body_h));
  if (self->detail) {
    gtk_widget_snapshot_child (w, self->detail, s);
  } else {
    OmarchySearchItem *item = omarchy_search_list_get_current_item (self);
    if (item) {
      float top = g->body_y + p->row_padding_x / 2.0f;
      g_autoptr (PangoLayout) t = omarchy_layout (w, item->title, p->title, TRUE);
      pango_layout_set_width (t, g->pane_w * PANGO_SCALE);
      pango_layout_set_ellipsize (t, PANGO_ELLIPSIZE_END);
      omarchy_draw_layout (s, t, g->pane_x, top, &p->menu.text);
      float y = top + omarchy_line_height (p->title) + p->sm;
      g_autoptr (PangoLayout) d = omarchy_layout (w, item->detail, p->body, FALSE);
      pango_layout_set_width (d, g->pane_w * PANGO_SCALE);
      pango_layout_set_wrap (d, PANGO_WRAP_WORD_CHAR);
      omarchy_draw_layout (s, d, g->pane_x, y, &p->text_dim);
    }
  }
  gtk_snapshot_pop (s);
}

static void
list_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchySearchList *self = OMARCHY_SEARCH_LIST (w);
  const OmarchyTokens *p = omarchy_paint ();
  int W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), p->radius, &p->menu.background, &p->menu.border);
  Geom g;
  geom (self, W, H, &g);
  draw_filter_line (self, s, &g);
  if (gtk_widget_get_visible (self->scroll))
    gtk_widget_snapshot_child (w, self->scroll, s);
  if (g.detail_shown)
    draw_pane (self, s, &g);
  if (gtk_widget_get_visible (self->empty))
    gtk_widget_snapshot_child (w, self->empty, s);
}

static void
list_realize (GtkWidget *w)
{
  GTK_WIDGET_CLASS (omarchy_search_list_parent_class)->realize (w);
  gtk_im_context_set_client_widget (OMARCHY_SEARCH_LIST (w)->im, w);
}

static void
list_unrealize (GtkWidget *w)
{
  gtk_im_context_set_client_widget (OMARCHY_SEARCH_LIST (w)->im, NULL);
  GTK_WIDGET_CLASS (omarchy_search_list_parent_class)->unrealize (w);
}

/* ---- GObject ----------------------------------------------------------------------------- */

static void
omarchy_search_list_init (OmarchySearchList *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->query = g_strdup ("");
  self->empty_glyph = g_strdup ("\U000F0349");
  self->empty_text = g_strdup ("");
  self->items = g_ptr_array_new_with_free_func (g_object_unref);
  self->rows = g_ptr_array_new ();
  self->detail_width = -1;
  self->last_cursor_index = -1;

  self->scroll = omarchy_scroll_area_new ();
  /* The body sizes it; nothing of its own (Qt's maxHeight: body.height). */
  omarchy_scroll_area_set_max_height (OMARCHY_SCROLL_AREA (self->scroll), 0);
  omarchy_scroll_area_set_spacing_token (OMARCHY_SCROLL_AREA (self->scroll), "xxs");
  gtk_widget_set_focusable (self->scroll, FALSE);
  gtk_widget_set_parent (self->scroll, w);

  self->empty = omarchy_empty_state_new ("");
  omarchy_empty_state_set_size (OMARCHY_EMPTY_STATE (self->empty), "overlay");
  gtk_widget_set_parent (self->empty, w);

  /* CursorModel: one section, showing from the start (the overlays open on the first row). */
  self->cursor = omarchy_cursor_model_new ();
  omarchy_cursor_model_add_section (self->cursor, SECTION, 0);
  omarchy_cursor_model_set_reference_item (self->cursor, w);
  omarchy_cursor_model_set (self->cursor, SECTION, 0);
  g_signal_connect_object (self->cursor, "moved", G_CALLBACK (cursor_moved), self, 0);
  g_signal_connect_object (self->cursor, "activated", G_CALLBACK (cursor_activated), self, 0);
  g_signal_connect_object (self->cursor, "delete-requested", G_CALLBACK (cursor_delete), self, 0);
  g_signal_connect_object (self->cursor, "notify::selected-index", G_CALLBACK (sync_cursor_index), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->cursor, "notify::cursor-active", G_CALLBACK (sync_cursor_index), self,
                           G_CONNECT_SWAPPED);

  /* The filter line: keys first, then the input method's commits (FilterInput.qml). */
  self->im = gtk_im_multicontext_new ();
  g_signal_connect (self->im, "commit", G_CALLBACK (im_commit), self);
  g_signal_connect (self->im, "preedit-changed", G_CALLBACK (im_preedit), self);
  self->keys = omarchy_widget_add_keys (w, list_key);
  gtk_event_controller_key_set_im_context (GTK_EVENT_CONTROLLER_KEY (self->keys), self->im);
  GtkEventController *focus = gtk_event_controller_focus_new ();
  g_signal_connect (focus, "enter", G_CALLBACK (focus_enter), self);
  g_signal_connect (focus, "leave", G_CALLBACK (focus_leave), self);
  gtk_widget_add_controller (w, focus);
  GtkGesture *click = gtk_gesture_click_new ();
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click), GTK_PHASE_CAPTURE);
  g_signal_connect (click, "pressed", G_CALLBACK (pressed), self);
  gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (click));

  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_cursor_from_name (w, "text");
  omarchy_widget_follow (w);
  sync_empty (self);
}

static void
list_dispose (GObject *o)
{
  OmarchySearchList *self = OMARCHY_SEARCH_LIST (o);
  g_ptr_array_set_size (self->rows, 0);
  g_clear_pointer (&self->scroll, gtk_widget_unparent);
  g_clear_pointer (&self->empty, gtk_widget_unparent);
  g_clear_pointer (&self->detail, gtk_widget_unparent);
  if (self->im) {
    g_signal_handlers_disconnect_by_data (self->im, self);
    g_clear_object (&self->im);
  }
  g_clear_object (&self->cursor);
  G_OBJECT_CLASS (omarchy_search_list_parent_class)->dispose (o);
}

static void
list_finalize (GObject *o)
{
  OmarchySearchList *self = OMARCHY_SEARCH_LIST (o);
  g_free (self->query);
  g_free (self->placeholder);
  g_free (self->empty_glyph);
  g_free (self->empty_text);
  g_free (self->no_matches);
  g_free (self->preedit);
  g_ptr_array_unref (self->items);
  g_ptr_array_unref (self->rows);
  G_OBJECT_CLASS (omarchy_search_list_parent_class)->finalize (o);
}

static void
list_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchySearchList *self = OMARCHY_SEARCH_LIST (o);
  switch (id) {
  case L_PROP_QUERY: omarchy_search_list_set_query (self, g_value_get_string (v)); break;
  case L_PROP_PLACEHOLDER_TEXT: omarchy_search_list_set_placeholder_text (self, g_value_get_string (v)); break;
  case L_PROP_SHOW_DETAIL: omarchy_search_list_set_show_detail (self, g_value_get_boolean (v)); break;
  case L_PROP_DETAIL: omarchy_search_list_set_detail (self, g_value_get_object (v)); break;
  case L_PROP_DETAIL_WIDTH: omarchy_search_list_set_detail_width (self, g_value_get_int (v)); break;
  case L_PROP_EMPTY_GLYPH: omarchy_search_list_set_empty_glyph (self, g_value_get_string (v)); break;
  case L_PROP_EMPTY_TEXT: omarchy_search_list_set_empty_text (self, g_value_get_string (v)); break;
  case L_PROP_NO_MATCHES_TEXT: omarchy_search_list_set_no_matches_text (self, g_value_get_string (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
list_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchySearchList *self = OMARCHY_SEARCH_LIST (o);
  switch (id) {
  case L_PROP_QUERY: g_value_set_string (v, self->query); break;
  case L_PROP_PLACEHOLDER_TEXT: g_value_set_string (v, placeholder_text (self)); break;
  case L_PROP_SHOW_DETAIL: g_value_set_boolean (v, self->show_detail); break;
  case L_PROP_DETAIL: g_value_set_object (v, self->detail); break;
  case L_PROP_DETAIL_WIDTH: g_value_set_int (v, self->detail_width); break;
  case L_PROP_EMPTY_GLYPH: g_value_set_string (v, self->empty_glyph); break;
  case L_PROP_EMPTY_TEXT: g_value_set_string (v, self->empty_text); break;
  case L_PROP_NO_MATCHES_TEXT: g_value_set_string (v, no_matches_text (self)); break;
  case L_PROP_N_ITEMS: g_value_set_uint (v, self->items->len); break;
  case L_PROP_CURSOR: g_value_set_object (v, self->cursor); break;
  case L_PROP_CURSOR_INDEX: g_value_set_int (v, omarchy_search_list_get_cursor_index (self)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_search_list_class_init (OmarchySearchListClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = list_dispose;
  oc->finalize = list_finalize;
  oc->set_property = list_set_property;
  oc->get_property = list_get_property;
  wc->measure = list_measure;
  wc->size_allocate = list_allocate;
  wc->snapshot = list_snapshot;
  wc->realize = list_realize;
  wc->unrealize = list_unrealize;

  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  const GParamFlags ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchySearchList:query:
   *
   * The filter line's text. Typing edits it; a new query puts the cursor on the first row.
   */
  l_props[L_PROP_QUERY] = g_param_spec_string ("query", NULL, NULL, "", f);
  /**
   * OmarchySearchList:placeholder-text:
   *
   * Shown in `textFaint` while the query is empty (default: the catalog's `common.search`).
   * Set %NULL for the default.
   */
  l_props[L_PROP_PLACEHOLDER_TEXT] = g_param_spec_string ("placeholder-text", NULL, NULL, NULL, f);
  /**
   * OmarchySearchList:show-detail:
   *
   * The detail pane beside the list: the cursor item's title and detail, or
   * #OmarchySearchList:detail instead.
   */
  l_props[L_PROP_SHOW_DETAIL] = g_param_spec_boolean ("show-detail", NULL, NULL, FALSE, f);
  /**
   * OmarchySearchList:detail:
   *
   * A custom detail pane, replacing the cursor item's title and detail (clipped to the body).
   */
  l_props[L_PROP_DETAIL] = g_param_spec_object ("detail", NULL, NULL, GTK_TYPE_WIDGET, f);
  /**
   * OmarchySearchList:detail-width:
   *
   * The detail pane's width, or -1 for 45% of the body.
   */
  l_props[L_PROP_DETAIL_WIDTH] = g_param_spec_int ("detail-width", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchySearchList:empty-glyph:
   *
   * The empty state's glyph (default the magnifier U+F0349).
   */
  l_props[L_PROP_EMPTY_GLYPH] = g_param_spec_string ("empty-glyph", NULL, NULL, "\U000F0349", f);
  /**
   * OmarchySearchList:empty-text:
   *
   * The empty state's line when there are no items and the query is empty.
   */
  l_props[L_PROP_EMPTY_TEXT] = g_param_spec_string ("empty-text", NULL, NULL, "", f);
  /**
   * OmarchySearchList:no-matches-text:
   *
   * The empty state's line when nothing matches the query; `%1` is the query (default: the
   * catalog's `common.noMatchesFor`). Set %NULL for the default.
   */
  l_props[L_PROP_NO_MATCHES_TEXT] = g_param_spec_string ("no-matches-text", NULL, NULL, NULL, f);
  /**
   * OmarchySearchList:n-items:
   *
   * The items listed.
   */
  l_props[L_PROP_N_ITEMS] = g_param_spec_uint ("n-items", NULL, NULL, 0, G_MAXUINT, 0, ro);
  /**
   * OmarchySearchList:cursor:
   *
   * The keyboard cursor (one section, `results`).
   */
  l_props[L_PROP_CURSOR] = g_param_spec_object ("cursor", NULL, NULL, OMARCHY_TYPE_CURSOR_MODEL, ro);
  /**
   * OmarchySearchList:cursor-index:
   *
   * The cursor's row, or -1 when there are no items (or the cursor is hidden).
   */
  l_props[L_PROP_CURSOR_INDEX] = g_param_spec_int ("cursor-index", NULL, NULL, -1, G_MAXINT, -1, ro);
  g_object_class_install_properties (oc, L_N_PROPS, l_props);

  /**
   * OmarchySearchList::activated:
   * @self: the list
   * @index: the item's index
   *
   * Return or Enter on the cursor row, or a click on a row.
   */
  l_signals[L_ACTIVATED] = g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                         G_TYPE_NONE, 1, G_TYPE_INT);
  /**
   * OmarchySearchList::delete-requested:
   * @self: the list
   * @index: the item's index
   *
   * Delete on the cursor row.
   */
  l_signals[L_DELETE_REQUESTED] = g_signal_new ("delete-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                                NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
  /**
   * OmarchySearchList::close-requested:
   * @self: the list
   *
   * Escape with the query already empty.
   */
  l_signals[L_CLOSE_REQUESTED] = g_signal_new ("close-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                               NULL, NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-search-list");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_SEARCH_BOX);
}

/* ---- API --------------------------------------------------------------------------------- */

/**
 * omarchy_search_list_new:
 *
 * Returns: (transfer floating): a search list, `space(480)` × `space(360)` at its natural size
 */
GtkWidget *
omarchy_search_list_new (void)
{
  return g_object_new (OMARCHY_TYPE_SEARCH_LIST, NULL);
}

/**
 * omarchy_search_list_matches:
 * @text: (nullable): a candidate
 * @query: (nullable): the query
 *
 * The plain case-insensitive substring test, for callers with nothing better (the
 * clipboard's filter). An empty query matches everything.
 *
 * Returns: whether @query is in @text
 */
gboolean
omarchy_search_list_matches (const char *text, const char *query)
{
  g_autofree char *t = g_utf8_strdown (text ? text : "", -1);
  g_autofree char *q = g_utf8_strdown (query ? query : "", -1);
  return strstr (t, q) != NULL;
}

const char *
omarchy_search_list_get_query (OmarchySearchList *self)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), NULL);
  return self->query;
}

/**
 * omarchy_search_list_set_query:
 * @self: the list
 * @query: (nullable): the filter text
 *
 * Sets the query and puts the cursor back on the first row; refilter the items on
 * notify::query.
 */
void
omarchy_search_list_set_query (OmarchySearchList *self, const char *query)
{
  g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));
  if (!g_strcmp0 (self->query, query ? query : ""))
    return;
  g_free (self->query);
  self->query = g_strdup (query ? query : "");
  /* A new query starts the cursor at the first match again, as the shell's overlays do. */
  omarchy_cursor_model_set (self->cursor, SECTION, 0);
  sync_empty (self);
  place_im (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_QUERY]);
}

const char *
omarchy_search_list_get_placeholder_text (OmarchySearchList *self)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), NULL);
  return placeholder_text (self);
}

/**
 * omarchy_search_list_set_placeholder_text:
 * @self: the list
 * @text: (nullable): the placeholder, or %NULL for the catalog's `common.search`
 */
void
omarchy_search_list_set_placeholder_text (OmarchySearchList *self, const char *text)
{
  g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));
  if (!g_strcmp0 (self->placeholder, text))
    return;
  g_free (self->placeholder);
  self->placeholder = g_strdup (text);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_PLACEHOLDER_TEXT]);
}

/**
 * omarchy_search_list_append:
 * @self: the list
 * @title: the title
 * @subtitle: (nullable): the dim line under it
 * @glyph: (nullable): the leading glyph
 * @value: (nullable): the dim text at the row's end
 * @detail: (nullable): the detail pane's text
 *
 * Adds an item (already filtered: the list shows exactly what it is given).
 */
void
omarchy_search_list_append (OmarchySearchList *self, const char *title, const char *subtitle, const char *glyph,
                            const char *value, const char *detail)
{
  g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));
  g_autoptr (OmarchySearchItem) item = g_object_new (OMARCHY_TYPE_SEARCH_ITEM, "title", title ? title : "", "subtitle",
                                                     subtitle ? subtitle : "", "glyph", glyph ? glyph : "", "value",
                                                     value ? value : "", "detail", detail ? detail : "", NULL);
  omarchy_search_list_append_item (self, item);
}

/**
 * omarchy_search_list_append_item:
 * @self: the list
 * @item: an item
 *
 * Adds an item (its row is made from it now; later changes to it aren't shown).
 */
void
omarchy_search_list_append_item (OmarchySearchList *self, OmarchySearchItem *item)
{
  g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));
  g_return_if_fail (OMARCHY_IS_SEARCH_ITEM (item));
  g_ptr_array_add (self->items, g_object_ref (item));
  add_row (self, item, self->items->len - 1);
  items_changed (self);
}

/**
 * omarchy_search_list_remove_all:
 * @self: the list
 */
void
omarchy_search_list_remove_all (OmarchySearchList *self)
{
  g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));
  for (guint i = 0; i < self->rows->len; i++)
    omarchy_scroll_area_remove (OMARCHY_SCROLL_AREA (self->scroll), g_ptr_array_index (self->rows, i));
  g_ptr_array_set_size (self->rows, 0);
  g_ptr_array_set_size (self->items, 0);
  items_changed (self);
}

guint
omarchy_search_list_get_n_items (OmarchySearchList *self)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), 0);
  return self->items->len;
}

/**
 * omarchy_search_list_get_item:
 * @self: the list
 * @index: an item's index
 *
 * Returns: (transfer none) (nullable): the item, or %NULL past the end
 */
OmarchySearchItem *
omarchy_search_list_get_item (OmarchySearchList *self, guint index)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), NULL);
  return index < self->items->len ? g_ptr_array_index (self->items, index) : NULL;
}

gboolean
omarchy_search_list_get_show_detail (OmarchySearchList *self)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), FALSE);
  return self->show_detail;
}

void
omarchy_search_list_set_show_detail (OmarchySearchList *self, gboolean show_detail)
{
  g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));
  if (self->show_detail == !!show_detail)
    return;
  self->show_detail = !!show_detail;
  sync_empty (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_SHOW_DETAIL]);
}

/**
 * omarchy_search_list_get_detail:
 * @self: the list
 *
 * Returns: (transfer none) (nullable): the custom detail pane
 */
GtkWidget *
omarchy_search_list_get_detail (OmarchySearchList *self)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), NULL);
  return self->detail;
}

/**
 * omarchy_search_list_set_detail:
 * @self: the list
 * @detail: (nullable) (transfer floating): a custom detail pane, or %NULL for the cursor
 *   item's title and detail
 *
 * Replaces the detail pane (shown while #OmarchySearchList:show-detail is on and there are
 * items). Update it from notify::cursor-index.
 */
void
omarchy_search_list_set_detail (OmarchySearchList *self, GtkWidget *detail)
{
  g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));
  if (self->detail == detail)
    return;
  if (self->detail)
    gtk_widget_unparent (self->detail);
  self->detail = detail;
  if (detail)
    gtk_widget_set_parent (detail, GTK_WIDGET (self));
  sync_empty (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_DETAIL]);
}

int
omarchy_search_list_get_detail_width (OmarchySearchList *self)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), -1);
  return self->detail_width;
}

/**
 * omarchy_search_list_set_detail_width:
 * @self: the list
 * @width: the pane's width, or -1 for 45% of the body
 */
void
omarchy_search_list_set_detail_width (OmarchySearchList *self, int width)
{
  g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));
  width = MAX (-1, width);
  if (self->detail_width == width)
    return;
  self->detail_width = width;
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_DETAIL_WIDTH]);
}

#define EMPTY_ACCESSORS(field, PROP)                                                          \
  const char *omarchy_search_list_get_##field (OmarchySearchList *self)                       \
  {                                                                                           \
    g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), NULL);                               \
    return self->field;                                                                       \
  }                                                                                           \
  void omarchy_search_list_set_##field (OmarchySearchList *self, const char *v)               \
  {                                                                                           \
    g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));                                         \
    if (!g_strcmp0 (self->field, v ? v : ""))                                                 \
      return;                                                                                 \
    g_free (self->field);                                                                     \
    self->field = g_strdup (v ? v : "");                                                      \
    sync_empty (self);                                                                        \
    g_object_notify_by_pspec (G_OBJECT (self), l_props[PROP]);                                \
  }

EMPTY_ACCESSORS (empty_glyph, L_PROP_EMPTY_GLYPH)
EMPTY_ACCESSORS (empty_text, L_PROP_EMPTY_TEXT)

const char *
omarchy_search_list_get_no_matches_text (OmarchySearchList *self)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), NULL);
  return no_matches_text (self);
}

/**
 * omarchy_search_list_set_no_matches_text:
 * @self: the list
 * @text: (nullable): the no-matches line (`%1` is the query), or %NULL for the catalog's
 *   `common.noMatchesFor`
 */
void
omarchy_search_list_set_no_matches_text (OmarchySearchList *self, const char *text)
{
  g_return_if_fail (OMARCHY_IS_SEARCH_LIST (self));
  if (!g_strcmp0 (self->no_matches, text))
    return;
  g_free (self->no_matches);
  self->no_matches = g_strdup (text);
  sync_empty (self);
  g_object_notify_by_pspec (G_OBJECT (self), l_props[L_PROP_NO_MATCHES_TEXT]);
}

/**
 * omarchy_search_list_get_cursor:
 * @self: the list
 *
 * Returns: (transfer none): the keyboard cursor (one section, `results`)
 */
OmarchyCursorModel *
omarchy_search_list_get_cursor (OmarchySearchList *self)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), NULL);
  return self->cursor;
}

int
omarchy_search_list_get_cursor_index (OmarchySearchList *self)
{
  g_return_val_if_fail (OMARCHY_IS_SEARCH_LIST (self), -1);
  if (!self->cursor || !omarchy_cursor_model_get_cursor_active (self->cursor) || self->items->len == 0)
    return -1;
  return omarchy_cursor_model_get_selected_index (self->cursor);
}

/**
 * omarchy_search_list_get_current_item:
 * @self: the list
 *
 * Returns: (transfer none) (nullable): the item under the cursor
 */
OmarchySearchItem *
omarchy_search_list_get_current_item (OmarchySearchList *self)
{
  int index = omarchy_search_list_get_cursor_index (self);
  return index >= 0 ? omarchy_search_list_get_item (self, index) : NULL;
}
