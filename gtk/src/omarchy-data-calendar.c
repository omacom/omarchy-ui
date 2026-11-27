/* omarchy-data-calendar.c: Calendar (Calendar.qml), the shell clock panel's month grid
 * (spec/components/data.md). */
#include "omarchy-data-private.h"

#include <stdio.h>

#define GLYPH_PREVIOUS "\xf3\xb0\x85\x81" /* U+F0141 */
#define GLYPH_NEXT "\xf3\xb0\x85\x82"     /* U+F0142 */

static const char *const default_months[] = { "January", "February", "March", "April", "May", "June", "July",
                                              "August", "September", "October", "November", "December", NULL };
static const char *const default_weekdays[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su", NULL };

/**
 * OmarchyCalendar:
 *
 * A month grid: the month and year in capitals (`body`, `textDim`) between previous/next
 * #OmarchyIconButton steppers, a `caption` bold `textDim` row of weekdays from Monday, then six
 * weeks (always six) of `space(36)` × `controlHeight` days `xxs` apart. Today is ringed in the
 * accent, the selected day takes the selected fill, days outside the month are faint and
 * weekends dim.
 *
 * Dates are "YYYY-MM-DD" strings, so nothing reads the clock: the app passes
 * #OmarchyCalendar:today. The steppers and PageUp/PageDown step #OmarchyCalendar:year and
 * #OmarchyCalendar:month; the arrows (h/j/k/l) move #OmarchyCalendar:cursor-date by a day or a
 * week (hover fill, the pointer moves it too), following it into the next month; Return/Space
 * or a click pick a day (::picked, after #OmarchyCalendar:selected takes it).
 */
struct _OmarchyCalendar {
  GtkWidget parent_instance;
  GtkWidget *prev, *next;
  int year, month;
  char *today, *selected, *cursor_date;
  gboolean has_cursor, hovering;
  char **month_names, **weekday_names;
};

enum {
  CA_PROP_0,
  CA_PROP_YEAR,
  CA_PROP_MONTH,
  CA_PROP_TODAY,
  CA_PROP_SELECTED,
  CA_PROP_CURSOR_DATE,
  CA_PROP_HAS_CURSOR,
  CA_PROP_MONTH_NAMES,
  CA_PROP_WEEKDAY_NAMES,
  CA_N_PROPS
};
static GParamSpec *ca_props[CA_N_PROPS];
enum { CA_PICKED, CA_N_SIGNALS };
static guint ca_signals[CA_N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyCalendar, omarchy_calendar, GTK_TYPE_WIDGET)

/* The left edge that centers a layout in `w` as Qt's centerIn does: at its fractional advance. */
static float
centered (PangoLayout *l, float w)
{
  PangoRectangle logical;
  pango_layout_get_extents (l, NULL, &logical);
  return (w - logical.width / (float) PANGO_SCALE) / 2;
}

/* ---- dates -------------------------------------------------------------------------------- */

static char *
iso (const GDate *d)
{
  return g_strdup_printf ("%04d-%02d-%02d", g_date_get_year (d), g_date_get_month (d), g_date_get_day (d));
}

static gboolean
parse (const char *s, GDate *out)
{
  int y, m, d;
  if (!s || sscanf (s, "%d-%d-%d", &y, &m, &d) != 3 || !g_date_valid_dmy (d, m, y))
    return FALSE;
  g_date_clear (out, 1);
  g_date_set_dmy (out, d, m, y);
  return TRUE;
}

/* The grid's first day: the Monday on or before the 1st. */
static void
grid_start (OmarchyCalendar *self, GDate *out)
{
  g_date_clear (out, 1);
  g_date_set_dmy (out, 1, self->month, self->year);
  g_date_subtract_days (out, g_date_get_weekday (out) - G_DATE_MONDAY);
}

/* ---- metrics ------------------------------------------------------------------------------ */

typedef struct {
  int cw, ch, gap, grid_w, header_h, weekday_y, weekday_h, days_y;
} CalendarMetrics;

static void
calendar_metrics (OmarchyCalendar *self, const OmarchyTokens *t, CalendarMetrics *m)
{
  m->cw = omarchy_space (36);
  m->ch = t->control_height;
  m->gap = t->xxs;
  m->grid_w = m->cw * 7 + m->gap * 6;
  int bs;
  gtk_widget_measure (self->prev, GTK_ORIENTATION_VERTICAL, -1, NULL, &bs, NULL, NULL);
  m->header_h = bs;
  m->weekday_y = m->header_h + t->sm;
  m->weekday_h = omarchy_space (16);
  m->days_y = m->weekday_y + m->weekday_h + m->gap;
}

static void
calendar_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyCalendar *self = OMARCHY_CALENDAR (w);
  CalendarMetrics m;
  calendar_metrics (self, omarchy_tokens (), &m);
  if (o == GTK_ORIENTATION_HORIZONTAL)
    *min = *nat = m.grid_w;
  else
    *min = *nat = m.days_y + 6 * m.ch + 5 * m.gap;
}

static void
calendar_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyCalendar *self = OMARCHY_CALENDAR (w);
  CalendarMetrics m;
  calendar_metrics (self, omarchy_tokens (), &m);
  int bs = m.header_h;
  int pad = (int) lround ((m.cw - bs) / 2.0);
  gtk_widget_size_allocate (self->prev, &(GtkAllocation) { pad, 0, bs, bs }, -1);
  gtk_widget_size_allocate (self->next, &(GtkAllocation) { m.grid_w - m.cw + pad, 0, bs, bs }, -1);
}

static void
calendar_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyCalendar *self = OMARCHY_CALENDAR (w);
  const OmarchyTokens *p = omarchy_paint ();
  CalendarMetrics m;
  calendar_metrics (self, p, &m);

  /* ---- Month and year between the steppers. */
  g_autofree char *title_raw = g_strdup_printf ("%s %d", self->month_names[self->month - 1], self->year);
  g_autofree char *title = g_utf8_strup (title_raw, -1);
  g_autoptr (PangoLayout) tl = omarchy_layout (w, title, p->body, FALSE);
  omarchy_draw_layout (s, tl, centered (tl, m.grid_w), data_center_y (0, m.header_h, p->body),
                       &p->text_dim);
  gtk_widget_snapshot_child (w, self->prev, s);
  gtk_widget_snapshot_child (w, self->next, s);

  /* ---- Weekdays */
  for (int i = 0; i < 7; i++) {
    g_autofree char *name = g_utf8_strup (self->weekday_names[i], -1);
    g_autoptr (PangoLayout) l = omarchy_layout (w, name, p->caption, TRUE);
    omarchy_draw_layout (s, l, i * (m.cw + m.gap) + centered (l, m.cw),
                         data_center_y (m.weekday_y, m.weekday_h, p->caption), &p->text_dim);
  }

  /* ---- Six weeks */
  GDate today, selected, cursor, d;
  gboolean has_today = parse (self->today, &today), has_selected = parse (self->selected, &selected);
  gboolean show = data_show_cursor (w, self->has_cursor, self->hovering) && parse (self->cursor_date, &cursor);
  OmarchyBorder ring = omarchy_border_flat (&p->accent, p->hairline);
  OmarchyBorder none = omarchy_border_none ();
  grid_start (self, &d);
  for (int i = 0; i < 42; i++, g_date_add_days (&d, 1)) {
    int col = i % 7, row = i / 7;
    float x = col * (m.cw + m.gap), y = m.days_y + row * (m.ch + m.gap);
    gboolean is_today = has_today && !g_date_compare (&d, &today);
    gboolean is_selected = has_selected && !g_date_compare (&d, &selected);
    gboolean is_cursor = show && !g_date_compare (&d, &cursor);
    gboolean in_month = g_date_get_month (&d) == (GDateMonth) self->month && g_date_get_year (&d) == self->year;
    const GdkRGBA *fill = is_cursor ? &p->hover.fill : is_selected ? &p->selected.fill : NULL;
    GdkRGBA clear = { 0 };
    omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (x, y, m.cw, m.ch), p->radius, fill ? fill : &clear,
                          is_today ? &ring : &none);
    const GdkRGBA *ink = !in_month ? &p->text_faint : is_selected ? &p->selected.text : col >= 5 ? &p->text_dim : &p->foreground;
    char num[4];
    g_snprintf (num, sizeof num, "%d", g_date_get_day (&d));
    g_autoptr (PangoLayout) l = omarchy_layout (w, num, p->body, is_selected || is_today);
    omarchy_draw_layout (s, l, x + centered (l, m.cw), data_center_y (y, m.ch, p->body), ink);
  }
}

/* The day under a point, as an index into the 42 cells, or -1. */
static int
calendar_day_at (OmarchyCalendar *self, double x, double y)
{
  CalendarMetrics m;
  calendar_metrics (self, omarchy_tokens (), &m);
  if (x < 0 || y < m.days_y)
    return -1;
  int col = (int) floor (x / (m.cw + m.gap)), row = (int) floor ((y - m.days_y) / (m.ch + m.gap));
  if (col > 6 || row > 5 || x - col * (m.cw + m.gap) >= m.cw || (y - m.days_y) - row * (m.ch + m.gap) >= m.ch)
    return -1;
  return row * 7 + col;
}

static void
calendar_pick (OmarchyCalendar *self, const char *date)
{
  if (!date || !*date)
    return;
  g_autofree char *copy = g_strdup (date);
  omarchy_calendar_set_selected (self, copy);
  g_signal_emit (self, ca_signals[CA_PICKED], 0, copy);
}

static void
calendar_move_cursor (OmarchyCalendar *self, int days)
{
  GDate d;
  if (!parse (self->cursor_date, &d) && !parse (self->selected, &d) && !parse (self->today, &d)) {
    g_date_clear (&d, 1);
    g_date_set_dmy (&d, 1, self->month, self->year);
  }
  if (days > 0)
    g_date_add_days (&d, days);
  else
    g_date_subtract_days (&d, -days);
  g_autofree char *s = iso (&d);
  omarchy_calendar_set_cursor_date (self, s);
  if (g_date_get_year (&d) != self->year || (int) g_date_get_month (&d) != self->month) {
    g_object_freeze_notify (G_OBJECT (self));
    omarchy_calendar_set_year (self, g_date_get_year (&d));
    omarchy_calendar_set_month (self, g_date_get_month (&d));
    g_object_thaw_notify (G_OBJECT (self));
  }
}

static void
calendar_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyCalendar *self = OMARCHY_CALENDAR (w);
  int i = button == GDK_BUTTON_PRIMARY ? calendar_day_at (self, x, y) : -1;
  if (i < 0)
    return;
  gtk_widget_grab_focus (w);
  g_autofree char *date = omarchy_calendar_get_day_at (self, i);
  calendar_pick (self, date);
}

static gboolean
calendar_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyCalendar *self = OMARCHY_CALENDAR (w);
  switch (keyval) {
  case GDK_KEY_Left: case GDK_KEY_KP_Left: case GDK_KEY_h: calendar_move_cursor (self, -1); break;
  case GDK_KEY_Right: case GDK_KEY_KP_Right: case GDK_KEY_l: calendar_move_cursor (self, 1); break;
  case GDK_KEY_Up: case GDK_KEY_KP_Up: case GDK_KEY_k: calendar_move_cursor (self, -7); break;
  case GDK_KEY_Down: case GDK_KEY_KP_Down: case GDK_KEY_j: calendar_move_cursor (self, 7); break;
  case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: omarchy_calendar_step_month (self, -1); break;
  case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: omarchy_calendar_step_month (self, 1); break;
  default:
    if (!omarchy_key_is_activate (keyval))
      return FALSE;
    /* Return/Space on a focused stepper is the stepper's. */
    if (gtk_widget_has_focus (w))
      calendar_pick (self, self->cursor_date);
    else
      return FALSE;
  }
  return TRUE;
}

static void
calendar_motion (GtkEventControllerMotion *c, double x, double y, OmarchyCalendar *self)
{
  int i = calendar_day_at (self, x, y);
  if (i >= 0) {
    g_autofree char *date = omarchy_calendar_get_day_at (self, i);
    omarchy_calendar_set_cursor_date (self, date);
  }
  if ((i >= 0) != self->hovering) {
    self->hovering = i >= 0;
    gtk_widget_queue_draw (GTK_WIDGET (self));
  }
}

static void
calendar_leave (GtkEventControllerMotion *c, OmarchyCalendar *self)
{
  self->hovering = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
step_previous (OmarchyCalendar *self)
{
  omarchy_calendar_step_month (self, -1);
}

static void
step_next (OmarchyCalendar *self)
{
  omarchy_calendar_step_month (self, 1);
}

static void
calendar_relabel (OmarchyCalendar *self)
{
  const char *prev = omarchy_t ("clock.previousMonth", "Previous month");
  const char *next = omarchy_t ("clock.nextMonth", "Next month");
  gtk_widget_set_tooltip_text (self->prev, prev);
  gtk_widget_set_tooltip_text (self->next, next);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self->prev), GTK_ACCESSIBLE_PROPERTY_LABEL, prev, -1);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self->next), GTK_ACCESSIBLE_PROPERTY_LABEL, next, -1);
}

static void
omarchy_calendar_init (OmarchyCalendar *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->year = 2026;
  self->month = 1;
  self->today = g_strdup ("");
  self->selected = g_strdup ("");
  self->cursor_date = g_strdup ("");
  self->month_names = g_strdupv ((char **) default_months);
  self->weekday_names = g_strdupv ((char **) default_weekdays);
  self->prev = omarchy_icon_button_new (GLYPH_PREVIOUS);
  self->next = omarchy_icon_button_new (GLYPH_NEXT);
  gtk_widget_set_parent (self->prev, w);
  gtk_widget_set_parent (self->next, w);
  g_signal_connect_swapped (self->prev, "clicked", G_CALLBACK (step_previous), self);
  g_signal_connect_swapped (self->next, "clicked", G_CALLBACK (step_next), self);
  calendar_relabel (self);
  g_signal_connect_object (omarchy_theme_get_default (), "restyle", G_CALLBACK (calendar_relabel), self, G_CONNECT_SWAPPED);
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, FALSE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, calendar_clicked);
  omarchy_widget_add_keys (w, calendar_key);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "enter", G_CALLBACK (calendar_motion), self);
  g_signal_connect (m, "motion", G_CALLBACK (calendar_motion), self);
  g_signal_connect (m, "leave", G_CALLBACK (calendar_leave), self);
  gtk_widget_add_controller (w, m);
}

static void
calendar_dispose (GObject *o)
{
  OmarchyCalendar *self = OMARCHY_CALENDAR (o);
  g_clear_pointer (&self->prev, gtk_widget_unparent);
  g_clear_pointer (&self->next, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_calendar_parent_class)->dispose (o);
}

static void
calendar_finalize (GObject *o)
{
  OmarchyCalendar *self = OMARCHY_CALENDAR (o);
  g_free (self->today);
  g_free (self->selected);
  g_free (self->cursor_date);
  g_strfreev (self->month_names);
  g_strfreev (self->weekday_names);
  G_OBJECT_CLASS (omarchy_calendar_parent_class)->finalize (o);
}

static void
calendar_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyCalendar *self = OMARCHY_CALENDAR (o);
  switch (id) {
  case CA_PROP_YEAR: omarchy_calendar_set_year (self, g_value_get_int (v)); break;
  case CA_PROP_MONTH: omarchy_calendar_set_month (self, g_value_get_int (v)); break;
  case CA_PROP_TODAY: omarchy_calendar_set_today (self, g_value_get_string (v)); break;
  case CA_PROP_SELECTED: omarchy_calendar_set_selected (self, g_value_get_string (v)); break;
  case CA_PROP_CURSOR_DATE: omarchy_calendar_set_cursor_date (self, g_value_get_string (v)); break;
  case CA_PROP_HAS_CURSOR: omarchy_calendar_set_has_cursor (self, g_value_get_boolean (v)); break;
  case CA_PROP_MONTH_NAMES: omarchy_calendar_set_month_names (self, g_value_get_boxed (v)); break;
  case CA_PROP_WEEKDAY_NAMES: omarchy_calendar_set_weekday_names (self, g_value_get_boxed (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
calendar_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyCalendar *self = OMARCHY_CALENDAR (o);
  switch (id) {
  case CA_PROP_YEAR: g_value_set_int (v, self->year); break;
  case CA_PROP_MONTH: g_value_set_int (v, self->month); break;
  case CA_PROP_TODAY: g_value_set_string (v, self->today); break;
  case CA_PROP_SELECTED: g_value_set_string (v, self->selected); break;
  case CA_PROP_CURSOR_DATE: g_value_set_string (v, self->cursor_date); break;
  case CA_PROP_HAS_CURSOR: g_value_set_boolean (v, self->has_cursor); break;
  case CA_PROP_MONTH_NAMES: g_value_set_boxed (v, self->month_names); break;
  case CA_PROP_WEEKDAY_NAMES: g_value_set_boxed (v, self->weekday_names); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_calendar_class_init (OmarchyCalendarClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = calendar_dispose;
  oc->finalize = calendar_finalize;
  oc->set_property = calendar_set_property;
  oc->get_property = calendar_get_property;
  wc->measure = calendar_measure;
  wc->size_allocate = calendar_allocate;
  wc->snapshot = calendar_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyCalendar:year:
   *
   * The year shown.
   */
  ca_props[CA_PROP_YEAR] = g_param_spec_int ("year", NULL, NULL, 1, 9999, 2026, f);
  /**
   * OmarchyCalendar:month:
   *
   * The month shown, 1–12.
   */
  ca_props[CA_PROP_MONTH] = g_param_spec_int ("month", NULL, NULL, 1, 12, 1, f);
  /**
   * OmarchyCalendar:today:
   *
   * Today as "YYYY-MM-DD" (ringed in the accent), or "".
   */
  ca_props[CA_PROP_TODAY] = g_param_spec_string ("today", NULL, NULL, "", f);
  /**
   * OmarchyCalendar:selected:
   *
   * The picked day as "YYYY-MM-DD" (selected fill, bold), or "".
   */
  ca_props[CA_PROP_SELECTED] = g_param_spec_string ("selected", NULL, NULL, "", f);
  /**
   * OmarchyCalendar:cursor-date:
   *
   * The keyboard cursor's day (hover fill) as "YYYY-MM-DD", or "".
   */
  ca_props[CA_PROP_CURSOR_DATE] = g_param_spec_string ("cursor-date", NULL, NULL, "", f);
  /**
   * OmarchyCalendar:has-cursor:
   *
   * Show the cursor day's hover look without focus.
   */
  ca_props[CA_PROP_HAS_CURSOR] = g_param_spec_boolean ("has-cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyCalendar:month-names:
   *
   * Twelve month names, January first (English by default).
   */
  ca_props[CA_PROP_MONTH_NAMES] = g_param_spec_boxed ("month-names", NULL, NULL, G_TYPE_STRV, f);
  /**
   * OmarchyCalendar:weekday-names:
   *
   * Seven weekday names, Monday first (English by default); shown in capitals.
   */
  ca_props[CA_PROP_WEEKDAY_NAMES] = g_param_spec_boxed ("weekday-names", NULL, NULL, G_TYPE_STRV, f);
  g_object_class_install_properties (oc, CA_N_PROPS, ca_props);
  /**
   * OmarchyCalendar::picked:
   * @self: the calendar
   * @calendar: the calendar
   * @date: the day picked, "YYYY-MM-DD" (now #OmarchyCalendar:selected)
   */
  ca_signals[CA_PICKED] = g_signal_new ("picked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                        G_TYPE_NONE, 1, G_TYPE_STRING);
  gtk_widget_class_set_css_name (wc, "omarchy-calendar");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_GRID);
}

/**
 * omarchy_calendar_new:
 * @year: the year shown
 * @month: the month shown, 1–12
 *
 * Returns: (transfer floating): a month grid
 */
GtkWidget *
omarchy_calendar_new (int year, int month)
{
  return g_object_new (OMARCHY_TYPE_CALENDAR, "year", year, "month", CLAMP (month, 1, 12), NULL);
}

int
omarchy_calendar_get_year (OmarchyCalendar *self)
{
  return self->year;
}

void
omarchy_calendar_set_year (OmarchyCalendar *self, int year)
{
  year = CLAMP (year, 1, 9999);
  if (self->year == year)
    return;
  self->year = year;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), ca_props[CA_PROP_YEAR]);
}

int
omarchy_calendar_get_month (OmarchyCalendar *self)
{
  return self->month;
}

void
omarchy_calendar_set_month (OmarchyCalendar *self, int month)
{
  month = CLAMP (month, 1, 12);
  if (self->month == month)
    return;
  self->month = month;
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), ca_props[CA_PROP_MONTH]);
}

/**
 * omarchy_calendar_step_month:
 * @self: the calendar
 * @delta: months to step, back when negative
 *
 * What the steppers and PageUp/PageDown do.
 */
void
omarchy_calendar_step_month (OmarchyCalendar *self, int delta)
{
  int m = (self->month - 1) + delta;
  int years = m >= 0 ? m / 12 : -((-m + 11) / 12);
  g_object_freeze_notify (G_OBJECT (self));
  omarchy_calendar_set_year (self, self->year + years);
  omarchy_calendar_set_month (self, ((m % 12) + 12) % 12 + 1);
  g_object_thaw_notify (G_OBJECT (self));
}

static void
set_names (char ***field, const char *const *names, int n, const char *const *fallback)
{
  int len = names ? (int) g_strv_length ((char **) names) : 0;
  g_strfreev (*field);
  *field = g_strdupv ((char **) (len >= n ? names : fallback));
}

/**
 * omarchy_calendar_set_month_names:
 * @self: the calendar
 * @names: (nullable) (array zero-terminated=1): twelve names, January first; %NULL for English
 */
void
omarchy_calendar_set_month_names (OmarchyCalendar *self, const char *const *names)
{
  set_names (&self->month_names, names, 12, default_months);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), ca_props[CA_PROP_MONTH_NAMES]);
}

/**
 * omarchy_calendar_set_weekday_names:
 * @self: the calendar
 * @names: (nullable) (array zero-terminated=1): seven names, Monday first; %NULL for English
 */
void
omarchy_calendar_set_weekday_names (OmarchyCalendar *self, const char *const *names)
{
  set_names (&self->weekday_names, names, 7, default_weekdays);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), ca_props[CA_PROP_WEEKDAY_NAMES]);
}

/**
 * omarchy_calendar_get_day_at:
 * @self: the calendar
 * @index: a cell of the grid, 0–41 from the top-left Monday
 *
 * Returns: (transfer full) (nullable): its date, "YYYY-MM-DD"
 */
char *
omarchy_calendar_get_day_at (OmarchyCalendar *self, int index)
{
  if (index < 0 || index >= 42)
    return NULL;
  GDate d;
  grid_start (self, &d);
  g_date_add_days (&d, index);
  return iso (&d);
}

OMARCHY_STRING_ACCESSORS (OmarchyCalendar, calendar, today, ca_props, CA_PROP_TODAY, FALSE)
OMARCHY_STRING_ACCESSORS (OmarchyCalendar, calendar, selected, ca_props, CA_PROP_SELECTED, FALSE)
OMARCHY_STRING_ACCESSORS (OmarchyCalendar, calendar, cursor_date, ca_props, CA_PROP_CURSOR_DATE, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyCalendar, calendar, has_cursor, ca_props, CA_PROP_HAS_CURSOR, FALSE)
