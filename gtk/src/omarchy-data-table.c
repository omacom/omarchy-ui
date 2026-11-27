/* omarchy-data-table.c: Table (Table.qml) and Tree (Tree.qml), drawn rows as the Qt kit draws
 * them (spec/components/data.md). */
#include "omarchy-data-private.h"

#include <string.h>

/* The sort chevrons and the tree's fold chevrons. */
#define GLYPH_ASCENDING "\xf3\xb0\x85\x83"  /* U+F0143 */
#define GLYPH_DESCENDING "\xf3\xb0\x85\x80" /* U+F0140 */
#define GLYPH_COLLAPSED "\xf3\xb0\x85\x82"  /* U+F0142 */
#define GLYPH_EXPANDED "\xf3\xb0\x85\x80"   /* U+F0140 */

/* A body that scrolls under a fixed top (the table's header): keep `offset` in range. */
static double
clamp_offset (double offset, int content, int view)
{
  return CLAMP (offset, 0, MAX (0, content - view));
}

/* The offset that brings [top, top + h) into a `view` tall window (ListView.Contain). */
static double
contain_offset (double offset, int top, int h, int view)
{
  if (top < offset)
    return top;
  if (top + h > offset + view)
    return top + h - view;
  return offset;
}

/* ================================================================================= Table === */

typedef struct {
  char *title;
  int width; /* 0: shares what the fixed columns leave */
  gboolean right;
} TableColumn;

static void
table_column_free (gpointer p)
{
  TableColumn *c = p;
  g_free (c->title);
  g_free (c);
}

/**
 * OmarchyTable:
 *
 * A data table: a header row of `caption` bold `textDim` titles (the sorted column carries a
 * sort chevron), then rows of `body` text cut with "…", hairline `track` rules between them.
 * Rows and the header are `popupRowHeight` tall, cells padded `controlPaddingX`. Fixed columns
 * take their width; the rest share what is left.
 *
 * #OmarchyTable:selected-row is the chosen row (selected fill, bold);
 * #OmarchyTable:cursor-row the keyboard cursor (hover fill), which the pointer moves too, shown
 * while the table has focus, is hovered, or has #OmarchyTable:has-cursor. Up/Down (j/k),
 * Home/End and PageUp/PageDown move the cursor; Return/Space, or a click, choose a row
 * (::row-activated, after #OmarchyTable:selected-row takes it). A header click flips the order
 * of the sorted column or sorts by another (::sort-requested, after the sort properties change);
 * the table never reorders its rows itself. Given less height than it asks for, the rows scroll
 * under the header.
 */
struct _OmarchyTable {
  GtkWidget parent_instance;
  GPtrArray *columns; /* TableColumn */
  GPtrArray *rows;    /* GStrv */
  int sort_column;
  gboolean sort_ascending;
  int selected_row, cursor_row;
  gboolean has_cursor, separators, hovering;
  double offset;
};

enum {
  TB_PROP_0,
  TB_PROP_SORT_COLUMN,
  TB_PROP_SORT_ASCENDING,
  TB_PROP_SELECTED_ROW,
  TB_PROP_CURSOR_ROW,
  TB_PROP_HAS_CURSOR,
  TB_PROP_SEPARATORS,
  TB_N_PROPS
};
static GParamSpec *tb_props[TB_N_PROPS];
enum { TB_ROW_ACTIVATED, TB_SORT_REQUESTED, TB_N_SIGNALS };
static guint tb_signals[TB_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyTable, omarchy_table, GTK_TYPE_WIDGET)

static int
table_width_of (OmarchyTable *self, int column, int width)
{
  int fixed = 0, flex = 0;
  for (guint i = 0; i < self->columns->len; i++) {
    TableColumn *c = g_ptr_array_index (self->columns, i);
    if (c->width > 0)
      fixed += c->width;
    else
      flex++;
  }
  TableColumn *c = g_ptr_array_index (self->columns, column);
  if (c->width > 0)
    return c->width;
  return flex > 0 ? MAX (0, (int) floor ((width - fixed) / (double) flex)) : 0;
}

static int
table_body_height (OmarchyTable *self)
{
  return MAX (0, gtk_widget_get_height (GTK_WIDGET (self)) - omarchy_tokens ()->popup_row_height);
}

static void
table_reveal (OmarchyTable *self, int row)
{
  int rh = omarchy_tokens ()->popup_row_height;
  int view = table_body_height (self);
  if (row < 0 || view <= 0)
    return;
  self->offset = clamp_offset (contain_offset (self->offset, row * rh, rh, view), (int) self->rows->len * rh, view);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
table_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyTable *self = OMARCHY_TABLE (w);
  const OmarchyTokens *t = omarchy_tokens ();
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = 0;
    *nat = 400; /* Qt's implicitWidth */
  } else {
    int n = self->rows->len;
    *min = t->popup_row_height * (n > 0 ? 2 : 1);
    *nat = t->popup_row_height * (1 + n);
  }
}

static void
table_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyTable *self = OMARCHY_TABLE (w);
  int rh = omarchy_tokens ()->popup_row_height;
  self->offset = clamp_offset (self->offset, (int) self->rows->len * rh, MAX (0, height - rh));
}

static void
table_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyTable *self = OMARCHY_TABLE (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  int rh = p->popup_row_height, pad = p->control_padding_x, hair = p->hairline;
  int n_cols = self->columns->len, n_rows = self->rows->len;
  int widths[MAX (1, n_cols)];
  for (int i = 0; i < n_cols; i++)
    widths[i] = table_width_of (self, i, (int) W);

  /* ---- Header */
  float cx = 0;
  float ty = data_center_y (0, rh, p->caption);
  for (int i = 0; i < n_cols; i++) {
    TableColumn *c = g_ptr_array_index (self->columns, i);
    int cw = widths[i], avail = MAX (0, cw - pad * 2);
    gboolean sorted = self->sort_column == i;
    const char *glyph = self->sort_ascending ? GLYPH_ASCENDING : GLYPH_DESCENDING;
    int gw = sorted ? data_glyph_width (w, glyph, p->caption) : 0;
    g_autoptr (PangoLayout) title = data_text (w, c->title, p->caption, TRUE, MAX (0, avail - (sorted ? gw + p->xs : 0)));
    int tw = data_text_width (title);
    gtk_snapshot_push_clip (s, &GRAPHENE_RECT_INIT (data_x (w, W, cx, cw), 0, cw, rh));
    float tx = c->right ? cx + cw - pad - tw : cx + pad;
    data_draw_text (w, s, title, W, tx, ty, &p->text_dim);
    if (sorted) {
      float gx = c->right ? tx - p->xs - gw : tx + tw + p->xs;
      data_draw_glyph (w, s, glyph, p->caption, W, gx, ty, &p->text_dim);
    }
    gtk_snapshot_pop (s);
    cx += cw;
  }
  gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, rh - hair, W, hair));

  /* ---- Body */
  gboolean show = data_show_cursor (w, self->has_cursor, self->hovering);
  gtk_snapshot_push_clip (s, &GRAPHENE_RECT_INIT (0, rh, W, MAX (0, H - rh)));
  gtk_snapshot_save (s);
  gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (0, rh - (float) self->offset));
  float view = MAX (0, H - rh);
  for (int r = 0; r < n_rows; r++) {
    float y = r * rh;
    if (y + rh <= self->offset || y >= self->offset + view)
      continue;
    char **cells = g_ptr_array_index (self->rows, r);
    gboolean selected = self->selected_row == r;
    gboolean cursor = show && self->cursor_row == r;
    data_row_fill (s, 0, y, W, rh, cursor, selected);
    const GdkRGBA *ink = selected ? &p->selected.text : &p->foreground;
    float by = data_center_y (y, rh, p->body);
    guint n_cells = cells ? g_strv_length (cells) : 0;
    cx = 0;
    for (int i = 0; i < n_cols; i++) {
      TableColumn *c = g_ptr_array_index (self->columns, i);
      int cw = widths[i];
      const char *text = i < (int) n_cells ? cells[i] : "";
      if (*text) {
        g_autoptr (PangoLayout) l = data_text (w, text, p->body, selected, MAX (0, cw - pad * 2));
        int tw = data_text_width (l);
        gtk_snapshot_push_clip (s, &GRAPHENE_RECT_INIT (data_x (w, W, cx, cw), y, cw, rh));
        data_draw_text (w, s, l, W, c->right ? cx + cw - pad - tw : cx + pad, by, ink);
        gtk_snapshot_pop (s);
      }
      cx += cw;
    }
    gboolean next_hi = self->selected_row == r + 1 || (show && self->cursor_row == r + 1);
    if (self->separators && r < n_rows - 1 && !selected && !cursor && !next_hi)
      gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, y + rh - hair, W, hair));
  }
  gtk_snapshot_restore (s);
  gtk_snapshot_pop (s);
}

/* The row under a point of the body, or -1 (the header, or below the rows). */
static int
table_row_at (OmarchyTable *self, double y)
{
  int rh = omarchy_tokens ()->popup_row_height;
  if (y < rh)
    return -1;
  int row = (int) floor ((y - rh + self->offset) / rh);
  return row >= 0 && row < (int) self->rows->len ? row : -1;
}

static int
table_column_at (OmarchyTable *self, double x)
{
  GtkWidget *w = GTK_WIDGET (self);
  int W = gtk_widget_get_width (w);
  double sx = data_rtl (w) ? W - x : x, cx = 0;
  for (guint i = 0; i < self->columns->len; i++) {
    int cw = table_width_of (self, i, W);
    if (sx >= cx && sx < cx + cw)
      return i;
    cx += cw;
  }
  return -1;
}

static void
table_activate (OmarchyTable *self, int row)
{
  if (row < 0 || row >= (int) self->rows->len)
    return;
  omarchy_table_set_selected_row (self, row);
  g_signal_emit (self, tb_signals[TB_ROW_ACTIVATED], 0, row);
}

static void
table_move_cursor (OmarchyTable *self, int delta)
{
  int n = self->rows->len;
  if (n == 0)
    return;
  int from = self->cursor_row < 0 ? (delta > 0 ? -1 : n) : self->cursor_row;
  omarchy_table_set_cursor_row (self, CLAMP (from + delta, 0, n - 1));
  table_reveal (self, self->cursor_row);
}

static void
table_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyTable *self = OMARCHY_TABLE (w);
  if (button != GDK_BUTTON_PRIMARY)
    return;
  if (y < omarchy_tokens ()->popup_row_height) {
    int column = table_column_at (self, x);
    if (column < 0)
      return;
    g_object_freeze_notify (G_OBJECT (self));
    if (column == self->sort_column) {
      omarchy_table_set_sort_ascending (self, !self->sort_ascending);
    } else {
      omarchy_table_set_sort_column (self, column);
      omarchy_table_set_sort_ascending (self, TRUE);
    }
    g_object_thaw_notify (G_OBJECT (self));
    g_signal_emit (self, tb_signals[TB_SORT_REQUESTED], 0, column);
    return;
  }
  int row = table_row_at (self, y);
  if (row < 0)
    return;
  gtk_widget_grab_focus (w);
  table_activate (self, row);
}

static gboolean
table_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyTable *self = OMARCHY_TABLE (w);
  int n = self->rows->len;
  int step = data_vertical_step (keyval);
  int page = MAX (1, table_body_height (self) / MAX (1, omarchy_tokens ()->popup_row_height));
  if (step) {
    table_move_cursor (self, step);
  } else if (keyval == GDK_KEY_Home || keyval == GDK_KEY_KP_Home) {
    omarchy_table_set_cursor_row (self, n > 0 ? 0 : -1);
    table_reveal (self, self->cursor_row);
  } else if (keyval == GDK_KEY_End || keyval == GDK_KEY_KP_End) {
    omarchy_table_set_cursor_row (self, n - 1);
    table_reveal (self, self->cursor_row);
  } else if (keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_KP_Page_Down) {
    table_move_cursor (self, page);
  } else if (keyval == GDK_KEY_Page_Up || keyval == GDK_KEY_KP_Page_Up) {
    table_move_cursor (self, -page);
  } else if (omarchy_key_is_activate (keyval)) {
    table_activate (self, self->cursor_row);
  } else {
    return FALSE;
  }
  return TRUE;
}

static void
table_motion (GtkEventControllerMotion *c, double x, double y, OmarchyTable *self)
{
  int row = table_row_at (self, y);
  gboolean hovering = row >= 0;
  if (hovering)
    omarchy_table_set_cursor_row (self, row);
  if (hovering != self->hovering) {
    self->hovering = hovering;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
table_leave (GtkEventControllerMotion *c, OmarchyTable *self)
{
  self->hovering = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static gboolean
table_scroll (GtkEventControllerScroll *c, double dx, double dy, OmarchyTable *self)
{
  int rh = omarchy_tokens ()->popup_row_height;
  int view = table_body_height (self), content = (int) self->rows->len * rh;
  if (content <= view)
    return FALSE;
  self->offset = clamp_offset (self->offset + dy * rh, content, view);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  return TRUE;
}

static void
add_motion_and_scroll (GtkWidget *w, GCallback motion, GCallback leave, GCallback scroll)
{
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "enter", motion, w);
  g_signal_connect (m, "motion", motion, w);
  g_signal_connect (m, "leave", leave, w);
  gtk_widget_add_controller (w, m);
  if (scroll) {
    GtkEventController *sc = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect (sc, "scroll", scroll, w);
    gtk_widget_add_controller (w, sc);
  }
}

static void
omarchy_table_init (OmarchyTable *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->columns = g_ptr_array_new_with_free_func (table_column_free);
  self->rows = g_ptr_array_new_with_free_func ((GDestroyNotify) g_strfreev);
  self->sort_column = -1;
  self->sort_ascending = TRUE;
  self->selected_row = -1;
  self->cursor_row = -1;
  self->separators = TRUE;
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  gtk_widget_set_overflow (w, GTK_OVERFLOW_HIDDEN);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, table_clicked);
  omarchy_widget_add_keys (w, table_key);
  add_motion_and_scroll (w, G_CALLBACK (table_motion), G_CALLBACK (table_leave), G_CALLBACK (table_scroll));
}

static void
table_finalize (GObject *o)
{
  OmarchyTable *self = OMARCHY_TABLE (o);
  g_ptr_array_unref (self->columns);
  g_ptr_array_unref (self->rows);
  G_OBJECT_CLASS (omarchy_table_parent_class)->finalize (o);
}

static void
table_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyTable *self = OMARCHY_TABLE (o);
  switch (id) {
  case TB_PROP_SORT_COLUMN: omarchy_table_set_sort_column (self, g_value_get_int (v)); break;
  case TB_PROP_SORT_ASCENDING: omarchy_table_set_sort_ascending (self, g_value_get_boolean (v)); break;
  case TB_PROP_SELECTED_ROW: omarchy_table_set_selected_row (self, g_value_get_int (v)); break;
  case TB_PROP_CURSOR_ROW: omarchy_table_set_cursor_row (self, g_value_get_int (v)); break;
  case TB_PROP_HAS_CURSOR: omarchy_table_set_has_cursor (self, g_value_get_boolean (v)); break;
  case TB_PROP_SEPARATORS: omarchy_table_set_separators (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
table_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyTable *self = OMARCHY_TABLE (o);
  switch (id) {
  case TB_PROP_SORT_COLUMN: g_value_set_int (v, self->sort_column); break;
  case TB_PROP_SORT_ASCENDING: g_value_set_boolean (v, self->sort_ascending); break;
  case TB_PROP_SELECTED_ROW: g_value_set_int (v, self->selected_row); break;
  case TB_PROP_CURSOR_ROW: g_value_set_int (v, self->cursor_row); break;
  case TB_PROP_HAS_CURSOR: g_value_set_boolean (v, self->has_cursor); break;
  case TB_PROP_SEPARATORS: g_value_set_boolean (v, self->separators); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_table_class_init (OmarchyTableClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = table_finalize;
  oc->set_property = table_set_property;
  oc->get_property = table_get_property;
  wc->measure = table_measure;
  wc->size_allocate = table_allocate;
  wc->snapshot = table_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyTable:sort-column:
   *
   * The column showing the sort chevron, or -1.
   */
  tb_props[TB_PROP_SORT_COLUMN] = g_param_spec_int ("sort-column", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyTable:sort-ascending:
   *
   * The chevron points up (ascending) or down.
   */
  tb_props[TB_PROP_SORT_ASCENDING] = g_param_spec_boolean ("sort-ascending", NULL, NULL, TRUE, f);
  /**
   * OmarchyTable:selected-row:
   *
   * The chosen row (selected fill, bold), or -1.
   */
  tb_props[TB_PROP_SELECTED_ROW] = g_param_spec_int ("selected-row", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyTable:cursor-row:
   *
   * The keyboard cursor's row (hover fill), or -1. The pointer moves it too.
   */
  tb_props[TB_PROP_CURSOR_ROW] = g_param_spec_int ("cursor-row", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyTable:has-cursor:
   *
   * Show the cursor row's hover look without focus, for panels that drive the cursor.
   */
  tb_props[TB_PROP_HAS_CURSOR] = g_param_spec_boolean ("has-cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyTable:separators:
   *
   * A hairline under every row but the last.
   */
  tb_props[TB_PROP_SEPARATORS] = g_param_spec_boolean ("separators", NULL, NULL, TRUE, f);
  g_object_class_install_properties (oc, TB_N_PROPS, tb_props);
  /**
   * OmarchyTable::row-activated:
   * @self: the table
   * @table: the table
   * @row: the row chosen with a click or Return/Space (now #OmarchyTable:selected-row)
   */
  tb_signals[TB_ROW_ACTIVATED] = g_signal_new ("row-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                               NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
  /**
   * OmarchyTable::sort-requested:
   * @self: the table
   * @table: the table
   * @column: the header clicked; #OmarchyTable:sort-column and #OmarchyTable:sort-ascending
   *   already show the order asked for, and the app reorders the rows
   */
  tb_signals[TB_SORT_REQUESTED] = g_signal_new ("sort-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                                NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
  gtk_widget_class_set_css_name (wc, "omarchy-table");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GRID);
}

/**
 * omarchy_table_new:
 *
 * Returns: (transfer floating): an empty table; add columns and rows
 */
GtkWidget *
omarchy_table_new (void)
{
  return g_object_new (OMARCHY_TYPE_TABLE, NULL);
}

/**
 * omarchy_table_append_column:
 * @self: the table
 * @title: (nullable): the header
 * @width: the column's width, or 0 to share what the fixed columns leave
 * @align_right: right-align the cells (numbers); the header's chevron goes before the title
 */
void
omarchy_table_append_column (OmarchyTable *self, const char *title, int width, gboolean align_right)
{
  TableColumn *c = g_new0 (TableColumn, 1);
  c->title = g_strdup (title ? title : "");
  c->width = MAX (0, width);
  c->right = !!align_right;
  g_ptr_array_add (self->columns, c);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
omarchy_table_remove_all_columns (OmarchyTable *self)
{
  g_ptr_array_set_size (self->columns, 0);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

int
omarchy_table_get_n_columns (OmarchyTable *self)
{
  return self->columns->len;
}

/**
 * omarchy_table_get_column_width:
 * @self: the table
 * @column: a column
 *
 * Returns: the column's width at the table's current width
 */
int
omarchy_table_get_column_width (OmarchyTable *self, int column)
{
  if (column < 0 || column >= (int) self->columns->len)
    return 0;
  return table_width_of (self, column, gtk_widget_get_width (GTK_WIDGET (self)));
}

/**
 * omarchy_table_append_row:
 * @self: the table
 * @cells: (array zero-terminated=1): one string per column
 */
void
omarchy_table_append_row (OmarchyTable *self, const char *const *cells)
{
  g_ptr_array_add (self->rows, g_strdupv ((char **) cells));
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
omarchy_table_remove_all_rows (OmarchyTable *self)
{
  g_ptr_array_set_size (self->rows, 0);
  self->offset = 0;
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

int
omarchy_table_get_n_rows (OmarchyTable *self)
{
  return self->rows->len;
}

/**
 * omarchy_table_get_cell:
 * @self: the table
 * @row: a row
 * @column: a column
 *
 * Returns: (nullable): the cell's text
 */
const char *
omarchy_table_get_cell (OmarchyTable *self, int row, int column)
{
  if (row < 0 || row >= (int) self->rows->len || column < 0)
    return NULL;
  char **cells = g_ptr_array_index (self->rows, row);
  return cells && column < (int) g_strv_length (cells) ? cells[column] : NULL;
}

#define INT_ACCESSORS(Type, type, field, PROPS, PROP)                                \
  int omarchy_##type##_get_##field (Type *self) { return self->field; }               \
  void omarchy_##type##_set_##field (Type *self, int v)                               \
  {                                                                                   \
    v = MAX (-1, v);                                                                  \
    if (self->field == v)                                                             \
      return;                                                                         \
    self->field = v;                                                                  \
    gtk_widget_queue_draw (GTK_WIDGET (self));                                        \
    g_object_notify_by_pspec (G_OBJECT (self), PROPS[PROP]);                          \
  }

INT_ACCESSORS (OmarchyTable, table, sort_column, tb_props, TB_PROP_SORT_COLUMN)
INT_ACCESSORS (OmarchyTable, table, selected_row, tb_props, TB_PROP_SELECTED_ROW)
INT_ACCESSORS (OmarchyTable, table, cursor_row, tb_props, TB_PROP_CURSOR_ROW)
OMARCHY_BOOL_ACCESSORS (OmarchyTable, table, sort_ascending, tb_props, TB_PROP_SORT_ASCENDING, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyTable, table, has_cursor, tb_props, TB_PROP_HAS_CURSOR, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyTable, table, separators, tb_props, TB_PROP_SEPARATORS, FALSE)

/* ================================================================================== Tree === */

typedef struct TreeNode TreeNode;
struct TreeNode {
  char *id, *label, *glyph;
  GPtrArray *children; /* TreeNode, owned */
};

static void
tree_node_free (gpointer p)
{
  TreeNode *n = p;
  g_free (n->id);
  g_free (n->label);
  g_free (n->glyph);
  g_ptr_array_unref (n->children);
  g_free (n);
}

typedef struct {
  TreeNode *node;
  int depth;
  gboolean open;
} TreeRow;

/**
 * OmarchyTree:
 *
 * A tree of rows, FileTree-style: each level indented by `huge`, a chevron on branches
 * (right when collapsed, down when expanded) in an `xxxl` slot, an optional glyph, the label.
 *
 * #OmarchyTree:selected-id is the chosen row (selected fill, bold); #OmarchyTree:cursor-id the
 * keyboard cursor (hover fill), which the pointer moves too. Up/Down (j/k) move the cursor;
 * Right (l) unfolds a folded branch, Left (h) folds an open one; Return/Space or a click choose
 * (::activated, after #OmarchyTree:selected-id takes it). A chevron click or a double click on a
 * branch folds or unfolds it (::toggled, after #OmarchyTree:expanded changes).
 */
struct _OmarchyTree {
  GtkWidget parent_instance;
  GPtrArray *roots;     /* TreeNode, owned */
  GHashTable *by_id;    /* id → TreeNode */
  GPtrArray *expanded;  /* char *, in the order they opened */
  GArray *visible;      /* TreeRow, rebuilt when stale */
  gboolean stale;
  char *selected_id, *cursor_id;
  gboolean has_cursor, hovering;
  double offset;
};

enum { TR_PROP_0, TR_PROP_EXPANDED, TR_PROP_SELECTED_ID, TR_PROP_CURSOR_ID, TR_PROP_HAS_CURSOR, TR_N_PROPS };
static GParamSpec *tr_props[TR_N_PROPS];
enum { TR_TOGGLED, TR_ACTIVATED, TR_N_SIGNALS };
static guint tr_signals[TR_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyTree, omarchy_tree, GTK_TYPE_WIDGET)

static int
tree_expanded_index (OmarchyTree *self, const char *id)
{
  for (guint i = 0; i < self->expanded->len; i++)
    if (!g_strcmp0 (g_ptr_array_index (self->expanded, i), id))
      return i;
  return -1;
}

static void
tree_walk (OmarchyTree *self, GPtrArray *list, int depth)
{
  for (guint i = 0; i < list->len; i++) {
    TreeNode *n = g_ptr_array_index (list, i);
    gboolean open = n->children->len > 0 && tree_expanded_index (self, n->id) >= 0;
    TreeRow r = { n, depth, open };
    g_array_append_val (self->visible, r);
    if (open)
      tree_walk (self, n->children, depth + 1);
  }
}

static GArray *
tree_rows (OmarchyTree *self)
{
  if (self->stale) {
    g_array_set_size (self->visible, 0);
    tree_walk (self, self->roots, 0);
    self->stale = FALSE;
  }
  return self->visible;
}

static void
tree_changed (OmarchyTree *self)
{
  self->stale = TRUE;
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static int
tree_index_of (OmarchyTree *self, const char *id)
{
  GArray *rows = tree_rows (self);
  for (guint i = 0; i < rows->len; i++)
    if (!g_strcmp0 (g_array_index (rows, TreeRow, i).node->id, id))
      return i;
  return -1;
}

static void
tree_reveal (OmarchyTree *self, int index)
{
  int rh = omarchy_tokens ()->popup_row_height, view = gtk_widget_get_height (GTK_WIDGET (self));
  if (index < 0 || view <= 0)
    return;
  self->offset = clamp_offset (contain_offset (self->offset, index * rh, rh, view), (int) tree_rows (self)->len * rh, view);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
tree_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyTree *self = OMARCHY_TREE (w);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = 0;
    *nat = 240; /* Qt's implicitWidth */
  } else {
    int n = tree_rows (self)->len, rh = omarchy_tokens ()->popup_row_height;
    *min = n > 0 ? rh : 0;
    *nat = n * rh;
  }
}

static void
tree_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyTree *self = OMARCHY_TREE (w);
  self->offset = clamp_offset (self->offset, (int) tree_rows (self)->len * omarchy_tokens ()->popup_row_height, height);
}

/* The chevron slot's start-edge x on a row. */
static int
tree_slot_x (const OmarchyTokens *t, int depth)
{
  return t->md + depth * t->huge;
}

static void
tree_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyTree *self = OMARCHY_TREE (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  int rh = p->popup_row_height;
  GArray *rows = tree_rows (self);
  gboolean show = data_show_cursor (w, self->has_cursor, self->hovering);
  gtk_snapshot_push_clip (s, &GRAPHENE_RECT_INIT (0, 0, W, H));
  gtk_snapshot_save (s);
  gtk_snapshot_translate (s, &GRAPHENE_POINT_INIT (0, -(float) self->offset));
  for (guint i = 0; i < rows->len; i++) {
    TreeRow *r = &g_array_index (rows, TreeRow, i);
    float y = i * rh;
    if (y + rh <= self->offset || y >= self->offset + H)
      continue;
    gboolean selected = !g_strcmp0 (self->selected_id, r->node->id);
    data_row_fill (s, 0, y, W, rh, show && !g_strcmp0 (self->cursor_id, r->node->id), selected);
    int slot = tree_slot_x (p, r->depth);
    if (r->node->children->len > 0) {
      const char *chevron = r->open ? GLYPH_EXPANDED : GLYPH_COLLAPSED;
      data_draw_optical_glyph (w, s, chevron, p->icon_small, W, slot, y, p->xxxl, rh, &p->text_dim);
    }
    float gx = slot + p->xxxl + p->sm;
    gboolean has_glyph = r->node->glyph && *r->node->glyph;
    if (has_glyph)
      data_draw_optical_glyph (w, s, r->node->glyph, p->icon, W, gx, y, p->icon, rh,
                               selected ? &p->selected.text : &p->text_dim);
    float lx = gx + (has_glyph ? p->icon + p->md : 0);
    g_autoptr (PangoLayout) l = data_text (w, r->node->label, p->body, selected, MAX (0, (int) (W - lx - p->md)));
    data_draw_text (w, s, l, W, lx, data_center_y (y, rh, p->body), selected ? &p->selected.text : &p->foreground);
  }
  gtk_snapshot_restore (s);
  gtk_snapshot_pop (s);
}

static TreeRow *
tree_row_at (OmarchyTree *self, double y, int *index)
{
  GArray *rows = tree_rows (self);
  int i = (int) floor ((y + self->offset) / omarchy_tokens ()->popup_row_height);
  if (index)
    *index = i;
  return y >= 0 && i >= 0 && i < (int) rows->len ? &g_array_index (rows, TreeRow, i) : NULL;
}

static gboolean
tree_in_chevron (OmarchyTree *self, TreeRow *r, double x)
{
  GtkWidget *w = GTK_WIDGET (self);
  const OmarchyTokens *t = omarchy_tokens ();
  double sx = data_rtl (w) ? gtk_widget_get_width (w) - x : x;
  int slot = tree_slot_x (t, r->depth);
  return r->node->children->len > 0 && sx >= slot && sx < slot + t->xxxl;
}

static void
tree_toggle (OmarchyTree *self, const char *id)
{
  g_autofree char *copy = g_strdup (id);
  omarchy_tree_set_node_expanded (self, copy, !omarchy_tree_is_expanded (self, copy));
  g_signal_emit (self, tr_signals[TR_TOGGLED], 0, copy);
}

static void
tree_activate (OmarchyTree *self, const char *id)
{
  if (!id || !*id)
    return;
  g_autofree char *copy = g_strdup (id);
  omarchy_tree_set_selected_id (self, copy);
  g_signal_emit (self, tr_signals[TR_ACTIVATED], 0, copy);
}

static void
tree_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyTree *self = OMARCHY_TREE (w);
  if (button != GDK_BUTTON_PRIMARY)
    return;
  TreeRow *r = tree_row_at (self, y, NULL);
  if (!r)
    return;
  if (tree_in_chevron (self, r, x)) {
    tree_toggle (self, r->node->id);
    return;
  }
  gtk_widget_grab_focus (w);
  tree_activate (self, r->node->id);
}

static void
tree_double_pressed (GtkGestureClick *g, int n_press, double x, double y, OmarchyTree *self)
{
  if (n_press != 2)
    return;
  TreeRow *r = tree_row_at (self, y, NULL);
  if (r && r->node->children->len > 0 && !tree_in_chevron (self, r, x))
    tree_toggle (self, r->node->id);
}

static gboolean
tree_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyTree *self = OMARCHY_TREE (w);
  GArray *rows = tree_rows (self);
  int step = data_vertical_step (keyval);
  guint reading = omarchy_reading_key (w, keyval, TRUE);
  int at = tree_index_of (self, self->cursor_id);
  TreeRow *r = at >= 0 ? &g_array_index (rows, TreeRow, at) : NULL;
  if (step) {
    if (rows->len == 0)
      return TRUE;
    int from = at < 0 ? (step > 0 ? -1 : (int) rows->len) : at;
    int next = CLAMP (from + step, 0, (int) rows->len - 1);
    omarchy_tree_set_cursor_id (self, g_array_index (rows, TreeRow, next).node->id);
    tree_reveal (self, next);
  } else if (reading == GDK_KEY_Right || reading == GDK_KEY_KP_Right) {
    if (r && r->node->children->len > 0 && !r->open)
      tree_toggle (self, r->node->id);
  } else if (reading == GDK_KEY_Left || reading == GDK_KEY_KP_Left) {
    if (r && r->node->children->len > 0 && r->open)
      tree_toggle (self, r->node->id);
  } else if (omarchy_key_is_activate (keyval)) {
    tree_activate (self, self->cursor_id);
  } else {
    return FALSE;
  }
  return TRUE;
}

static void
tree_motion (GtkEventControllerMotion *c, double x, double y, OmarchyTree *self)
{
  TreeRow *r = tree_row_at (self, y, NULL);
  if (r)
    omarchy_tree_set_cursor_id (self, r->node->id);
  if (!!r != self->hovering) {
    self->hovering = !!r;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
tree_leave (GtkEventControllerMotion *c, OmarchyTree *self)
{
  self->hovering = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static gboolean
tree_scroll (GtkEventControllerScroll *c, double dx, double dy, OmarchyTree *self)
{
  int rh = omarchy_tokens ()->popup_row_height;
  int view = gtk_widget_get_height (GTK_WIDGET (self)), content = (int) tree_rows (self)->len * rh;
  if (content <= view)
    return FALSE;
  self->offset = clamp_offset (self->offset + dy * rh, content, view);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  return TRUE;
}

static void
omarchy_tree_init (OmarchyTree *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->roots = g_ptr_array_new_with_free_func (tree_node_free);
  self->by_id = g_hash_table_new (g_str_hash, g_str_equal);
  self->expanded = g_ptr_array_new_with_free_func (g_free);
  self->visible = g_array_new (FALSE, FALSE, sizeof (TreeRow));
  self->selected_id = g_strdup ("");
  self->cursor_id = g_strdup ("");
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, tree_clicked);
  GtkGesture *dbl = gtk_gesture_click_new ();
  g_signal_connect (dbl, "pressed", G_CALLBACK (tree_double_pressed), self);
  gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (dbl));
  omarchy_widget_add_keys (w, tree_key);
  add_motion_and_scroll (w, G_CALLBACK (tree_motion), G_CALLBACK (tree_leave), G_CALLBACK (tree_scroll));
}

static void
tree_finalize (GObject *o)
{
  OmarchyTree *self = OMARCHY_TREE (o);
  g_hash_table_unref (self->by_id);
  g_ptr_array_unref (self->roots);
  g_ptr_array_unref (self->expanded);
  g_array_unref (self->visible);
  g_free (self->selected_id);
  g_free (self->cursor_id);
  G_OBJECT_CLASS (omarchy_tree_parent_class)->finalize (o);
}

static void
tree_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyTree *self = OMARCHY_TREE (o);
  switch (id) {
  case TR_PROP_EXPANDED: omarchy_tree_set_expanded (self, g_value_get_boxed (v)); break;
  case TR_PROP_SELECTED_ID: omarchy_tree_set_selected_id (self, g_value_get_string (v)); break;
  case TR_PROP_CURSOR_ID: omarchy_tree_set_cursor_id (self, g_value_get_string (v)); break;
  case TR_PROP_HAS_CURSOR: omarchy_tree_set_has_cursor (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
tree_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyTree *self = OMARCHY_TREE (o);
  switch (id) {
  case TR_PROP_EXPANDED: g_value_take_boxed (v, omarchy_tree_get_expanded (self)); break;
  case TR_PROP_SELECTED_ID: g_value_set_string (v, self->selected_id); break;
  case TR_PROP_CURSOR_ID: g_value_set_string (v, self->cursor_id); break;
  case TR_PROP_HAS_CURSOR: g_value_set_boolean (v, self->has_cursor); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_tree_class_init (OmarchyTreeClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = tree_finalize;
  oc->set_property = tree_set_property;
  oc->get_property = tree_get_property;
  wc->measure = tree_measure;
  wc->size_allocate = tree_allocate;
  wc->snapshot = tree_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyTree:expanded:
   *
   * The ids of the branches whose children show.
   */
  tr_props[TR_PROP_EXPANDED] = g_param_spec_boxed ("expanded", NULL, NULL, G_TYPE_STRV, f);
  /**
   * OmarchyTree:selected-id:
   *
   * The chosen row's id (selected fill, bold), or "".
   */
  tr_props[TR_PROP_SELECTED_ID] = g_param_spec_string ("selected-id", NULL, NULL, "", f);
  /**
   * OmarchyTree:cursor-id:
   *
   * The keyboard cursor's row (hover fill), or "". The pointer moves it too.
   */
  tr_props[TR_PROP_CURSOR_ID] = g_param_spec_string ("cursor-id", NULL, NULL, "", f);
  /**
   * OmarchyTree:has-cursor:
   *
   * Show the cursor row's hover look without focus.
   */
  tr_props[TR_PROP_HAS_CURSOR] = g_param_spec_boolean ("has-cursor", NULL, NULL, FALSE, f);
  g_object_class_install_properties (oc, TR_N_PROPS, tr_props);
  /**
   * OmarchyTree::toggled:
   * @self: the tree
   * @tree: the tree
   * @id: the branch folded or unfolded (#OmarchyTree:expanded already says which)
   */
  tr_signals[TR_TOGGLED] = g_signal_new ("toggled", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                         G_TYPE_NONE, 1, G_TYPE_STRING);
  /**
   * OmarchyTree::activated:
   * @self: the tree
   * @tree: the tree
   * @id: the row chosen (now #OmarchyTree:selected-id)
   */
  tr_signals[TR_ACTIVATED] = g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                           NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  gtk_widget_class_set_css_name (wc, "omarchy-tree");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_TREE);
}

/**
 * omarchy_tree_new:
 *
 * Returns: (transfer floating): an empty tree; add nodes with omarchy_tree_append()
 */
GtkWidget *
omarchy_tree_new (void)
{
  return g_object_new (OMARCHY_TYPE_TREE, NULL);
}

/**
 * omarchy_tree_append:
 * @self: the tree
 * @parent_id: (nullable): the parent node's id, or %NULL for a top-level node
 * @id: the node's id, unique in the tree
 * @label: (nullable): the label
 * @glyph: (nullable): a Nerd Font glyph before the label
 *
 * Adds a node after its siblings. A node with children is a branch.
 */
void
omarchy_tree_append (OmarchyTree *self, const char *parent_id, const char *id, const char *label, const char *glyph)
{
  g_return_if_fail (id != NULL);
  TreeNode *parent = parent_id ? g_hash_table_lookup (self->by_id, parent_id) : NULL;
  g_return_if_fail (!parent_id || parent);
  TreeNode *n = g_new0 (TreeNode, 1);
  n->id = g_strdup (id);
  n->label = g_strdup (label ? label : "");
  n->glyph = g_strdup (glyph ? glyph : "");
  n->children = g_ptr_array_new_with_free_func (tree_node_free);
  g_ptr_array_add (parent ? parent->children : self->roots, n);
  g_hash_table_insert (self->by_id, n->id, n);
  tree_changed (self);
}

void
omarchy_tree_remove_all (OmarchyTree *self)
{
  g_hash_table_remove_all (self->by_id);
  g_ptr_array_set_size (self->roots, 0);
  self->offset = 0;
  tree_changed (self);
}

/**
 * omarchy_tree_get_expanded:
 * @self: the tree
 *
 * Returns: (transfer full) (array zero-terminated=1): the expanded branches' ids
 */
char **
omarchy_tree_get_expanded (OmarchyTree *self)
{
  char **out = g_new0 (char *, self->expanded->len + 1);
  for (guint i = 0; i < self->expanded->len; i++)
    out[i] = g_strdup (g_ptr_array_index (self->expanded, i));
  return out;
}

/**
 * omarchy_tree_set_expanded:
 * @self: the tree
 * @ids: (nullable) (array zero-terminated=1): the branches to show open
 */
void
omarchy_tree_set_expanded (OmarchyTree *self, const char *const *ids)
{
  g_ptr_array_set_size (self->expanded, 0);
  for (int i = 0; ids && ids[i]; i++)
    if (tree_expanded_index (self, ids[i]) < 0)
      g_ptr_array_add (self->expanded, g_strdup (ids[i]));
  tree_changed (self);
  g_object_notify_by_pspec (G_OBJECT (self), tr_props[TR_PROP_EXPANDED]);
}

gboolean
omarchy_tree_is_expanded (OmarchyTree *self, const char *id)
{
  return tree_expanded_index (self, id) >= 0;
}

/**
 * omarchy_tree_set_node_expanded:
 * @self: the tree
 * @id: a branch's id
 * @expanded: show its children
 */
void
omarchy_tree_set_node_expanded (OmarchyTree *self, const char *id, gboolean expanded)
{
  int at = tree_expanded_index (self, id);
  if ((at >= 0) == !!expanded)
    return;
  if (expanded)
    g_ptr_array_add (self->expanded, g_strdup (id));
  else
    g_ptr_array_remove_index (self->expanded, at);
  tree_changed (self);
  g_object_notify_by_pspec (G_OBJECT (self), tr_props[TR_PROP_EXPANDED]);
}

int
omarchy_tree_get_n_visible_rows (OmarchyTree *self)
{
  return tree_rows (self)->len;
}

/**
 * omarchy_tree_get_visible_id:
 * @self: the tree
 * @index: a visible row, from the top
 *
 * Returns: (nullable): its node's id
 */
const char *
omarchy_tree_get_visible_id (OmarchyTree *self, int index)
{
  GArray *rows = tree_rows (self);
  return index >= 0 && index < (int) rows->len ? g_array_index (rows, TreeRow, index).node->id : NULL;
}

OMARCHY_STRING_ACCESSORS (OmarchyTree, tree, selected_id, tr_props, TR_PROP_SELECTED_ID, FALSE)
OMARCHY_STRING_ACCESSORS (OmarchyTree, tree, cursor_id, tr_props, TR_PROP_CURSOR_ID, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyTree, tree, has_cursor, tr_props, TR_PROP_HAS_CURSOR, FALSE)
