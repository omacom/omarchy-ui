/* omarchy-forms-multi.c: MultiSelect (MultiSelect.qml): a searchable multi-choice dropdown. */
#include "omarchy-forms-private.h"

#include <math.h>
#include <string.h>

/**
 * OmarchyMultiSelect:
 *
 * Pick zero or more of a list. The trigger is a Dropdown's (`searchableDropdownWidth` ×
 * `controlHeight`, the label `caption` bold in `textDim` above, a chevron at the end) reading
 * the chosen labels joined with ", ", "N selected" past three, or "None selected" in
 * `textDim`. Return, Space, Down or j open the popup `xxs` below on the popups surface: a
 * search field, a rule, then a check row per option. Typing filters; in the search Return
 * toggles the first match and Down goes into the list; in the list j/k and Down/Up walk (Up off
 * the first row goes back to the search), Return or Space toggle; Escape closes. A click on a
 * row toggles it. Every change sets #OmarchyMultiSelect:values and emits ::changed.
 *
 * Options are a plain list ([method@Omarchy.MultiSelect.append]); an app that loads them sets
 * #OmarchyMultiSelect:loading-options or #OmarchyMultiSelect:options-error itself, and sets
 * #OmarchyMultiSelect:refreshable for a refresh button that emits ::refresh-requested.
 */
typedef struct {
  char *value, *label, *description;
} Option;

struct _OmarchyMultiSelect {
  GtkWidget parent_instance;
  char *label, *placeholder, *empty_text, *no_selection_text, *trigger_label, *options_error;
  gboolean show_label, cursor, refreshable, loading, inline_popup, inline_open;
  GArray *options;  /* Option */
  GPtrArray *values;
  GArray *filtered; /* int: indices into options */
  int current;      /* into filtered, -1 for none */
  GtkPopover *popover;
  GtkWidget *popup;
};

enum {
  PROP_0, PROP_LABEL, PROP_VALUES, PROP_PLACEHOLDER, PROP_EMPTY_TEXT, PROP_NO_SELECTION_TEXT, PROP_TRIGGER_LABEL,
  PROP_SHOW_LABEL, PROP_CURSOR, PROP_REFRESHABLE, PROP_LOADING, PROP_OPTIONS_ERROR, PROP_INLINE_POPUP,
  PROP_POPUP_OPEN, N_PROPS
};
static GParamSpec *props[N_PROPS];
enum { CHANGED, HOVERED, REFRESH_REQUESTED, N_SIGNALS };
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (OmarchyMultiSelect, omarchy_multi_select, GTK_TYPE_WIDGET)

/* The popup: the popups surface, the search field, the refresh button, the rule and the list. */
#define OMARCHY_TYPE_MULTI_SELECT_POPUP (omarchy_multi_select_popup_get_type ())
G_DECLARE_FINAL_TYPE (OmarchyMultiSelectPopup, omarchy_multi_select_popup, OMARCHY, MULTI_SELECT_POPUP, GtkWidget)
struct _OmarchyMultiSelectPopup {
  GtkWidget parent_instance;
  OmarchyMultiSelect *owner;
  GtkWidget *search, *list;
  gboolean refresh_hot;
};
G_DEFINE_FINAL_TYPE (OmarchyMultiSelectPopup, omarchy_multi_select_popup, GTK_TYPE_WIDGET)

/* The rows, scrolled. */
#define OMARCHY_TYPE_MULTI_SELECT_LIST (omarchy_multi_select_list_get_type ())
G_DECLARE_FINAL_TYPE (OmarchyMultiSelectList, omarchy_multi_select_list, OMARCHY, MULTI_SELECT_LIST, GtkWidget)
struct _OmarchyMultiSelectList {
  GtkWidget parent_instance;
  OmarchyMultiSelect *owner;
  double scroll;
};
G_DEFINE_FINAL_TYPE (OmarchyMultiSelectList, omarchy_multi_select_list, GTK_TYPE_WIDGET)

static const char CHEVRON[] = "\xf3\xb0\x85\x80";   /* U+F0140 */
static const char REFRESH[] = "\xf3\xb0\x91\x90";   /* U+F0450 */
static const char LOADING[] = "\xf3\xb0\xa6\x96";   /* U+F0996 */

static Option *
option_at (OmarchyMultiSelect *self, int filtered_index)
{
  int i = g_array_index (self->filtered, int, filtered_index);
  return &g_array_index (self->options, Option, i);
}

static int
n_filtered (OmarchyMultiSelect *self)
{
  return (int) self->filtered->len;
}

/* ---- geometry ---------------------------------------------------------------------------- */

static int
label_h (OmarchyMultiSelect *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return self->show_label && self->label && *self->label ? omarchy_line_height (t->caption) + t->label_gap : 0;
}

static graphene_rect_t
trigger_rect (OmarchyMultiSelect *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return GRAPHENE_RECT_INIT (0, label_h (self), gtk_widget_get_width (GTK_WIDGET (self)), t->control_height);
}

static OmarchyBorder
popup_border (void)
{
  const OmarchyTokens *p = omarchy_paint ();
  return omarchy_border_with_width (&p->popups.border, p->normal.border.widths[0]);
}

static int
row_h (const Option *o)
{
  const OmarchyTokens *t = omarchy_tokens ();
  int content = omarchy_line_height (t->body);
  if (o->description && *o->description)
    content += t->xxs + omarchy_line_height (t->caption);
  return MAX (t->popup_row_height, content + t->row_padding_x);
}

static int
row_y (OmarchyMultiSelect *self, int index)
{
  int y = 0;
  for (int i = 0; i < index; i++)
    y += row_h (option_at (self, i)) + omarchy_tokens ()->label_gap;
  return y;
}

static int
list_content_h (OmarchyMultiSelect *self)
{
  int n = n_filtered (self);
  return n ? row_y (self, n - 1) + row_h (option_at (self, n - 1)) : 0;
}

static int
header_h (void)
{
  const OmarchyTokens *t = omarchy_tokens ();
  return t->popup_row_height + t->control_padding_x;
}

static int
popup_h (OmarchyMultiSelect *self)
{
  const OmarchyTokens *t = omarchy_tokens ();
  OmarchyBorder b = popup_border ();
  int chrome = header_h () + 1 + (int) (b.widths[0] + b.widths[2]) + 2 * t->hairline;
  int ideal = MAX (t->popup_row_height, list_content_h (self)) + chrome;
  int most = t->popup_row_height * 6 + 5 * t->label_gap + chrome;
  return MIN (ideal, most);
}

typedef struct {
  float x0, y0, cw, bottom;
  graphene_rect_t search, refresh, list;
  float rule_y;
} PopGeo;

static PopGeo
pop_geo (OmarchyMultiSelectPopup *pop, float W, float H)
{
  const OmarchyTokens *t = omarchy_tokens ();
  OmarchyBorder b = popup_border ();
  PopGeo g;
  g.x0 = b.widths[3] + t->hairline;
  g.y0 = b.widths[0] + t->hairline;
  g.cw = W - g.x0 - b.widths[1] - t->hairline;
  g.bottom = H - b.widths[2] - t->hairline;
  int hh = header_h ();
  float inner = hh - 2 * t->md;
  gboolean refresh = pop->owner->refreshable;
  float sw = g.cw - 2 * t->md - (refresh ? inner + t->row_gap : 0);
  gboolean rtl = gtk_widget_get_direction (GTK_WIDGET (pop)) == GTK_TEXT_DIR_RTL;
  float left = g.x0 + t->md, right = g.x0 + g.cw - t->md;
  g.search = GRAPHENE_RECT_INIT (rtl ? right - sw : left, g.y0 + t->md, MAX (0, sw), inner);
  g.refresh = GRAPHENE_RECT_INIT (rtl ? left : right - inner, g.y0 + t->md, refresh ? inner : 0, inner);
  g.rule_y = g.y0 + hh;
  g.list = GRAPHENE_RECT_INIT (g.x0, g.rule_y + 1, g.cw, MAX (0, g.bottom - g.rule_y - 1));
  return g;
}

/* ---- selection --------------------------------------------------------------------------- */

static int
value_index (OmarchyMultiSelect *self, const char *value)
{
  for (guint i = 0; i < self->values->len; i++)
    if (!g_strcmp0 (g_ptr_array_index (self->values, i), value))
      return (int) i;
  return -1;
}

static char *
selection_label (OmarchyMultiSelect *self)
{
  guint n = self->values->len;
  if (n == 0)
    return NULL;
  if (n > 3) {
    char num[16];
    g_snprintf (num, sizeof num, "%u", n);
    return omarchy_theme_format (omarchy_theme_get_default (), "multiSelect.selectedCount", "%1 selected", num);
  }
  GString *out = g_string_new (NULL);
  for (guint i = 0; i < n; i++) {
    const char *v = g_ptr_array_index (self->values, i);
    const char *label = v;
    for (guint j = 0; j < self->options->len; j++) {
      Option *o = &g_array_index (self->options, Option, j);
      if (!g_strcmp0 (o->value, v))
        label = o->label;
    }
    if (i)
      g_string_append (out, ", ");
    g_string_append (out, label);
  }
  return g_string_free (out, FALSE);
}

static void
emit_changed (OmarchyMultiSelect *self)
{
  g_auto (GStrv) values = omarchy_multi_select_get_values (self);
  g_signal_emit (self, signals[CHANGED], 0, values);
}

static void
redraw_all (OmarchyMultiSelect *self)
{
  gtk_widget_queue_draw (GTK_WIDGET (self));
  if (self->popup) {
    gtk_widget_queue_draw (self->popup);
    gtk_widget_queue_draw (OMARCHY_MULTI_SELECT_POPUP (self->popup)->list);
  }
}

/**
 * omarchy_multi_select_toggle_value:
 * @self: the multi-select
 * @value: an option's value
 *
 * Choose @value, or drop it when chosen, and emit ::changed (as a click on its row does).
 */
void
omarchy_multi_select_toggle_value (OmarchyMultiSelect *self, const char *value)
{
  int i = value_index (self, value);
  if (i < 0)
    g_ptr_array_add (self->values, g_strdup (value));
  else
    g_ptr_array_remove_index (self->values, (guint) i);
  redraw_all (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_VALUES]);
  emit_changed (self);
}

static void
toggle_current (OmarchyMultiSelect *self)
{
  if (self->current < 0 || self->current >= n_filtered (self))
    return;
  omarchy_multi_select_toggle_value (self, option_at (self, self->current)->value);
}

static gboolean
contains_casefold (const char *hay, const char *needle)
{
  if (!hay)
    return FALSE;
  g_autofree char *h = g_utf8_strdown (hay, -1);
  return strstr (h, needle) != NULL;
}

static const char *
search_text (OmarchyMultiSelect *self)
{
  return self->popup ? gtk_editable_get_text (GTK_EDITABLE (OMARCHY_MULTI_SELECT_POPUP (self->popup)->search)) : "";
}

static void
refilter (OmarchyMultiSelect *self)
{
  g_array_set_size (self->filtered, 0);
  g_autofree char *q = g_utf8_strdown (search_text (self), -1);
  for (guint i = 0; i < self->options->len; i++) {
    Option *o = &g_array_index (self->options, Option, i);
    if (!*q || contains_casefold (o->label, q) || contains_casefold (o->description, q) || contains_casefold (o->value, q)) {
      int idx = (int) i;
      g_array_append_val (self->filtered, idx);
    }
  }
  if (self->current >= n_filtered (self))
    self->current = n_filtered (self) - 1;
  if (self->popup) {
    OMARCHY_MULTI_SELECT_LIST (OMARCHY_MULTI_SELECT_POPUP (self->popup)->list)->scroll = 0;
    gtk_widget_queue_resize (self->popup);
  }
  if (self->inline_open)
    gtk_widget_queue_resize (GTK_WIDGET (self));
  redraw_all (self);
}

/* ---- the list ---------------------------------------------------------------------------- */

static void
list_clamp_scroll (OmarchyMultiSelectList *list)
{
  double most = MAX (0, list_content_h (list->owner) - gtk_widget_get_height (GTK_WIDGET (list)));
  list->scroll = CLAMP (list->scroll, 0, most);
}

static void
list_show_current (OmarchyMultiSelect *self)
{
  if (!self->popup || self->current < 0)
    return;
  OmarchyMultiSelectList *list = OMARCHY_MULTI_SELECT_LIST (OMARCHY_MULTI_SELECT_POPUP (self->popup)->list);
  int top = row_y (self, self->current), bottom = top + row_h (option_at (self, self->current));
  int h = gtk_widget_get_height (GTK_WIDGET (list));
  if (top < list->scroll)
    list->scroll = top;
  else if (bottom > list->scroll + h)
    list->scroll = bottom - h;
  list_clamp_scroll (list);
  gtk_widget_queue_draw (GTK_WIDGET (list));
}

static int
list_index_at (OmarchyMultiSelectList *list, double y)
{
  OmarchyMultiSelect *self = list->owner;
  y += list->scroll;
  int top = 0;
  for (int i = 0; i < n_filtered (self); i++) {
    int h = row_h (option_at (self, i));
    if (y >= top && y < top + h)
      return i;
    top += h + omarchy_tokens ()->label_gap;
  }
  return -1;
}

static void
list_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  *min = *nat = 0;
}

static void
list_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyMultiSelectList *list = OMARCHY_MULTI_SELECT_LIST (w);
  OmarchyMultiSelect *self = list->owner;
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  int box = omarchy_space (16), line = omarchy_line_height (p->body), cap = omarchy_line_height (p->caption);
  float box_r = omarchy_radius_small ();
  float words_w = MAX (1, W - 2 * p->control_padding_x - box - p->row_gap);
  float y = (float) -list->scroll;
  for (int i = 0; i < n_filtered (self); i++) {
    Option *o = option_at (self, i);
    int h = row_h (o);
    if (y + h >= 0 && y < gtk_widget_get_height (w)) {
      gboolean cur = i == self->current, chosen = value_index (self, o->value) >= 0;
      if (cur)
        omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (0, y, W, h), box_r, &p->hover.fill);
      gboolean desc = o->description && *o->description;
      int words_h = line + (desc ? p->xxs + cap : 0);
      float content_h = MAX (box, words_h);
      float cy = y + roundf ((h - content_h) / 2);
      float bx = rtl ? W - p->control_padding_x - box : p->control_padding_x;
      graphene_rect_t r = GRAPHENE_RECT_INIT (bx, cy + roundf ((content_h - box) / 2), box, box);
      if (chosen) {
        omarchy_draw_surface (s, &r, box_r, &p->selected.fill, &p->selected.border);
        omarchy_forms_draw_check (w, s, &r, &p->selected.text);
      } else {
        omarchy_draw_surface (s, &r, box_r, NULL, &p->normal.border);
      }
      float wx = rtl ? p->control_padding_x : p->control_padding_x + box + p->row_gap;
      float wy = cy + roundf ((content_h - words_h) / 2);
      g_autoptr (PangoLayout) l = omarchy_layout (w, o->label, p->body, FALSE);
      pango_layout_set_width (l, (int) (words_w * PANGO_SCALE));
      pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
      if (rtl)
        pango_layout_set_alignment (l, PANGO_ALIGN_RIGHT);
      omarchy_draw_layout (s, l, wx, wy, cur ? &p->hover.text : &p->popups.text);
      if (desc) {
        g_autoptr (PangoLayout) d = omarchy_layout (w, o->description, p->caption, FALSE);
        pango_layout_set_width (d, (int) (words_w * PANGO_SCALE));
        pango_layout_set_ellipsize (d, PANGO_ELLIPSIZE_END);
        if (rtl)
          pango_layout_set_alignment (d, PANGO_ALIGN_RIGHT);
        omarchy_draw_layout (s, d, wx, wy + line + p->xxs, &p->text_dim);
      }
    }
    y += h + p->label_gap;
  }
}

static void
list_motion (GtkEventControllerMotion *c, double x, double y, OmarchyMultiSelectList *list)
{
  int i = list_index_at (list, y);
  if (i >= 0 && i != list->owner->current) {
    list->owner->current = i;
    gtk_widget_queue_draw (GTK_WIDGET (list));
  }
}

static void
list_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyMultiSelectList *list = OMARCHY_MULTI_SELECT_LIST (w);
  int i = list_index_at (list, y);
  if (button == GDK_BUTTON_PRIMARY && i >= 0)
    omarchy_multi_select_toggle_value (list->owner, option_at (list->owner, i)->value);
}

static gboolean
list_scrolled (GtkEventControllerScroll *c, double dx, double dy, OmarchyMultiSelectList *list)
{
  list->scroll += dy * omarchy_tokens ()->popup_row_height;
  list_clamp_scroll (list);
  gtk_widget_queue_draw (GTK_WIDGET (list));
  return TRUE;
}

static void
omarchy_multi_select_list_init (OmarchyMultiSelectList *list)
{
  GtkWidget *w = GTK_WIDGET (list);
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_overflow (w, GTK_OVERFLOW_HIDDEN);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, list_clicked);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "motion", G_CALLBACK (list_motion), list);
  gtk_widget_add_controller (w, m);
  GtkEventController *sc = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect (sc, "scroll", G_CALLBACK (list_scrolled), list);
  gtk_widget_add_controller (w, sc);
}

static void
omarchy_multi_select_list_class_init (OmarchyMultiSelectListClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  wc->measure = list_measure;
  wc->snapshot = list_snapshot;
  gtk_widget_class_set_css_name (wc, "omarchy-multi-select-list");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_LIST_BOX);
}

/* ---- the popup --------------------------------------------------------------------------- */

static void
pop_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyMultiSelectPopup *pop = OMARCHY_MULTI_SELECT_POPUP (w);
  if (o == GTK_ORIENTATION_HORIZONTAL)
    *min = *nat = MAX (1, gtk_widget_get_width (GTK_WIDGET (pop->owner)));
  else
    *min = *nat = popup_h (pop->owner);
}

static void
pop_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyMultiSelectPopup *pop = OMARCHY_MULTI_SELECT_POPUP (w);
  PopGeo g = pop_geo (pop, width, height);
  int m;
  gtk_widget_measure (pop->search, GTK_ORIENTATION_VERTICAL, -1, &m, NULL, NULL, NULL);
  gtk_widget_measure (pop->search, GTK_ORIENTATION_HORIZONTAL, -1, &m, NULL, NULL, NULL);
  gtk_widget_size_allocate (pop->search,
                            &(GtkAllocation) { (int) g.search.origin.x, (int) g.search.origin.y,
                                               (int) g.search.size.width, (int) g.search.size.height },
                            -1);
  gtk_widget_measure (pop->list, GTK_ORIENTATION_VERTICAL, -1, &m, NULL, NULL, NULL);
  gtk_widget_size_allocate (pop->list,
                            &(GtkAllocation) { (int) g.list.origin.x, (int) g.list.origin.y, (int) g.list.size.width,
                                               (int) g.list.size.height },
                            -1);
  list_clamp_scroll (OMARCHY_MULTI_SELECT_LIST (pop->list));
}

static void
pop_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyMultiSelectPopup *pop = OMARCHY_MULTI_SELECT_POPUP (w);
  OmarchyMultiSelect *self = pop->owner;
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  OmarchyBorder b = popup_border ();
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, W, H), p->radius, &p->popups.background, &b);
  PopGeo g = pop_geo (pop, W, H);
  gtk_widget_snapshot_child (w, pop->search, s);
  if (self->refreshable) {
    gboolean hot = pop->refresh_hot && !self->loading;
    omarchy_draw_surface (s, &g.refresh, p->radius, hot ? &p->hover.fill : &p->normal.fill,
                          hot ? &p->hover.border : &p->normal.border);
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->loading ? LOADING : REFRESH, p->body, FALSE);
    int gw, gh, off = omarchy_glyph_box (l, &gw, NULL);
    gh = omarchy_line_height (p->body);
    omarchy_draw_layout (s, l, g.refresh.origin.x + roundf ((g.refresh.size.width - gw) / 2) + off,
                         g.refresh.origin.y + roundf ((g.refresh.size.height - gh) / 2), &p->popups.text);
  }
  GdkRGBA rule = omarchy_rgba_alpha (&p->popups.text, 0.10);
  omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (g.x0, g.rule_y, g.cw, 1), 0, &rule);
  if (n_filtered (self) == 0) {
    const char *text = self->loading ? omarchy_t ("common.loading", "Loading...")
                       : self->options_error && *self->options_error ? self->options_error
                                                                      : omarchy_multi_select_get_empty_text (self);
    g_autoptr (PangoLayout) l = omarchy_layout (w, text, p->body, FALSE);
    int tw;
    omarchy_layout_size (l, &tw, NULL);
    int th = omarchy_line_height (p->body);
    float area_h = MAX (0.0f, g.list.size.height);
    omarchy_draw_layout (s, l, g.x0 + roundf ((g.cw - tw) / 2), g.rule_y + 1 + roundf ((area_h - th) / 2), &p->text_faint);
  }
  gtk_widget_snapshot_child (w, pop->list, s);
}

static gboolean
pop_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyMultiSelectPopup *pop = OMARCHY_MULTI_SELECT_POPUP (w);
  OmarchyMultiSelect *self = pop->owner;
  int n = n_filtered (self);
  if (keyval == GDK_KEY_Escape) {
    omarchy_multi_select_close (self);
    return TRUE;
  }
  if (!gtk_widget_is_focus (pop->list)) {
    /* The search field. */
    if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down) {
      if (n > 0) {
        self->current = 0;
        list_show_current (self);
        gtk_widget_grab_focus (pop->list);
      }
      return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_ISO_Enter) {
      if (n > 0) {
        self->current = 0;
        list_show_current (self);
        toggle_current (self);
      }
      return TRUE;
    }
    return FALSE;
  }
  if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down || keyval == GDK_KEY_j) {
    if (self->current < n - 1) {
      self->current++;
      list_show_current (self);
    }
    return TRUE;
  }
  if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up || keyval == GDK_KEY_k) {
    if (self->current <= 0)
      gtk_widget_grab_focus (pop->search);
    else
      self->current--;
    list_show_current (self);
    return TRUE;
  }
  if (omarchy_key_is_activate (keyval)) {
    toggle_current (self);
    return TRUE;
  }
  return FALSE;
}

static gboolean
pop_on_refresh (OmarchyMultiSelectPopup *pop, double x, double y)
{
  if (!pop->owner->refreshable)
    return FALSE;
  PopGeo g = pop_geo (pop, gtk_widget_get_width (GTK_WIDGET (pop)), gtk_widget_get_height (GTK_WIDGET (pop)));
  return graphene_rect_contains_point (&g.refresh, &GRAPHENE_POINT_INIT (x, y));
}

static void
pop_motion (GtkEventControllerMotion *c, double x, double y, OmarchyMultiSelectPopup *pop)
{
  gboolean hot = pop_on_refresh (pop, x, y);
  if (hot != pop->refresh_hot) {
    pop->refresh_hot = hot;
    gtk_widget_set_cursor_from_name (GTK_WIDGET (pop), hot ? "pointer" : NULL);
    gtk_widget_queue_draw (GTK_WIDGET (pop));
  }
}

static void
pop_leave (GtkEventControllerMotion *c, OmarchyMultiSelectPopup *pop)
{
  pop->refresh_hot = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (pop));
}

static void
pop_released (GtkGestureClick *g, int n, double x, double y, OmarchyMultiSelectPopup *pop)
{
  if (pop_on_refresh (pop, x, y) && !pop->owner->loading)
    g_signal_emit (pop->owner, signals[REFRESH_REQUESTED], 0);
}

static void
pop_search_changed (GtkEditable *e, OmarchyMultiSelectPopup *pop)
{
  OmarchyMultiSelect *self = pop->owner;
  refilter (self);
  self->current = n_filtered (self) > 0 ? 0 : -1;
  redraw_all (self);
}

static void
omarchy_multi_select_popup_init (OmarchyMultiSelectPopup *pop)
{
  GtkWidget *w = GTK_WIDGET (pop);
  pop->search = omarchy_text_field_new ();
  gtk_widget_set_parent (pop->search, w);
  g_signal_connect (pop->search, "changed", G_CALLBACK (pop_search_changed), pop);
  pop->list = g_object_new (OMARCHY_TYPE_MULTI_SELECT_LIST, NULL);
  gtk_widget_set_parent (pop->list, w);
  omarchy_widget_follow (w);
  GtkEventController *keys = omarchy_widget_add_keys (w, pop_key);
  gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
  GtkEventController *m = gtk_event_controller_motion_new ();
  g_signal_connect (m, "motion", G_CALLBACK (pop_motion), pop);
  g_signal_connect (m, "leave", G_CALLBACK (pop_leave), pop);
  gtk_widget_add_controller (w, m);
  GtkGesture *click = gtk_gesture_click_new ();
  g_signal_connect (click, "released", G_CALLBACK (pop_released), pop);
  gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (click));
}

static void
pop_dispose (GObject *o)
{
  OmarchyMultiSelectPopup *pop = OMARCHY_MULTI_SELECT_POPUP (o);
  g_clear_pointer (&pop->search, gtk_widget_unparent);
  g_clear_pointer (&pop->list, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_multi_select_popup_parent_class)->dispose (o);
}

static void
omarchy_multi_select_popup_class_init (OmarchyMultiSelectPopupClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = pop_dispose;
  wc->measure = pop_measure;
  wc->size_allocate = pop_allocate;
  wc->snapshot = pop_snapshot;
  gtk_widget_class_set_css_name (wc, "omarchy-multi-select-popup");
}

/* ---- the trigger ------------------------------------------------------------------------- */

static void
ms_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyMultiSelect *self = OMARCHY_MULTI_SELECT (w);
  const OmarchyTokens *t = omarchy_tokens ();
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = omarchy_space (80);
    *nat = t->searchable_dropdown_width;
    return;
  }
  *min = *nat = t->control_height + label_h (self) + (self->inline_open ? t->xxs + popup_h (self) : 0);
}

static void
ms_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyMultiSelect *self = OMARCHY_MULTI_SELECT (w);
  if (self->popover)
    gtk_popover_present (self->popover);
  if (self->inline_open && self->popup) {
    graphene_rect_t r = trigger_rect (self);
    int y = (int) (r.origin.y + r.size.height) + omarchy_tokens ()->xxs;
    int m;
    gtk_widget_measure (self->popup, GTK_ORIENTATION_VERTICAL, width, &m, NULL, NULL, NULL);
    gtk_widget_size_allocate (self->popup, &(GtkAllocation) { 0, y, width, popup_h (self) }, -1);
  }
}

static void
ms_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyMultiSelect *self = OMARCHY_MULTI_SELECT (w);
  const OmarchyTokens *p = omarchy_paint ();
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  float W = gtk_widget_get_width (w);
  if (label_h (self)) {
    g_autoptr (PangoLayout) l = omarchy_layout (w, self->label, p->caption, TRUE);
    int lw;
    omarchy_layout_size (l, &lw, NULL);
    omarchy_draw_layout (s, l, rtl ? W - lw : 0, 0, &p->text_dim);
  }
  graphene_rect_t r = trigger_rect (self);
  GtkStateFlags f = gtk_widget_get_state_flags (w);
  gboolean focused = (f & GTK_STATE_FLAG_FOCUSED) && (f & GTK_STATE_FLAG_FOCUS_VISIBLE);
  gboolean hot = self->cursor || (f & GTK_STATE_FLAG_PRELIGHT);
  const GdkRGBA *fill = focused ? &p->focus.fill : hot ? &p->hover.fill : &p->normal.fill;
  const OmarchyBorder *b = focused ? &p->focus.border : hot ? &p->hover.border : &p->normal.border;
  omarchy_draw_surface (s, &r, p->radius, fill, b);

  int line = omarchy_line_height (p->body);
  g_autoptr (PangoLayout) chevron = omarchy_layout (w, CHEVRON, p->body, FALSE);
  int cw;
  omarchy_layout_size (chevron, &cw, NULL);
  float cy = r.origin.y + roundf ((r.size.height - line) / 2);
  float cx = rtl ? b->widths[3] + p->control_gap : W - b->widths[1] - p->control_gap - cw;
  omarchy_draw_layout (s, chevron, cx, cy, &p->text_dim);

  g_autofree char *chosen = selection_label (self);
  const char *text = chosen ? chosen
                     : self->trigger_label && *self->trigger_label ? self->trigger_label
                                                                    : omarchy_multi_select_get_no_selection_text (self);
  g_autoptr (PangoLayout) tl = omarchy_layout (w, text, p->body, FALSE);
  float left = b->widths[3] + p->control_padding_x;
  float room = W - left - b->widths[1] - p->md - cw - p->control_gap;
  pango_layout_set_width (tl, (int) (MAX (1, room) * PANGO_SCALE));
  pango_layout_set_ellipsize (tl, PANGO_ELLIPSIZE_END);
  if (rtl)
    pango_layout_set_alignment (tl, PANGO_ALIGN_RIGHT);
  omarchy_draw_layout (s, tl, rtl ? W - left - room : left, cy, chosen ? &p->popups.text : &p->text_dim);
  if (self->inline_open && self->popup)
    gtk_widget_snapshot_child (w, self->popup, s);
}

static void
ms_clicked (GtkWidget *w, guint button, double x, double y)
{
  OmarchyMultiSelect *self = OMARCHY_MULTI_SELECT (w);
  graphene_rect_t r = trigger_rect (self);
  if (button == GDK_BUTTON_PRIMARY && y >= r.origin.y && y < r.origin.y + r.size.height) {
    gtk_widget_grab_focus (w);
    omarchy_multi_select_toggle (self);
  }
}

static gboolean
ms_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  OmarchyMultiSelect *self = OMARCHY_MULTI_SELECT (w);
  if (!gtk_widget_is_focus (w))
    return FALSE;
  if (omarchy_key_is_activate (keyval) || keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down || keyval == GDK_KEY_j) {
    omarchy_multi_select_toggle (self);
    return TRUE;
  }
  if (keyval == GDK_KEY_Escape && omarchy_multi_select_get_popup_open (self)) {
    omarchy_multi_select_close (self);
    return TRUE;
  }
  return FALSE;
}

static void
ms_popover_closed (GtkPopover *popover, OmarchyMultiSelect *self)
{
  if (self->popup) {
    GtkWidget *search = OMARCHY_MULTI_SELECT_POPUP (self->popup)->search;
    g_signal_handlers_block_by_func (search, pop_search_changed, self->popup);
    gtk_editable_set_text (GTK_EDITABLE (search), "");
    g_signal_handlers_unblock_by_func (search, pop_search_changed, self->popup);
    refilter (self);
  }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_POPUP_OPEN]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
omarchy_multi_select_init (OmarchyMultiSelect *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->label = g_strdup ("");
  self->trigger_label = g_strdup ("");
  self->options_error = g_strdup ("");
  self->show_label = TRUE;
  self->options = g_array_new (FALSE, TRUE, sizeof (Option));
  self->values = g_ptr_array_new_with_free_func (g_free);
  self->filtered = g_array_new (FALSE, FALSE, sizeof (int));
  self->current = -1;
  gtk_widget_set_focusable (w, TRUE);
  gtk_widget_set_focus_on_click (w, TRUE);
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, ms_clicked);
  omarchy_widget_add_keys (w, ms_key);
  omarchy_forms_add_hover_signal (w, signals[HOVERED]);
}

static void
ms_dispose (GObject *o)
{
  OmarchyMultiSelect *self = OMARCHY_MULTI_SELECT (o);
  if (self->popover) {
    gtk_widget_unparent (GTK_WIDGET (self->popover));
    self->popover = NULL;
    self->popup = NULL;
  }
  g_clear_pointer (&self->popup, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_multi_select_parent_class)->dispose (o);
}

static void
option_clear (gpointer data)
{
  Option *o = data;
  g_free (o->value);
  g_free (o->label);
  g_free (o->description);
}

static void
ms_finalize (GObject *o)
{
  OmarchyMultiSelect *self = OMARCHY_MULTI_SELECT (o);
  g_free (self->label);
  g_free (self->placeholder);
  g_free (self->empty_text);
  g_free (self->no_selection_text);
  g_free (self->trigger_label);
  g_free (self->options_error);
  g_array_unref (self->options);
  g_ptr_array_unref (self->values);
  g_array_unref (self->filtered);
  G_OBJECT_CLASS (omarchy_multi_select_parent_class)->finalize (o);
}

static void
ms_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyMultiSelect *self = OMARCHY_MULTI_SELECT (o);
  switch (id) {
  case PROP_LABEL: omarchy_multi_select_set_label (self, g_value_get_string (v)); break;
  case PROP_VALUES: omarchy_multi_select_set_values (self, g_value_get_boxed (v)); break;
  case PROP_PLACEHOLDER: omarchy_multi_select_set_placeholder_text (self, g_value_get_string (v)); break;
  case PROP_EMPTY_TEXT: omarchy_multi_select_set_empty_text (self, g_value_get_string (v)); break;
  case PROP_NO_SELECTION_TEXT: omarchy_multi_select_set_no_selection_text (self, g_value_get_string (v)); break;
  case PROP_TRIGGER_LABEL: omarchy_multi_select_set_trigger_label (self, g_value_get_string (v)); break;
  case PROP_SHOW_LABEL: omarchy_multi_select_set_show_label (self, g_value_get_boolean (v)); break;
  case PROP_CURSOR: omarchy_multi_select_set_cursor (self, g_value_get_boolean (v)); break;
  case PROP_REFRESHABLE: omarchy_multi_select_set_refreshable (self, g_value_get_boolean (v)); break;
  case PROP_LOADING: omarchy_multi_select_set_loading_options (self, g_value_get_boolean (v)); break;
  case PROP_OPTIONS_ERROR: omarchy_multi_select_set_options_error (self, g_value_get_string (v)); break;
  case PROP_INLINE_POPUP: omarchy_multi_select_set_inline_popup (self, g_value_get_boolean (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
ms_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyMultiSelect *self = OMARCHY_MULTI_SELECT (o);
  switch (id) {
  case PROP_LABEL: g_value_set_string (v, self->label); break;
  case PROP_VALUES: g_value_take_boxed (v, omarchy_multi_select_get_values (self)); break;
  case PROP_PLACEHOLDER: g_value_set_string (v, omarchy_multi_select_get_placeholder_text (self)); break;
  case PROP_EMPTY_TEXT: g_value_set_string (v, omarchy_multi_select_get_empty_text (self)); break;
  case PROP_NO_SELECTION_TEXT: g_value_set_string (v, omarchy_multi_select_get_no_selection_text (self)); break;
  case PROP_TRIGGER_LABEL: g_value_set_string (v, self->trigger_label); break;
  case PROP_SHOW_LABEL: g_value_set_boolean (v, self->show_label); break;
  case PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case PROP_REFRESHABLE: g_value_set_boolean (v, self->refreshable); break;
  case PROP_LOADING: g_value_set_boolean (v, self->loading); break;
  case PROP_OPTIONS_ERROR: g_value_set_string (v, self->options_error); break;
  case PROP_INLINE_POPUP: g_value_set_boolean (v, self->inline_popup); break;
  case PROP_POPUP_OPEN: g_value_set_boolean (v, omarchy_multi_select_get_popup_open (self)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
omarchy_multi_select_class_init (OmarchyMultiSelectClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = ms_dispose;
  oc->finalize = ms_finalize;
  oc->set_property = ms_set_property;
  oc->get_property = ms_get_property;
  wc->measure = ms_measure;
  wc->size_allocate = ms_allocate;
  wc->snapshot = ms_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  props[PROP_LABEL] = g_param_spec_string ("label", NULL, NULL, "", f);
  /**
   * OmarchyMultiSelect:values:
   *
   * The chosen options' values, in the order they were chosen.
   */
  props[PROP_VALUES] = g_param_spec_boxed ("values", NULL, NULL, G_TYPE_STRV, f);
  /**
   * OmarchyMultiSelect:placeholder-text:
   *
   * The search field's placeholder; defaults to the catalog's `common.search`.
   */
  props[PROP_PLACEHOLDER] = g_param_spec_string ("placeholder-text", NULL, NULL, NULL, f);
  /**
   * OmarchyMultiSelect:empty-text:
   *
   * Shown when no option matches; defaults to the catalog's `common.noOptions`.
   */
  props[PROP_EMPTY_TEXT] = g_param_spec_string ("empty-text", NULL, NULL, NULL, f);
  /**
   * OmarchyMultiSelect:no-selection-text:
   *
   * The trigger with nothing chosen; defaults to the catalog's `common.noneSelected`.
   */
  props[PROP_NO_SELECTION_TEXT] = g_param_spec_string ("no-selection-text", NULL, NULL, NULL, f);
  /**
   * OmarchyMultiSelect:trigger-label:
   *
   * The trigger with nothing chosen, before #OmarchyMultiSelect:no-selection-text.
   */
  props[PROP_TRIGGER_LABEL] = g_param_spec_string ("trigger-label", NULL, NULL, "", f);
  props[PROP_SHOW_LABEL] = g_param_spec_boolean ("show-label", NULL, NULL, TRUE, f);
  /**
   * OmarchyMultiSelect:cursor:
   *
   * Force the hover-cursor look on the trigger, as a keyboard-driven panel's cursor does.
   */
  props[PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyMultiSelect:refreshable:
   *
   * Show a refresh button beside the search; it emits ::refresh-requested.
   */
  props[PROP_REFRESHABLE] = g_param_spec_boolean ("refreshable", NULL, NULL, FALSE, f);
  /**
   * OmarchyMultiSelect:loading-options:
   *
   * The app is loading the options: the list reads "Loading..." and refresh is off.
   */
  props[PROP_LOADING] = g_param_spec_boolean ("loading-options", NULL, NULL, FALSE, f);
  /**
   * OmarchyMultiSelect:options-error:
   *
   * Why the options couldn't load, shown in place of the empty list.
   */
  props[PROP_OPTIONS_ERROR] = g_param_spec_string ("options-error", NULL, NULL, "", f);
  /**
   * OmarchyMultiSelect:inline-popup:
   *
   * Open the popup inside this widget, under the trigger (it grows to hold it), instead of in
   * a popover: for a host that gives it the room, or a still.
   */
  props[PROP_INLINE_POPUP] = g_param_spec_boolean ("inline-popup", NULL, NULL, FALSE, f);
  props[PROP_POPUP_OPEN] = g_param_spec_boolean ("popup-open", NULL, NULL, FALSE,
                                                 G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, N_PROPS, props);
  /**
   * OmarchyMultiSelect::changed:
   * @self: the multiselect
   * @values: (array zero-terminated=1): the chosen values now
   *
   * A click or a key toggled an option.
   */
  signals[CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 1, G_TYPE_STRV);
  /**
   * OmarchyMultiSelect::hovered:
   * @self: the multiselect
   * @hovered: the pointer is over it
   */
  signals[HOVERED] = g_signal_new ("hovered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  /**
   * OmarchyMultiSelect::refresh-requested:
   *
   * The refresh button: reload the options.
   */
  signals[REFRESH_REQUESTED] = g_signal_new ("refresh-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                             NULL, NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-multi-select");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_COMBO_BOX);
}

/* ---- API --------------------------------------------------------------------------------- */

/**
 * omarchy_multi_select_new:
 * @label: (nullable): the label above the trigger
 *
 * Returns: (transfer floating): an empty multi-select; add options with
 *   [method@Omarchy.MultiSelect.append]
 */
GtkWidget *
omarchy_multi_select_new (const char *label)
{
  return g_object_new (OMARCHY_TYPE_MULTI_SELECT, "label", label ? label : "", NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyMultiSelect, multi_select, label, props, PROP_LABEL, TRUE)
OMARCHY_STRING_ACCESSORS (OmarchyMultiSelect, multi_select, trigger_label, props, PROP_TRIGGER_LABEL, FALSE)
OMARCHY_BOOL_ACCESSORS (OmarchyMultiSelect, multi_select, show_label, props, PROP_SHOW_LABEL, TRUE)
OMARCHY_BOOL_ACCESSORS (OmarchyMultiSelect, multi_select, cursor, props, PROP_CURSOR, FALSE)

/**
 * omarchy_multi_select_append:
 * @self: the multi-select
 * @value: the option's value
 * @label: (nullable): what it reads (@value when %NULL)
 * @description: (nullable): a dim line under it
 *
 * Add an option (a repeated or empty value is skipped, as Qt's normalizing does).
 */
void
omarchy_multi_select_append (OmarchyMultiSelect *self, const char *value, const char *label, const char *description)
{
  if (!value || !*value)
    return;
  for (guint i = 0; i < self->options->len; i++)
    if (!g_strcmp0 (g_array_index (self->options, Option, i).value, value))
      return;
  Option o = { g_strdup (value), g_strdup (label ? label : value), g_strdup (description ? description : "") };
  g_array_append_val (self->options, o);
  refilter (self);
}

void
omarchy_multi_select_remove_all (OmarchyMultiSelect *self)
{
  for (guint i = 0; i < self->options->len; i++)
    option_clear (&g_array_index (self->options, Option, i));
  g_array_set_size (self->options, 0);
  self->current = -1;
  refilter (self);
}

/**
 * omarchy_multi_select_get_values:
 * @self: the multi-select
 *
 * Returns: (transfer full) (array zero-terminated=1): the chosen values, in the order chosen
 */
char **
omarchy_multi_select_get_values (OmarchyMultiSelect *self)
{
  char **out = g_new0 (char *, self->values->len + 1);
  for (guint i = 0; i < self->values->len; i++)
    out[i] = g_strdup (g_ptr_array_index (self->values, i));
  return out;
}

/**
 * omarchy_multi_select_set_values:
 * @self: the multi-select
 * @values: (nullable) (array zero-terminated=1): the chosen values
 */
void
omarchy_multi_select_set_values (OmarchyMultiSelect *self, const char *const *values)
{
  g_ptr_array_set_size (self->values, 0);
  for (const char *const *v = values; v && *v; v++)
    if (value_index (self, *v) < 0)
      g_ptr_array_add (self->values, g_strdup (*v));
  redraw_all (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_VALUES]);
}

gboolean
omarchy_multi_select_is_selected (OmarchyMultiSelect *self, const char *value)
{
  return value_index (self, value) >= 0;
}

#define CATALOG_TEXT(field, PROP, key, english)                                          \
  const char *omarchy_multi_select_get_##field (OmarchyMultiSelect *self)                \
  {                                                                                      \
    return self->field ? self->field : omarchy_t (key, english);                         \
  }                                                                                      \
  void omarchy_multi_select_set_##field (OmarchyMultiSelect *self, const char *text)     \
  {                                                                                      \
    g_free (self->field);                                                                \
    self->field = g_strdup (text);                                                       \
    redraw_all (self);                                                                   \
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP]);                             \
  }

CATALOG_TEXT (empty_text, PROP_EMPTY_TEXT, "common.noOptions", "No options")
CATALOG_TEXT (no_selection_text, PROP_NO_SELECTION_TEXT, "common.noneSelected", "None selected")

const char *
omarchy_multi_select_get_placeholder_text (OmarchyMultiSelect *self)
{
  return self->placeholder ? self->placeholder : omarchy_t ("common.search", "Search...");
}

/**
 * omarchy_multi_select_set_placeholder_text:
 * @self: the multi-select
 * @text: (nullable): the search field's placeholder, or %NULL for the translated "Search..."
 */
void
omarchy_multi_select_set_placeholder_text (OmarchyMultiSelect *self, const char *text)
{
  g_free (self->placeholder);
  self->placeholder = g_strdup (text);
  if (self->popup)
    omarchy_text_field_set_placeholder_text (OMARCHY_TEXT_FIELD (OMARCHY_MULTI_SELECT_POPUP (self->popup)->search),
                                             omarchy_multi_select_get_placeholder_text (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PLACEHOLDER]);
}

gboolean
omarchy_multi_select_get_refreshable (OmarchyMultiSelect *self)
{
  return self->refreshable;
}

void
omarchy_multi_select_set_refreshable (OmarchyMultiSelect *self, gboolean refreshable)
{
  if (self->refreshable == !!refreshable)
    return;
  self->refreshable = !!refreshable;
  if (self->popup)
    gtk_widget_queue_allocate (self->popup);
  redraw_all (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_REFRESHABLE]);
}

gboolean
omarchy_multi_select_get_loading_options (OmarchyMultiSelect *self)
{
  return self->loading;
}

void
omarchy_multi_select_set_loading_options (OmarchyMultiSelect *self, gboolean loading)
{
  if (self->loading == !!loading)
    return;
  self->loading = !!loading;
  redraw_all (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LOADING]);
}

const char *
omarchy_multi_select_get_options_error (OmarchyMultiSelect *self)
{
  return self->options_error;
}

void
omarchy_multi_select_set_options_error (OmarchyMultiSelect *self, const char *error)
{
  g_free (self->options_error);
  self->options_error = g_strdup (error ? error : "");
  redraw_all (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_OPTIONS_ERROR]);
}

gboolean
omarchy_multi_select_get_inline_popup (OmarchyMultiSelect *self)
{
  return self->inline_popup;
}

void
omarchy_multi_select_set_inline_popup (OmarchyMultiSelect *self, gboolean inline_popup)
{
  if (self->inline_popup == !!inline_popup)
    return;
  omarchy_multi_select_close (self);
  if (self->popover) {
    gtk_widget_unparent (GTK_WIDGET (self->popover));
    self->popover = NULL;
    self->popup = NULL;
  }
  g_clear_pointer (&self->popup, gtk_widget_unparent);
  self->inline_popup = !!inline_popup;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INLINE_POPUP]);
}

gboolean
omarchy_multi_select_get_popup_open (OmarchyMultiSelect *self)
{
  if (self->inline_popup)
    return self->inline_open;
  return self->popover && gtk_widget_get_visible (GTK_WIDGET (self->popover));
}

/**
 * omarchy_multi_select_get_current_index:
 * @self: the multi-select
 *
 * Returns: the row under the open list's cursor (among those the search shows), or -1
 */
int
omarchy_multi_select_get_current_index (OmarchyMultiSelect *self)
{
  return self->current;
}

/**
 * omarchy_multi_select_get_search_field:
 * @self: the multi-select
 *
 * Returns: (transfer none): the popup's search field (an #OmarchyTextField, a #GtkEditable)
 */
GtkWidget *
omarchy_multi_select_get_search_field (OmarchyMultiSelect *self)
{
  if (!self->popup) {
    self->popup = g_object_new (OMARCHY_TYPE_MULTI_SELECT_POPUP, NULL);
    OmarchyMultiSelectPopup *pop = OMARCHY_MULTI_SELECT_POPUP (self->popup);
    pop->owner = self;
    OMARCHY_MULTI_SELECT_LIST (pop->list)->owner = self;
    omarchy_text_field_set_placeholder_text (OMARCHY_TEXT_FIELD (pop->search), omarchy_multi_select_get_placeholder_text (self));
    if (self->inline_popup) {
      gtk_widget_set_visible (self->popup, FALSE);
      gtk_widget_set_parent (self->popup, GTK_WIDGET (self));
    } else {
      self->popover = omarchy_popup_new (GTK_WIDGET (self), self->popup);
      g_signal_connect (self->popover, "closed", G_CALLBACK (ms_popover_closed), self);
    }
  }
  return OMARCHY_MULTI_SELECT_POPUP (self->popup)->search;
}

void
omarchy_multi_select_open (OmarchyMultiSelect *self)
{
  if (omarchy_multi_select_get_popup_open (self))
    return;
  GtkWidget *search = omarchy_multi_select_get_search_field (self);
  g_signal_handlers_block_by_func (search, pop_search_changed, self->popup);
  gtk_editable_set_text (GTK_EDITABLE (search), "");
  g_signal_handlers_unblock_by_func (search, pop_search_changed, self->popup);
  self->current = -1;
  refilter (self);
  OMARCHY_MULTI_SELECT_LIST (OMARCHY_MULTI_SELECT_POPUP (self->popup)->list)->scroll = 0;
  if (self->inline_popup) {
    self->inline_open = TRUE;
    gtk_widget_set_visible (self->popup, TRUE);
    gtk_widget_queue_resize (GTK_WIDGET (self));
  } else {
    graphene_rect_t r = trigger_rect (self);
    gtk_widget_queue_resize (self->popup);
    omarchy_popup_show_below (self->popover, GTK_WIDGET (self), &r, omarchy_tokens ()->xxs);
  }
  gtk_widget_grab_focus (search);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_POPUP_OPEN]);
}

void
omarchy_multi_select_close (OmarchyMultiSelect *self)
{
  if (!omarchy_multi_select_get_popup_open (self))
    return;
  if (self->inline_popup) {
    self->inline_open = FALSE;
    gtk_widget_set_visible (self->popup, FALSE);
    gtk_widget_queue_resize (GTK_WIDGET (self));
    gtk_widget_grab_focus (GTK_WIDGET (self));
    ms_popover_closed (NULL, self);
  } else {
    gtk_popover_popdown (self->popover);
    gtk_widget_grab_focus (GTK_WIDGET (self));
  }
}

void
omarchy_multi_select_toggle (OmarchyMultiSelect *self)
{
  if (omarchy_multi_select_get_popup_open (self))
    omarchy_multi_select_close (self);
  else
    omarchy_multi_select_open (self);
}
