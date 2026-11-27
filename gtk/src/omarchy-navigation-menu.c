/* omarchy-navigation-menu.c: Menu (Menu.qml), MenuItem (MenuItem.qml), MenuSection
 * (MenuSection.qml), MenuSeparator (MenuSeparator.qml) and ContextMenu (ContextMenu.qml), drawn
 * as the Qt kit draws them. */
#include "omarchy-navigation-private.h"

#include <math.h>

/* ================================================================================== Menu === */

/**
 * OmarchyMenu:
 *
 * A menu: #OmarchyMenuItem rows, #OmarchyMenuSeparator rules and #OmarchyMenuSection labels on
 * the menu surface (`surfaces.menu` background and border, Hyprland's rounding), `sm` inside its
 * border, at least #OmarchyMenu:minimum-width wide.
 *
 * Give it a #OmarchyMenu:trigger and omarchy_menu_open() opens it `gap` below the trigger, in a
 * popover the trigger owns; omarchy_menu_popup() opens it at a point. #OmarchyMenu:held draws it
 * open in place (put it in a layout) with no positioning or dismissal, for galleries and stills.
 *
 * Keyboard (while open it holds the focus): Up/Down or k/j move the cursor over enabled rows and
 * wrap, Home/End jump, Return, Enter or Space runs the row, Right or l opens its submenu, Left or
 * h goes back out of a submenu, Escape closes. A press outside closes it. Running a row closes
 * the whole chain (a held menu stays, with the keys back).
 */
typedef struct {
  GtkWidget *box;
  gboolean opened, held;
  GtkWidget *trigger; /* weak */
  int gap;            /* -1: xs */
  int minimum_width;  /* -1: space(160) */
  int current_index;
  OmarchyMenu *parent_menu; /* while open as another's submenu */
  GtkPopover *popover;      /* weak: its parent (the trigger, a target, a row) owns it */
} OmarchyMenuPrivate;

enum { MN_PROP_0, MN_PROP_OPENED, MN_PROP_HELD, MN_PROP_TRIGGER, MN_PROP_GAP, MN_PROP_MINIMUM_WIDTH,
       MN_PROP_CURRENT_INDEX, MN_PROP_HAS_ICONS, MN_PROP_HAS_MARKS, MN_N_PROPS };
static GParamSpec *mn_props[MN_N_PROPS];
enum { MN_TRIGGERED, MN_CLOSED, MN_N_SIGNALS };
static guint mn_signals[MN_N_SIGNALS];

static void menu_buildable_init (GtkBuildableIface *iface);
G_DEFINE_TYPE_WITH_CODE (OmarchyMenu, omarchy_menu, GTK_TYPE_WIDGET,
                         G_ADD_PRIVATE (OmarchyMenu)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, menu_buildable_init))

#define MENU_PRIV(m) ((OmarchyMenuPrivate *) omarchy_menu_get_instance_private (OMARCHY_MENU (m)))

/* ============================================================================== MenuItem === */

/**
 * OmarchyMenuItem:
 *
 * One row of an #OmarchyMenu: an optional leading glyph, the label, a dim shortcut hint at the
 * right, and a trailing mark: an accent check (a radio dot when #OmarchyMenuItem:exclusive) when
 * #OmarchyMenuItem:checked, or a chevron when it opens a submenu. Under the pointer or the
 * menu's keyboard cursor it takes the menu surface's selected background and text. Insensitive
 * rows are faint and inert; destructive rows are urgent.
 *
 * Running it (a click, or Return/Space on the menu's cursor) opens its submenu, or flips a
 * checkable row (an exclusive one checks itself and unchecks the exclusive rows next to it),
 * emits ::triggered, then the menu's ::triggered, and closes the menu.
 */
struct _OmarchyMenuItem {
  GtkWidget parent_instance;
  char *text, *icon, *shortcut;
  gboolean checkable, checked, exclusive, destructive, cursor, has_submenu;
  OmarchyMenu *submenu; /* owned */
};

enum { MI_PROP_0, MI_PROP_TEXT, MI_PROP_ICON, MI_PROP_SHORTCUT, MI_PROP_CHECKABLE, MI_PROP_CHECKED, MI_PROP_EXCLUSIVE,
       MI_PROP_DESTRUCTIVE, MI_PROP_CURSOR, MI_PROP_HAS_SUBMENU, MI_PROP_SUBMENU, MI_PROP_HIGHLIGHTED, MI_PROP_HOT,
       MI_N_PROPS };
static GParamSpec *mi_props[MI_N_PROPS];
enum { MI_TRIGGERED, MI_N_SIGNALS };
static guint mi_signals[MI_N_SIGNALS];

static void item_buildable_init (GtkBuildableIface *iface);
G_DEFINE_FINAL_TYPE_WITH_CODE (OmarchyMenuItem, omarchy_menu_item, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, item_buildable_init))

/* ---- the menu's rows --------------------------------------------------------------------- */

static void menu_open_submenu (OmarchyMenu *self, OmarchyMenuItem *item);
static void menu_item_triggered (OmarchyMenu *self, OmarchyMenuItem *item);

static gboolean
is_row (GtkWidget *c)
{
  return OMARCHY_IS_MENU_ITEM (c) && gtk_widget_get_visible (c);
}

static GPtrArray *
menu_rows (OmarchyMenu *self)
{
  GPtrArray *rows = g_ptr_array_new ();
  for (GtkWidget *c = gtk_widget_get_first_child (MENU_PRIV (self)->box); c; c = gtk_widget_get_next_sibling (c))
    if (is_row (c))
      g_ptr_array_add (rows, c);
  return rows;
}

static int
menu_row_index (OmarchyMenu *self, OmarchyMenuItem *item)
{
  int i = 0;
  for (GtkWidget *c = gtk_widget_get_first_child (MENU_PRIV (self)->box); c; c = gtk_widget_get_next_sibling (c)) {
    if (!is_row (c))
      continue;
    if (c == GTK_WIDGET (item))
      return i;
    i++;
  }
  return -1;
}

static void
menu_redraw_rows (OmarchyMenu *self, gboolean resize)
{
  for (GtkWidget *c = gtk_widget_get_first_child (MENU_PRIV (self)->box); c; c = gtk_widget_get_next_sibling (c)) {
    if (resize)
      gtk_widget_queue_resize (c);
    else
      gtk_widget_queue_draw (c);
  }
}

/* The rows' reserve flags or sizes changed: every row relays out. */
static void
menu_reflow (OmarchyMenu *self)
{
  menu_redraw_rows (self, TRUE);
  g_object_notify_by_pspec (G_OBJECT (self), mn_props[MN_PROP_HAS_ICONS]);
  g_object_notify_by_pspec (G_OBJECT (self), mn_props[MN_PROP_HAS_MARKS]);
}

static void
menu_insets (float in[4])
{
  const OmarchyTokens *t = omarchy_tokens ();
  for (int i = 0; i < 4; i++)
    in[i] = t->menu.border.widths[i] + t->sm;
}

static int
menu_min_width (OmarchyMenu *self)
{
  int w = MENU_PRIV (self)->minimum_width;
  return w >= 0 ? w : omarchy_space (160);
}

/* ---- Menu widget ------------------------------------------------------------------------- */

static void
menu_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (w);
  float in[4];
  menu_insets (in);
  int bmin, bnat;
  gtk_widget_measure (priv->box, o, -1, &bmin, &bnat, NULL, NULL);
  if (o == GTK_ORIENTATION_HORIZONTAL) {
    *min = *nat = MAX (menu_min_width (OMARCHY_MENU (w)), bnat) + (int) ceilf (in[1] + in[3]);
  } else {
    *min = *nat = bnat + (int) ceilf (in[0] + in[2]);
  }
}

static void
menu_allocate (GtkWidget *w, int width, int height, int baseline)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (w);
  float in[4];
  menu_insets (in);
  GtkAllocation a = { (int) in[3], (int) in[0], MAX (0, width - (int) (in[1] + in[3])), MAX (0, height - (int) (in[0] + in[2])) };
  gtk_widget_size_allocate (priv->box, &a, -1);
}

static void
menu_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  const OmarchyTokens *p = omarchy_paint ();
  omarchy_draw_surface (s, &GRAPHENE_RECT_INIT (0, 0, gtk_widget_get_width (w), gtk_widget_get_height (w)), p->radius,
                        &p->menu.background, &p->menu.border);
  gtk_widget_snapshot_child (w, MENU_PRIV (w)->box, s);
}

static void
menu_move (OmarchyMenu *self, int step)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  g_autoptr (GPtrArray) rows = menu_rows (self);
  int n = (int) rows->len;
  if (n == 0)
    return;
  int i = priv->current_index < 0 ? (step > 0 ? -1 : n) : priv->current_index;
  for (int tries = 0; tries < n; tries++) {
    i = ((i + step) % n + n) % n;
    if (gtk_widget_get_sensitive (g_ptr_array_index (rows, i))) {
      omarchy_menu_set_current_index (self, i);
      return;
    }
  }
}

static void
menu_jump (OmarchyMenu *self, gboolean to_end)
{
  MENU_PRIV (self)->current_index = -1;
  menu_move (self, to_end ? -1 : 1);
}

static void
menu_close_submenus (OmarchyMenu *self)
{
  g_autoptr (GPtrArray) rows = menu_rows (self);
  for (guint i = 0; i < rows->len; i++) {
    OmarchyMenuItem *item = g_ptr_array_index (rows, i);
    if (item->submenu && MENU_PRIV (item->submenu)->opened)
      omarchy_menu_close (item->submenu);
  }
}

static OmarchyMenu *
menu_root (OmarchyMenu *self)
{
  OmarchyMenu *m = self;
  while (MENU_PRIV (m)->parent_menu)
    m = MENU_PRIV (m)->parent_menu;
  return m;
}

static void
menu_close_all (OmarchyMenu *self)
{
  OmarchyMenu *m = menu_root (self);
  if (!MENU_PRIV (m)->held) {
    omarchy_menu_close (m);
  } else {
    /* A held menu stays: the keys come back to it from the submenu that just closed. */
    menu_close_submenus (m);
    gtk_widget_grab_focus (GTK_WIDGET (m));
  }
}

static void
menu_item_triggered (OmarchyMenu *self, OmarchyMenuItem *item)
{
  OmarchyMenu *root = menu_root (self);
  g_object_ref (item);
  g_signal_emit (root, mn_signals[MN_TRIGGERED], 0, item);
  menu_close_all (self);
  g_object_unref (item);
}

static void
menu_popover_closed (GtkPopover *pop, OmarchyMenu *self)
{
  /* Dismissed from outside (a press outside the popup): the menu closes too. */
  if (MENU_PRIV (self)->opened)
    omarchy_menu_close (self);
}

static void menu_anchor_destroyed (GtkWidget *anchor, OmarchyMenu *self);

/* Takes the menu out of its popover and drops the popover. The caller gets a reference to the
 * menu, to hand on or release. */
static void
menu_drop_popover (OmarchyMenu *self)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  g_object_ref (self);
  if (!priv->popover)
    return;
  GtkPopover *old = priv->popover;
  GtkWidget *anchor = gtk_widget_get_parent (GTK_WIDGET (old));
  g_object_remove_weak_pointer (G_OBJECT (old), (gpointer *) &priv->popover);
  priv->popover = NULL;
  g_signal_handlers_disconnect_by_data (old, self);
  if (anchor)
    g_signal_handlers_disconnect_by_func (anchor, menu_anchor_destroyed, self);
  gtk_popover_set_child (old, NULL);
  gtk_widget_unparent (GTK_WIDGET (old));
}

/* The widget the popover hangs off is going: take the popover off it (GTK wants children gone
 * before a widget finalizes), which releases the menu. */
static void
menu_anchor_destroyed (GtkWidget *anchor, OmarchyMenu *self)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  if (priv->popover && gtk_widget_get_parent (GTK_WIDGET (priv->popover)) == anchor) {
    menu_drop_popover (self);
    g_object_unref (self);
  }
}

/* The popover the menu floats in, a child of `anchor` (made on first use, moved when the anchor
 * changes). NULL when the menu sits in a layout of its own. */
static GtkPopover *
menu_popover_on (OmarchyMenu *self, GtkWidget *anchor)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  GtkWidget *me = GTK_WIDGET (self);
  if (!anchor)
    return priv->popover;
  if (priv->popover && gtk_widget_get_parent (GTK_WIDGET (priv->popover)) == anchor)
    return priv->popover;
  if (priv->popover)
    menu_drop_popover (self);
  else if (gtk_widget_get_parent (me))
    return NULL;
  else
    g_object_ref (me);
  priv->popover = omarchy_popup_new (anchor, me);
  g_object_unref (me);
  g_object_add_weak_pointer (G_OBJECT (priv->popover), (gpointer *) &priv->popover);
  g_signal_connect (priv->popover, "closed", G_CALLBACK (menu_popover_closed), self);
  g_signal_connect_object (anchor, "destroy", G_CALLBACK (menu_anchor_destroyed), self, 0);
  return priv->popover;
}

static void
menu_show (OmarchyMenu *self)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  omarchy_menu_set_current_index (self, -1);
  if (!priv->opened) {
    priv->opened = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), mn_props[MN_PROP_OPENED]);
  }
}

static void
menu_open_submenu (OmarchyMenu *self, OmarchyMenuItem *item)
{
  OmarchyMenu *sub = item ? item->submenu : NULL;
  if (!sub)
    return;
  menu_close_submenus (self);
  omarchy_menu_set_current_index (self, menu_row_index (self, item));
  MENU_PRIV (sub)->parent_menu = self;
  menu_show (sub);
  GtkPopover *pop = menu_popover_on (sub, GTK_WIDGET (item));
  if (pop) {
    float in[4], sub_in[4];
    menu_insets (in);
    menu_insets (sub_in);
    gboolean rtl = gtk_widget_get_direction (GTK_WIDGET (item)) == GTK_TEXT_DIR_RTL;
    int w = MAX (1, gtk_widget_get_width (GTK_WIDGET (item)));
    gtk_popover_set_position (pop, rtl ? GTK_POS_LEFT : GTK_POS_RIGHT);
    gtk_widget_set_valign (GTK_WIDGET (pop), GTK_ALIGN_START);
    gtk_popover_set_pointing_to (pop, &(GdkRectangle) { 0, 0, w, 1 });
    gtk_popover_set_offset (pop, (int) (rtl ? -in[3] : in[1]), (int) -sub_in[0]);
    gtk_popover_popup (pop);
  }
  menu_move (sub, 1);
  gtk_widget_grab_focus (GTK_WIDGET (sub));
}

/**
 * omarchy_menu_handle_key:
 * @self: the menu
 * @keyval: the key
 * @state: its modifiers
 *
 * Handles a key for the menu, for a panel's own key catcher (the menu's own key controller calls
 * it while the menu has the focus).
 *
 * Returns: %TRUE when the key was the menu's
 */
gboolean
omarchy_menu_handle_key (OmarchyMenu *self, guint keyval, GdkModifierType state)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK))
    return FALSE;
  if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down || keyval == GDK_KEY_j) {
    menu_move (self, 1);
  } else if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up || keyval == GDK_KEY_k) {
    menu_move (self, -1);
  } else if (keyval == GDK_KEY_Home || keyval == GDK_KEY_KP_Home) {
    menu_jump (self, FALSE);
  } else if (keyval == GDK_KEY_End || keyval == GDK_KEY_KP_End) {
    menu_jump (self, TRUE);
  } else if (omarchy_key_is_activate (keyval)) {
    OmarchyMenuItem *item = omarchy_menu_get_current_item (self);
    if (item)
      omarchy_menu_item_activate (item);
  } else if (keyval == GDK_KEY_Escape) {
    if (priv->parent_menu) {
      OmarchyMenu *back = priv->parent_menu;
      omarchy_menu_close (self);
      gtk_widget_grab_focus (GTK_WIDGET (back));
    } else {
      omarchy_menu_close (self);
    }
  } else {
    guint k = omarchy_reading_key (GTK_WIDGET (self), keyval, TRUE);
    if (k == GDK_KEY_Right || k == GDK_KEY_KP_Right) {
      OmarchyMenuItem *item = omarchy_menu_get_current_item (self);
      if (item && omarchy_menu_item_get_has_submenu (item))
        menu_open_submenu (self, item);
    } else if (k == GDK_KEY_Left || k == GDK_KEY_KP_Left) {
      if (priv->parent_menu) {
        OmarchyMenu *back = priv->parent_menu;
        omarchy_menu_close (self);
        gtk_widget_grab_focus (GTK_WIDGET (back));
      }
    } else {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean
menu_key (GtkWidget *w, guint keyval, GdkModifierType state)
{
  return omarchy_menu_handle_key (OMARCHY_MENU (w), keyval, state);
}

static void
menu_child_visible (GtkWidget *child, GParamSpec *pspec, OmarchyMenu *self)
{
  menu_reflow (self);
}

static void
omarchy_menu_init (OmarchyMenu *self)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  GtkWidget *w = GTK_WIDGET (self);
  priv->gap = -1;
  priv->minimum_width = -1;
  priv->current_index = -1;
  priv->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_parent (priv->box, w);
  gtk_widget_set_focusable (w, TRUE);
  omarchy_widget_follow (w);
  omarchy_widget_add_keys (w, menu_key);
}

static void
menu_dispose (GObject *o)
{
  OmarchyMenu *self = OMARCHY_MENU (o);
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  if (priv->trigger) {
    g_object_remove_weak_pointer (G_OBJECT (priv->trigger), (gpointer *) &priv->trigger);
    priv->trigger = NULL;
  }
  if (priv->popover) {
    g_object_remove_weak_pointer (G_OBJECT (priv->popover), (gpointer *) &priv->popover);
    g_signal_handlers_disconnect_by_data (priv->popover, self);
    priv->popover = NULL;
  }
  g_clear_pointer (&priv->box, gtk_widget_unparent);
  G_OBJECT_CLASS (omarchy_menu_parent_class)->dispose (o);
}

static void
menu_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyMenu *self = OMARCHY_MENU (o);
  switch (id) {
  case MN_PROP_HELD: omarchy_menu_set_held (self, g_value_get_boolean (v)); break;
  case MN_PROP_TRIGGER: omarchy_menu_set_trigger (self, g_value_get_object (v)); break;
  case MN_PROP_GAP: omarchy_menu_set_gap (self, g_value_get_int (v)); break;
  case MN_PROP_MINIMUM_WIDTH: omarchy_menu_set_minimum_width (self, g_value_get_int (v)); break;
  case MN_PROP_CURRENT_INDEX: omarchy_menu_set_current_index (self, g_value_get_int (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
menu_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyMenu *self = OMARCHY_MENU (o);
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  switch (id) {
  case MN_PROP_OPENED: g_value_set_boolean (v, priv->opened); break;
  case MN_PROP_HELD: g_value_set_boolean (v, priv->held); break;
  case MN_PROP_TRIGGER: g_value_set_object (v, priv->trigger); break;
  case MN_PROP_GAP: g_value_set_int (v, priv->gap); break;
  case MN_PROP_MINIMUM_WIDTH: g_value_set_int (v, priv->minimum_width); break;
  case MN_PROP_CURRENT_INDEX: g_value_set_int (v, priv->current_index); break;
  case MN_PROP_HAS_ICONS: g_value_set_boolean (v, omarchy_menu_get_has_icons (self)); break;
  case MN_PROP_HAS_MARKS: g_value_set_boolean (v, omarchy_menu_get_has_marks (self)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static GtkBuildableIface *menu_parent_buildable;

static void
menu_add_child (GtkBuildable *b, GtkBuilder *builder, GObject *child, const char *type)
{
  if (GTK_IS_WIDGET (child))
    omarchy_menu_append (OMARCHY_MENU (b), GTK_WIDGET (child));
  else
    menu_parent_buildable->add_child (b, builder, child, type);
}

static void
menu_buildable_init (GtkBuildableIface *iface)
{
  menu_parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = menu_add_child;
}

static void
omarchy_menu_class_init (OmarchyMenuClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = menu_dispose;
  oc->set_property = menu_set_property;
  oc->get_property = menu_get_property;
  wc->measure = menu_measure;
  wc->size_allocate = menu_allocate;
  wc->snapshot = menu_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  const GParamFlags ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  /**
   * OmarchyMenu:opened:
   *
   * Open: by omarchy_menu_open(), omarchy_menu_popup() or as a submenu, until it closes.
   */
  mn_props[MN_PROP_OPENED] = g_param_spec_boolean ("opened", NULL, NULL, FALSE, ro);
  /**
   * OmarchyMenu:held:
   *
   * Drawn open in place (in a layout), with no positioning and no dismissal.
   */
  mn_props[MN_PROP_HELD] = g_param_spec_boolean ("held", NULL, NULL, FALSE, f);
  /**
   * OmarchyMenu:trigger:
   *
   * The widget omarchy_menu_open() opens the menu below. It owns the menu from then on.
   */
  mn_props[MN_PROP_TRIGGER] = g_param_spec_object ("trigger", NULL, NULL, GTK_TYPE_WIDGET, f);
  /**
   * OmarchyMenu:gap:
   *
   * The distance from the trigger, or -1 for `xs`.
   */
  mn_props[MN_PROP_GAP] = g_param_spec_int ("gap", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyMenu:minimum-width:
   *
   * The narrowest the rows get, or -1 for `space(160)`.
   */
  mn_props[MN_PROP_MINIMUM_WIDTH] = g_param_spec_int ("minimum-width", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyMenu:current-index:
   *
   * The keyboard cursor: an index into the visible #OmarchyMenuItem rows, or -1.
   */
  mn_props[MN_PROP_CURRENT_INDEX] = g_param_spec_int ("current-index", NULL, NULL, -1, G_MAXINT, -1, f);
  /**
   * OmarchyMenu:has-icons:
   *
   * Some row has a leading glyph, so every row reserves the glyph column.
   */
  mn_props[MN_PROP_HAS_ICONS] = g_param_spec_boolean ("has-icons", NULL, NULL, FALSE, ro);
  /**
   * OmarchyMenu:has-marks:
   *
   * Some row is checkable or opens a submenu, so every row reserves the mark column.
   */
  mn_props[MN_PROP_HAS_MARKS] = g_param_spec_boolean ("has-marks", NULL, NULL, FALSE, ro);
  g_object_class_install_properties (oc, MN_N_PROPS, mn_props);
  /**
   * OmarchyMenu::triggered:
   * @self: the menu
   * @item: the row that ran
   *
   * A row ran, in this menu or one of its submenus. The menu has closed itself unless held.
   */
  mn_signals[MN_TRIGGERED] = g_signal_new ("triggered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                           NULL, G_TYPE_NONE, 1, OMARCHY_TYPE_MENU_ITEM);
  /**
   * OmarchyMenu::closed:
   *
   * The open menu closed: Escape, a press outside, a row ran, or omarchy_menu_close().
   */
  mn_signals[MN_CLOSED] = g_signal_new ("closed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                        G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-menu");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_MENU);
}

/**
 * omarchy_menu_new:
 *
 * Returns: (transfer floating): an empty menu; add rows with omarchy_menu_append()
 */
GtkWidget *
omarchy_menu_new (void)
{
  return g_object_new (OMARCHY_TYPE_MENU, NULL);
}

/**
 * omarchy_menu_append:
 * @self: the menu
 * @row: (transfer floating): an #OmarchyMenuItem, #OmarchyMenuSeparator or #OmarchyMenuSection
 */
void
omarchy_menu_append (OmarchyMenu *self, GtkWidget *row)
{
  gtk_box_append (GTK_BOX (MENU_PRIV (self)->box), row);
  g_signal_connect_object (row, "notify::visible", G_CALLBACK (menu_child_visible), self, 0);
  menu_reflow (self);
}

/**
 * omarchy_menu_remove:
 * @self: the menu
 * @row: a row of the menu
 */
void
omarchy_menu_remove (OmarchyMenu *self, GtkWidget *row)
{
  g_signal_handlers_disconnect_by_data (row, self);
  gtk_box_remove (GTK_BOX (MENU_PRIV (self)->box), row);
  omarchy_menu_set_current_index (self, -1);
  menu_reflow (self);
}

/**
 * omarchy_menu_append_item:
 * @self: the menu
 * @text: the label
 *
 * Returns: (transfer none): the new row, to set up further
 */
OmarchyMenuItem *
omarchy_menu_append_item (OmarchyMenu *self, const char *text)
{
  GtkWidget *item = omarchy_menu_item_new (text);
  omarchy_menu_append (self, item);
  return OMARCHY_MENU_ITEM (item);
}

void
omarchy_menu_append_separator (OmarchyMenu *self)
{
  omarchy_menu_append (self, omarchy_menu_separator_new ());
}

void
omarchy_menu_append_section (OmarchyMenu *self, const char *text)
{
  omarchy_menu_append (self, omarchy_menu_section_new (text));
}

/**
 * omarchy_menu_get_n_rows:
 * @self: the menu
 *
 * Returns: how many visible #OmarchyMenuItem rows the cursor walks
 */
guint
omarchy_menu_get_n_rows (OmarchyMenu *self)
{
  g_autoptr (GPtrArray) rows = menu_rows (self);
  return rows->len;
}

/**
 * omarchy_menu_get_row:
 * @self: the menu
 * @index: an index into the visible rows
 *
 * Returns: (transfer none) (nullable): the row
 */
OmarchyMenuItem *
omarchy_menu_get_row (OmarchyMenu *self, guint index)
{
  g_autoptr (GPtrArray) rows = menu_rows (self);
  return index < rows->len ? g_ptr_array_index (rows, index) : NULL;
}

/**
 * omarchy_menu_get_current_item:
 * @self: the menu
 *
 * Returns: (transfer none) (nullable): the row under the keyboard cursor
 */
OmarchyMenuItem *
omarchy_menu_get_current_item (OmarchyMenu *self)
{
  int i = MENU_PRIV (self)->current_index;
  return i >= 0 ? omarchy_menu_get_row (self, (guint) i) : NULL;
}

int
omarchy_menu_get_current_index (OmarchyMenu *self)
{
  return MENU_PRIV (self)->current_index;
}

void
omarchy_menu_set_current_index (OmarchyMenu *self, int index)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  index = MAX (-1, index);
  if (priv->current_index == index)
    return;
  priv->current_index = index;
  menu_redraw_rows (self, FALSE);
  g_object_notify_by_pspec (G_OBJECT (self), mn_props[MN_PROP_CURRENT_INDEX]);
}

gboolean
omarchy_menu_get_opened (OmarchyMenu *self)
{
  return MENU_PRIV (self)->opened;
}

gboolean
omarchy_menu_get_held (OmarchyMenu *self)
{
  return MENU_PRIV (self)->held;
}

void
omarchy_menu_set_held (OmarchyMenu *self, gboolean held)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  if (priv->held == !!held)
    return;
  priv->held = !!held;
  g_object_notify_by_pspec (G_OBJECT (self), mn_props[MN_PROP_HELD]);
}

/**
 * omarchy_menu_get_trigger:
 * @self: the menu
 *
 * Returns: (transfer none) (nullable): the widget it opens below
 */
GtkWidget *
omarchy_menu_get_trigger (OmarchyMenu *self)
{
  return MENU_PRIV (self)->trigger;
}

/**
 * omarchy_menu_set_trigger:
 * @self: the menu
 * @trigger: (nullable): the widget omarchy_menu_open() opens it below
 *
 * Unless the menu is in a layout (held), the trigger takes it: the menu floats in a popover the
 * trigger owns.
 */
void
omarchy_menu_set_trigger (OmarchyMenu *self, GtkWidget *trigger)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  if (priv->trigger == trigger)
    return;
  if (priv->trigger)
    g_object_remove_weak_pointer (G_OBJECT (priv->trigger), (gpointer *) &priv->trigger);
  priv->trigger = trigger;
  if (trigger) {
    g_object_add_weak_pointer (G_OBJECT (trigger), (gpointer *) &priv->trigger);
    if (!priv->held)
      menu_popover_on (self, trigger);
  }
  g_object_notify_by_pspec (G_OBJECT (self), mn_props[MN_PROP_TRIGGER]);
}

int
omarchy_menu_get_gap (OmarchyMenu *self)
{
  return MENU_PRIV (self)->gap;
}

void
omarchy_menu_set_gap (OmarchyMenu *self, int gap)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  if (priv->gap == gap)
    return;
  priv->gap = gap;
  g_object_notify_by_pspec (G_OBJECT (self), mn_props[MN_PROP_GAP]);
}

int
omarchy_menu_get_minimum_width (OmarchyMenu *self)
{
  return MENU_PRIV (self)->minimum_width;
}

void
omarchy_menu_set_minimum_width (OmarchyMenu *self, int width)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  if (priv->minimum_width == width)
    return;
  priv->minimum_width = width;
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), mn_props[MN_PROP_MINIMUM_WIDTH]);
}

gboolean
omarchy_menu_get_has_icons (OmarchyMenu *self)
{
  g_autoptr (GPtrArray) rows = menu_rows (self);
  for (guint i = 0; i < rows->len; i++) {
    OmarchyMenuItem *r = g_ptr_array_index (rows, i);
    if (r->icon && *r->icon)
      return TRUE;
  }
  return FALSE;
}

gboolean
omarchy_menu_get_has_marks (OmarchyMenu *self)
{
  g_autoptr (GPtrArray) rows = menu_rows (self);
  for (guint i = 0; i < rows->len; i++) {
    OmarchyMenuItem *r = g_ptr_array_index (rows, i);
    if (r->checkable || omarchy_menu_item_get_has_submenu (r))
      return TRUE;
  }
  return FALSE;
}

/**
 * omarchy_menu_get_parent_menu:
 * @self: the menu
 *
 * Returns: (transfer none) (nullable): the menu this one last opened from as a submenu
 */
OmarchyMenu *
omarchy_menu_get_parent_menu (OmarchyMenu *self)
{
  return MENU_PRIV (self)->parent_menu;
}

/**
 * omarchy_menu_open:
 * @self: the menu
 *
 * Opens the menu `gap` below its #OmarchyMenu:trigger, its left edges lined up, with the focus.
 */
void
omarchy_menu_open (OmarchyMenu *self)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  menu_show (self);
  GtkPopover *pop = priv->held ? NULL : menu_popover_on (self, priv->trigger);
  if (pop && priv->trigger) {
    int gap = priv->gap >= 0 ? priv->gap : omarchy_tokens ()->xs;
    gtk_popover_set_position (pop, GTK_POS_BOTTOM);
    gtk_widget_set_halign (GTK_WIDGET (pop), GTK_ALIGN_START);
    gtk_popover_set_pointing_to (pop, &(GdkRectangle) { 0, 0, MAX (1, gtk_widget_get_width (priv->trigger)),
                                                        MAX (1, gtk_widget_get_height (priv->trigger)) });
    gtk_popover_set_offset (pop, 0, gap);
    gtk_popover_popup (pop);
  }
  gtk_widget_grab_focus (GTK_WIDGET (self));
}

/**
 * omarchy_menu_popup:
 * @self: the menu
 * @parent: (nullable): the widget the point is in (and that then owns the menu), or %NULL for
 *   the trigger
 * @x: the menu's left edge, in @parent's coordinates
 * @y: its top edge
 *
 * Opens the menu with its top-left corner at a point, kept on screen, with the focus.
 */
void
omarchy_menu_popup (OmarchyMenu *self, GtkWidget *parent, double x, double y)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  menu_show (self);
  GtkPopover *pop = priv->held ? NULL : menu_popover_on (self, parent ? parent : priv->trigger);
  if (pop) {
    gtk_popover_set_position (pop, GTK_POS_BOTTOM);
    gtk_widget_set_halign (GTK_WIDGET (pop), GTK_ALIGN_START);
    gtk_popover_set_pointing_to (pop, &(GdkRectangle) { (int) x, (int) y, 1, 1 });
    gtk_popover_set_offset (pop, 0, -1);
    gtk_popover_popup (pop);
  }
  gtk_widget_grab_focus (GTK_WIDGET (self));
}

/**
 * omarchy_menu_close:
 * @self: the menu
 *
 * Closes the menu and its open submenus; ::closed when it was open.
 */
void
omarchy_menu_close (OmarchyMenu *self)
{
  OmarchyMenuPrivate *priv = MENU_PRIV (self);
  menu_close_submenus (self);
  if (!priv->opened)
    return;
  priv->opened = FALSE;
  omarchy_menu_set_current_index (self, -1);
  if (priv->popover && gtk_widget_get_visible (GTK_WIDGET (priv->popover)))
    gtk_popover_popdown (priv->popover);
  g_object_notify_by_pspec (G_OBJECT (self), mn_props[MN_PROP_OPENED]);
  g_signal_emit (self, mn_signals[MN_CLOSED], 0);
}

/* ---- MenuItem widget --------------------------------------------------------------------- */

/**
 * omarchy_menu_item_get_menu:
 * @self: the row
 *
 * Returns: (transfer none) (nullable): the menu the row is in
 */
OmarchyMenu *
omarchy_menu_item_get_menu (OmarchyMenuItem *self)
{
  GtkWidget *m = gtk_widget_get_ancestor (GTK_WIDGET (self), OMARCHY_TYPE_MENU);
  return m ? OMARCHY_MENU (m) : NULL;
}

static void
item_menu_reflow (OmarchyMenuItem *self)
{
  OmarchyMenu *m = omarchy_menu_item_get_menu (self);
  if (m)
    menu_reflow (m);
  else
    gtk_widget_queue_resize (GTK_WIDGET (self));
}

static gboolean
item_reserve_icon (OmarchyMenuItem *self)
{
  OmarchyMenu *m = omarchy_menu_item_get_menu (self);
  return m ? omarchy_menu_get_has_icons (m) : (self->icon && *self->icon);
}

static gboolean
item_reserve_mark (OmarchyMenuItem *self)
{
  OmarchyMenu *m = omarchy_menu_item_get_menu (self);
  return m ? omarchy_menu_get_has_marks (m) : (self->checkable || omarchy_menu_item_get_has_submenu (self));
}

static void
item_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyMenuItem *self = OMARCHY_MENU_ITEM (w);
  const OmarchyTokens *t = omarchy_tokens ();
  if (o == GTK_ORIENTATION_VERTICAL) {
    *min = *nat = t->popup_row_height;
    return;
  }
  int width = t->control_padding_x * 2 + omarchy_nav_text_width (w, self->text, t->body, FALSE);
  if (item_reserve_icon (self))
    width += t->icon + t->control_gap;
  if (self->shortcut && *self->shortcut)
    width += t->huge + omarchy_nav_text_width (w, self->shortcut, t->body_small, FALSE);
  if (item_reserve_mark (self))
    width += t->control_gap + t->icon;
  *min = *nat = width;
}

static void
item_allocate (GtkWidget *w, int width, int height, int baseline)
{
  /* A submenu's popover hangs off the row. */
  for (GtkWidget *c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c))
    if (GTK_IS_POPOVER (c))
      gtk_popover_present (GTK_POPOVER (c));
}

static void
item_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyMenuItem *self = OMARCHY_MENU_ITEM (w);
  const OmarchyTokens *p = omarchy_paint ();
  float W = gtk_widget_get_width (w), H = gtk_widget_get_height (w);
  gboolean rtl = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL;
  gboolean enabled = gtk_widget_is_sensitive (w);
  gboolean hot = omarchy_menu_item_get_hot (self);
#define MX(x, width) (rtl ? W - (x) - (width) : (x))

  if (enabled && omarchy_widget_pressed (w))
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (0, 0, W, H), MIN (4, p->radius), &p->pressed);
  else if (hot)
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (0, 0, W, H), MIN (4, p->radius), &p->menu_selected_background);

  GdkRGBA ink = !enabled ? p->text_faint : self->destructive ? p->urgent : hot ? p->menu_selected_text : p->menu.text;
  float pad = p->control_padding_x, box = p->icon, gap = p->control_gap;
  gboolean reserve_icon = item_reserve_icon (self), reserve_mark = item_reserve_mark (self);

  if (self->icon && *self->icon)
    omarchy_nav_draw_glyph (w, s, self->icon, p->icon, MX (pad, box), 0, box, H, &ink);

  int line = omarchy_line_height (p->body);
  g_autoptr (PangoLayout) l = omarchy_layout (w, self->text, p->body, FALSE);
  int tw;
  omarchy_layout_size (l, &tw, NULL);
  float lx = pad + (reserve_icon ? box + gap : 0);
  float ly = omarchy_nav_center (H, line);
  omarchy_draw_layout (s, l, MX (lx, tw), ly, &ink);

  if (self->shortcut && *self->shortcut) {
    g_autoptr (PangoLayout) h = omarchy_layout (w, self->shortcut, p->body_small, FALSE);
    /* Anchored right: at its fractional advance, on the label's baseline, as Qt places it. */
    float hw = (float) omarchy_nav_text_advance (w, self->shortcut, p->body_small, FALSE);
    float hx = W - pad - (reserve_mark ? box + gap : 0) - hw;
    float label_base = pango_layout_get_baseline (l) / (float) PANGO_SCALE;
    float hint_base = pango_layout_get_baseline (h) / (float) PANGO_SCALE;
    float hy = roundf (ly + label_base - hint_base);
    omarchy_draw_layout (s, h, MX (hx, hw), hy, enabled ? &p->text_dim : &p->text_faint);
  }

  gboolean mark = self->checkable && self->checked;
  if (mark && self->exclusive) {
    float dot = omarchy_space (6);
    float mx = MX (W - pad - box, box) + roundf ((box - dot) / 2.0f);
    float my = omarchy_nav_center (H, dot);
    const GdkRGBA *c = !enabled ? &p->text_faint : &p->accent;
    omarchy_draw_fill (s, &GRAPHENE_RECT_INIT (mx, my, dot, dot), dot / 2.0f, c);
  } else if (mark || omarchy_menu_item_get_has_submenu (self)) {
    const char *glyph = mark ? "✓" : "\xef\x81\x94"; /* U+F054 */
    int px = mark ? p->body : p->icon_small;
    const GdkRGBA *c = !enabled ? &p->text_faint : mark ? &p->accent : &p->text_dim;
    omarchy_nav_draw_glyph (w, s, glyph, px, MX (W - pad - box, box), 0, box, H, c);
  }
#undef MX
}

static void
item_clicked (GtkWidget *w, guint button, double x, double y)
{
  if (button == GDK_BUTTON_PRIMARY)
    omarchy_menu_item_activate (OMARCHY_MENU_ITEM (w));
}

static void
item_enter (GtkEventControllerMotion *c, double x, double y, OmarchyMenuItem *self)
{
  OmarchyMenu *m = omarchy_menu_item_get_menu (self);
  if (m && gtk_widget_is_sensitive (GTK_WIDGET (self)))
    omarchy_menu_set_current_index (m, menu_row_index (m, self));
}

static void
omarchy_menu_item_init (OmarchyMenuItem *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  self->text = g_strdup ("");
  self->icon = g_strdup ("");
  self->shortcut = g_strdup ("");
  omarchy_widget_follow (w);
  omarchy_widget_add_click (w, item_clicked);
  GtkEventController *motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "enter", G_CALLBACK (item_enter), self);
  gtk_widget_add_controller (w, motion);
}

static void
item_dispose (GObject *o)
{
  OmarchyMenuItem *self = OMARCHY_MENU_ITEM (o);
  if (self->submenu) {
    OmarchyMenuPrivate *sp = MENU_PRIV (self->submenu);
    if (sp->popover && gtk_widget_get_parent (GTK_WIDGET (sp->popover)) == GTK_WIDGET (self)) {
      menu_drop_popover (self->submenu);
      g_object_unref (self->submenu);
    }
    g_clear_object (&self->submenu);
  }
  omarchy_widget_dispose_children (GTK_WIDGET (o));
  G_OBJECT_CLASS (omarchy_menu_item_parent_class)->dispose (o);
}

static void
item_finalize (GObject *o)
{
  OmarchyMenuItem *self = OMARCHY_MENU_ITEM (o);
  g_free (self->text);
  g_free (self->icon);
  g_free (self->shortcut);
  G_OBJECT_CLASS (omarchy_menu_item_parent_class)->finalize (o);
}

static void
item_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  OmarchyMenuItem *self = OMARCHY_MENU_ITEM (o);
  switch (id) {
  case MI_PROP_TEXT: omarchy_menu_item_set_text (self, g_value_get_string (v)); break;
  case MI_PROP_ICON: omarchy_menu_item_set_icon (self, g_value_get_string (v)); break;
  case MI_PROP_SHORTCUT: omarchy_menu_item_set_shortcut (self, g_value_get_string (v)); break;
  case MI_PROP_CHECKABLE: omarchy_menu_item_set_checkable (self, g_value_get_boolean (v)); break;
  case MI_PROP_CHECKED: omarchy_menu_item_set_checked (self, g_value_get_boolean (v)); break;
  case MI_PROP_EXCLUSIVE: omarchy_menu_item_set_exclusive (self, g_value_get_boolean (v)); break;
  case MI_PROP_DESTRUCTIVE: omarchy_menu_item_set_destructive (self, g_value_get_boolean (v)); break;
  case MI_PROP_CURSOR: omarchy_menu_item_set_cursor (self, g_value_get_boolean (v)); break;
  case MI_PROP_HAS_SUBMENU: omarchy_menu_item_set_has_submenu (self, g_value_get_boolean (v)); break;
  case MI_PROP_SUBMENU: omarchy_menu_item_set_submenu (self, g_value_get_object (v)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static void
item_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  OmarchyMenuItem *self = OMARCHY_MENU_ITEM (o);
  switch (id) {
  case MI_PROP_TEXT: g_value_set_string (v, self->text); break;
  case MI_PROP_ICON: g_value_set_string (v, self->icon); break;
  case MI_PROP_SHORTCUT: g_value_set_string (v, self->shortcut); break;
  case MI_PROP_CHECKABLE: g_value_set_boolean (v, self->checkable); break;
  case MI_PROP_CHECKED: g_value_set_boolean (v, self->checked); break;
  case MI_PROP_EXCLUSIVE: g_value_set_boolean (v, self->exclusive); break;
  case MI_PROP_DESTRUCTIVE: g_value_set_boolean (v, self->destructive); break;
  case MI_PROP_CURSOR: g_value_set_boolean (v, self->cursor); break;
  case MI_PROP_HAS_SUBMENU: g_value_set_boolean (v, omarchy_menu_item_get_has_submenu (self)); break;
  case MI_PROP_SUBMENU: g_value_set_object (v, self->submenu); break;
  case MI_PROP_HIGHLIGHTED: g_value_set_boolean (v, omarchy_menu_item_get_highlighted (self)); break;
  case MI_PROP_HOT: g_value_set_boolean (v, omarchy_menu_item_get_hot (self)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
  }
}

static GtkBuildableIface *item_parent_buildable;

static void
item_add_child (GtkBuildable *b, GtkBuilder *builder, GObject *child, const char *type)
{
  if (OMARCHY_IS_MENU (child))
    omarchy_menu_item_set_submenu (OMARCHY_MENU_ITEM (b), OMARCHY_MENU (child));
  else
    item_parent_buildable->add_child (b, builder, child, type);
}

static void
item_buildable_init (GtkBuildableIface *iface)
{
  item_parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = item_add_child;
}

static void
omarchy_menu_item_class_init (OmarchyMenuItemClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = item_dispose;
  oc->finalize = item_finalize;
  oc->set_property = item_set_property;
  oc->get_property = item_get_property;
  wc->measure = item_measure;
  wc->size_allocate = item_allocate;
  wc->snapshot = item_snapshot;
  const GParamFlags f = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  const GParamFlags ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
  mi_props[MI_PROP_TEXT] = g_param_spec_string ("text", NULL, NULL, "", f);
  /**
   * OmarchyMenuItem:icon:
   *
   * A leading glyph (Nerd Font codepoint), centered by its ink in an `icon`-wide column.
   */
  mi_props[MI_PROP_ICON] = g_param_spec_string ("icon", NULL, NULL, "", f);
  /**
   * OmarchyMenuItem:shortcut:
   *
   * A hint at the right ("Ctrl+S"), as plain dim text.
   */
  mi_props[MI_PROP_SHORTCUT] = g_param_spec_string ("shortcut", NULL, NULL, "", f);
  mi_props[MI_PROP_CHECKABLE] = g_param_spec_boolean ("checkable", NULL, NULL, FALSE, f);
  mi_props[MI_PROP_CHECKED] = g_param_spec_boolean ("checked", NULL, NULL, FALSE, f);
  /**
   * OmarchyMenuItem:exclusive:
   *
   * A radio mark: one of a set of exclusive choices (the exclusive rows next to each other).
   */
  mi_props[MI_PROP_EXCLUSIVE] = g_param_spec_boolean ("exclusive", NULL, NULL, FALSE, f);
  mi_props[MI_PROP_DESTRUCTIVE] = g_param_spec_boolean ("destructive", NULL, NULL, FALSE, f);
  /**
   * OmarchyMenuItem:cursor:
   *
   * Force the cursor look, for stills and panels with their own cursor model (Qt's hasCursor).
   */
  mi_props[MI_PROP_CURSOR] = g_param_spec_boolean ("cursor", NULL, NULL, FALSE, f);
  /**
   * OmarchyMenuItem:has-submenu:
   *
   * Draws the submenu chevron; set by #OmarchyMenuItem:submenu.
   */
  mi_props[MI_PROP_HAS_SUBMENU] = g_param_spec_boolean ("has-submenu", NULL, NULL, FALSE, f);
  /**
   * OmarchyMenuItem:submenu:
   *
   * The menu Right, l or a click opens beside the row. The row owns it.
   */
  mi_props[MI_PROP_SUBMENU] = g_param_spec_object ("submenu", NULL, NULL, OMARCHY_TYPE_MENU, f);
  /**
   * OmarchyMenuItem:highlighted:
   *
   * The menu's keyboard cursor is on the row.
   */
  mi_props[MI_PROP_HIGHLIGHTED] = g_param_spec_boolean ("highlighted", NULL, NULL, FALSE, ro);
  /**
   * OmarchyMenuItem:hot:
   *
   * Drawn with the cursor look: sensitive, and hovered, highlighted or #OmarchyMenuItem:cursor.
   */
  mi_props[MI_PROP_HOT] = g_param_spec_boolean ("hot", NULL, NULL, FALSE, ro);
  g_object_class_install_properties (oc, MI_N_PROPS, mi_props);
  /**
   * OmarchyMenuItem::triggered:
   *
   * The row ran (a click, or Return/Space on the menu's cursor). Not for submenu rows.
   */
  mi_signals[MI_TRIGGERED] = g_signal_new ("triggered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                           NULL, G_TYPE_NONE, 0);
  gtk_widget_class_set_css_name (wc, "omarchy-menu-item");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_MENU_ITEM);
}

/**
 * omarchy_menu_item_new:
 * @text: (nullable): the label
 *
 * Returns: (transfer floating): a menu row
 */
GtkWidget *
omarchy_menu_item_new (const char *text)
{
  return g_object_new (OMARCHY_TYPE_MENU_ITEM, "text", text ? text : "", NULL);
}

static void
item_set_string (OmarchyMenuItem *self, char **field, const char *v, int prop, gboolean reflow)
{
  if (!g_strcmp0 (*field, v))
    return;
  g_free (*field);
  *field = g_strdup (v ? v : "");
  if (reflow)
    item_menu_reflow (self);
  else
    gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), mi_props[prop]);
}

const char *
omarchy_menu_item_get_text (OmarchyMenuItem *self)
{
  return self->text;
}

void
omarchy_menu_item_set_text (OmarchyMenuItem *self, const char *text)
{
  item_set_string (self, &self->text, text, MI_PROP_TEXT, FALSE);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_LABEL, self->text, -1);
}

const char *
omarchy_menu_item_get_icon (OmarchyMenuItem *self)
{
  return self->icon;
}

void
omarchy_menu_item_set_icon (OmarchyMenuItem *self, const char *glyph)
{
  item_set_string (self, &self->icon, glyph, MI_PROP_ICON, TRUE);
}

const char *
omarchy_menu_item_get_shortcut (OmarchyMenuItem *self)
{
  return self->shortcut;
}

void
omarchy_menu_item_set_shortcut (OmarchyMenuItem *self, const char *shortcut)
{
  item_set_string (self, &self->shortcut, shortcut, MI_PROP_SHORTCUT, FALSE);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_PROPERTY_KEY_SHORTCUTS, self->shortcut, -1);
}

static void
item_set_flag (OmarchyMenuItem *self, gboolean *field, gboolean v, int prop, gboolean reflow)
{
  if (*field == !!v)
    return;
  *field = !!v;
  if (reflow)
    item_menu_reflow (self);
  else
    gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), mi_props[prop]);
}

gboolean
omarchy_menu_item_get_checkable (OmarchyMenuItem *self)
{
  return self->checkable;
}

void
omarchy_menu_item_set_checkable (OmarchyMenuItem *self, gboolean checkable)
{
  item_set_flag (self, &self->checkable, checkable, MI_PROP_CHECKABLE, TRUE);
}

gboolean
omarchy_menu_item_get_checked (OmarchyMenuItem *self)
{
  return self->checked;
}

void
omarchy_menu_item_set_checked (OmarchyMenuItem *self, gboolean checked)
{
  item_set_flag (self, &self->checked, checked, MI_PROP_CHECKED, FALSE);
  gtk_accessible_update_state (GTK_ACCESSIBLE (self), GTK_ACCESSIBLE_STATE_CHECKED,
                               self->checkable ? (self->checked ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE)
                                               : GTK_ACCESSIBLE_TRISTATE_FALSE,
                               -1);
}

gboolean
omarchy_menu_item_get_exclusive (OmarchyMenuItem *self)
{
  return self->exclusive;
}

void
omarchy_menu_item_set_exclusive (OmarchyMenuItem *self, gboolean exclusive)
{
  item_set_flag (self, &self->exclusive, exclusive, MI_PROP_EXCLUSIVE, FALSE);
}

gboolean
omarchy_menu_item_get_destructive (OmarchyMenuItem *self)
{
  return self->destructive;
}

void
omarchy_menu_item_set_destructive (OmarchyMenuItem *self, gboolean destructive)
{
  item_set_flag (self, &self->destructive, destructive, MI_PROP_DESTRUCTIVE, FALSE);
}

gboolean
omarchy_menu_item_get_cursor (OmarchyMenuItem *self)
{
  return self->cursor;
}

void
omarchy_menu_item_set_cursor (OmarchyMenuItem *self, gboolean cursor)
{
  item_set_flag (self, &self->cursor, cursor, MI_PROP_CURSOR, FALSE);
}

gboolean
omarchy_menu_item_get_has_submenu (OmarchyMenuItem *self)
{
  return self->has_submenu || self->submenu;
}

void
omarchy_menu_item_set_has_submenu (OmarchyMenuItem *self, gboolean has_submenu)
{
  item_set_flag (self, &self->has_submenu, has_submenu, MI_PROP_HAS_SUBMENU, TRUE);
}

/**
 * omarchy_menu_item_get_submenu:
 * @self: the row
 *
 * Returns: (transfer none) (nullable): the menu it opens
 */
OmarchyMenu *
omarchy_menu_item_get_submenu (OmarchyMenuItem *self)
{
  return self->submenu;
}

/**
 * omarchy_menu_item_set_submenu:
 * @self: the row
 * @submenu: (nullable) (transfer floating): the menu it opens beside itself
 */
void
omarchy_menu_item_set_submenu (OmarchyMenuItem *self, OmarchyMenu *submenu)
{
  if (self->submenu == submenu)
    return;
  if (self->submenu) {
    OmarchyMenuPrivate *sp = MENU_PRIV (self->submenu);
    omarchy_menu_close (self->submenu);
    if (sp->popover && gtk_widget_get_parent (GTK_WIDGET (sp->popover)) == GTK_WIDGET (self)) {
      menu_drop_popover (self->submenu);
      g_object_unref (self->submenu);
    }
    sp->parent_menu = NULL;
    g_clear_object (&self->submenu);
  }
  if (submenu)
    self->submenu = g_object_ref_sink (submenu);
  item_menu_reflow (self);
  g_object_notify_by_pspec (G_OBJECT (self), mi_props[MI_PROP_SUBMENU]);
}

gboolean
omarchy_menu_item_get_highlighted (OmarchyMenuItem *self)
{
  OmarchyMenu *m = omarchy_menu_item_get_menu (self);
  return m && omarchy_menu_get_current_item (m) == self;
}

gboolean
omarchy_menu_item_get_hot (OmarchyMenuItem *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  return gtk_widget_is_sensitive (w)
         && (omarchy_widget_hovered (w) || self->cursor || omarchy_menu_item_get_highlighted (self));
}

/* Check one row of a set: the run of exclusive rows around it. */
static void
item_choose_exclusive (OmarchyMenuItem *self)
{
  GtkWidget *w = GTK_WIDGET (self);
  GtkWidget *first = w;
  for (GtkWidget *c = gtk_widget_get_prev_sibling (w); c && OMARCHY_IS_MENU_ITEM (c) && OMARCHY_MENU_ITEM (c)->exclusive;
       c = gtk_widget_get_prev_sibling (c))
    first = c;
  for (GtkWidget *c = first; c && OMARCHY_IS_MENU_ITEM (c) && OMARCHY_MENU_ITEM (c)->exclusive; c = gtk_widget_get_next_sibling (c))
    if (OMARCHY_MENU_ITEM (c)->checkable)
      omarchy_menu_item_set_checked (OMARCHY_MENU_ITEM (c), c == w);
}

/**
 * omarchy_menu_item_activate:
 * @self: the row
 *
 * Runs the row: opens its submenu, or (flipping a checkable row first) emits ::triggered and the
 * menu's ::triggered, and lets the menu close. Nothing when insensitive.
 */
void
omarchy_menu_item_activate (OmarchyMenuItem *self)
{
  if (!gtk_widget_is_sensitive (GTK_WIDGET (self)))
    return;
  OmarchyMenu *m = omarchy_menu_item_get_menu (self);
  if (omarchy_menu_item_get_has_submenu (self)) {
    if (m)
      menu_open_submenu (m, self);
    return;
  }
  if (self->checkable) {
    if (self->exclusive)
      item_choose_exclusive (self);
    else
      omarchy_menu_item_set_checked (self, !self->checked);
  }
  g_object_ref (self);
  g_signal_emit (self, mi_signals[MI_TRIGGERED], 0);
  if (m)
    menu_item_triggered (m, self);
  g_object_unref (self);
}

/* ========================================================================= MenuSeparator === */

/**
 * OmarchyMenuSeparator:
 *
 * A rule between groups of menu rows: 1 px in `track`, `sm` above and below.
 */
struct _OmarchyMenuSeparator {
  GtkWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (OmarchyMenuSeparator, omarchy_menu_separator, GTK_TYPE_WIDGET)

static void
msep_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  *min = *nat = o == GTK_ORIENTATION_HORIZONTAL ? 0 : omarchy_tokens ()->sm * 2 + 1;
}

static void
msep_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  const OmarchyTokens *p = omarchy_paint ();
  gtk_snapshot_append_color (s, &p->track, &GRAPHENE_RECT_INIT (0, p->sm, gtk_widget_get_width (w), 1));
}

static void
omarchy_menu_separator_init (OmarchyMenuSeparator *self)
{
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
omarchy_menu_separator_class_init (OmarchyMenuSeparatorClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  wc->measure = msep_measure;
  wc->snapshot = msep_snapshot;
  gtk_widget_class_set_css_name (wc, "omarchy-menu-separator");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_SEPARATOR);
}

/**
 * omarchy_menu_separator_new:
 *
 * Returns: (transfer floating): a menu rule
 */
GtkWidget *
omarchy_menu_separator_new (void)
{
  return g_object_new (OMARCHY_TYPE_MENU_SEPARATOR, NULL);
}

/* =========================================================================== MenuSection === */

/**
 * OmarchyMenuSection:
 *
 * A label heading a group of menu rows: the section header style (`caption`, bold, `textDim`,
 * its 15% top pad), inset `controlPaddingX` like the rows' labels, `sm` above and `xxs` below.
 */
struct _OmarchyMenuSection {
  GtkWidget parent_instance;
  char *text;
};

enum { MS_PROP_0, MS_PROP_TEXT, MS_N_PROPS };
static GParamSpec *ms_props[MS_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyMenuSection, omarchy_menu_section, GTK_TYPE_WIDGET)

static int
msec_pad (void)
{
  return (int) ceil (omarchy_tokens ()->caption * 0.15);
}

static void
msec_measure (GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  OmarchyMenuSection *self = OMARCHY_MENU_SECTION (w);
  const OmarchyTokens *t = omarchy_tokens ();
  if (o == GTK_ORIENTATION_HORIZONTAL)
    *min = *nat = omarchy_nav_text_width (w, self->text, t->caption, TRUE) + t->control_padding_x * 2;
  else
    *min = *nat = t->sm + omarchy_line_height (t->caption) + msec_pad () + t->xxs;
}

static void
msec_snapshot (GtkWidget *w, GtkSnapshot *s)
{
  OmarchyMenuSection *self = OMARCHY_MENU_SECTION (w);
  const OmarchyTokens *p = omarchy_paint ();
  g_autoptr (PangoLayout) l = omarchy_layout (w, self->text, p->caption, TRUE);
  int tw;
  omarchy_layout_size (l, &tw, NULL);
  float x = gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL ? gtk_widget_get_width (w) - p->control_padding_x - tw
                                                              : p->control_padding_x;
  omarchy_draw_layout (s, l, x, p->sm + msec_pad (), &p->text_dim);
}

static void
omarchy_menu_section_init (OmarchyMenuSection *self)
{
  self->text = g_strdup ("");
  omarchy_widget_follow (GTK_WIDGET (self));
}

static void
msec_finalize (GObject *o)
{
  g_free (OMARCHY_MENU_SECTION (o)->text);
  G_OBJECT_CLASS (omarchy_menu_section_parent_class)->finalize (o);
}

static void
msec_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  if (id == MS_PROP_TEXT)
    omarchy_menu_section_set_text (OMARCHY_MENU_SECTION (o), g_value_get_string (v));
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
msec_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  if (id == MS_PROP_TEXT)
    g_value_set_string (v, OMARCHY_MENU_SECTION (o)->text);
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
omarchy_menu_section_class_init (OmarchyMenuSectionClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->finalize = msec_finalize;
  oc->set_property = msec_set_property;
  oc->get_property = msec_get_property;
  wc->measure = msec_measure;
  wc->snapshot = msec_snapshot;
  ms_props[MS_PROP_TEXT] = g_param_spec_string ("text", NULL, NULL, "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, MS_N_PROPS, ms_props);
  gtk_widget_class_set_css_name (wc, "omarchy-menu-section");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_HEADING);
}

/**
 * omarchy_menu_section_new:
 * @text: (nullable): the label
 *
 * Returns: (transfer floating): a menu section label
 */
GtkWidget *
omarchy_menu_section_new (const char *text)
{
  return g_object_new (OMARCHY_TYPE_MENU_SECTION, "text", text ? text : "", NULL);
}

OMARCHY_STRING_ACCESSORS (OmarchyMenuSection, menu_section, text, ms_props, MS_PROP_TEXT, TRUE)

/* =========================================================================== ContextMenu === */

/**
 * OmarchyContextMenu:
 *
 * An #OmarchyMenu that opens at a point (the pointer) instead of under a trigger: give it a
 * #OmarchyContextMenu:target and a secondary press there opens it at the pointer (the Menu key
 * or Shift+F10 while the target has the focus opens it at the target's bottom-left).
 * omarchy_menu_popup() opens it anywhere.
 */
struct _OmarchyContextMenu {
  OmarchyMenu parent_instance;
  GtkWidget *target; /* weak */
  GtkEventController *click, *keys;
};

enum { CM_PROP_0, CM_PROP_TARGET, CM_N_PROPS };
static GParamSpec *cm_props[CM_N_PROPS];

G_DEFINE_FINAL_TYPE (OmarchyContextMenu, omarchy_context_menu, OMARCHY_TYPE_MENU)

static void
context_pressed (GtkGestureClick *g, int n, double x, double y, OmarchyContextMenu *self)
{
  if (!self->target)
    return;
  gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_CLAIMED);
  omarchy_menu_popup (OMARCHY_MENU (self), self->target, x, y);
}

static gboolean
context_key (GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType state, OmarchyContextMenu *self)
{
  if (!self->target)
    return FALSE;
  if (keyval == GDK_KEY_Menu || (keyval == GDK_KEY_F10 && (state & GDK_SHIFT_MASK))) {
    omarchy_menu_popup (OMARCHY_MENU (self), self->target, 0, gtk_widget_get_height (self->target));
    return TRUE;
  }
  return FALSE;
}

static void
context_unhook (OmarchyContextMenu *self)
{
  if (!self->target)
    return;
  gtk_widget_remove_controller (self->target, self->click);
  gtk_widget_remove_controller (self->target, self->keys);
  self->click = self->keys = NULL;
  g_object_remove_weak_pointer (G_OBJECT (self->target), (gpointer *) &self->target);
  self->target = NULL;
}

static void
omarchy_context_menu_init (OmarchyContextMenu *self)
{
}

static void
context_dispose (GObject *o)
{
  context_unhook (OMARCHY_CONTEXT_MENU (o));
  G_OBJECT_CLASS (omarchy_context_menu_parent_class)->dispose (o);
}

static void
context_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  if (id == CM_PROP_TARGET)
    omarchy_context_menu_set_target (OMARCHY_CONTEXT_MENU (o), g_value_get_object (v));
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
context_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  if (id == CM_PROP_TARGET)
    g_value_set_object (v, OMARCHY_CONTEXT_MENU (o)->target);
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
}

static void
omarchy_context_menu_class_init (OmarchyContextMenuClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);
  oc->dispose = context_dispose;
  oc->set_property = context_set_property;
  oc->get_property = context_get_property;
  /**
   * OmarchyContextMenu:target:
   *
   * Where secondary presses open the menu at the pointer. It owns the menu from then on.
   */
  cm_props[CM_PROP_TARGET] = g_param_spec_object ("target", NULL, NULL, GTK_TYPE_WIDGET,
                                                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, CM_N_PROPS, cm_props);
  gtk_widget_class_set_css_name (wc, "omarchy-context-menu");
}

/**
 * omarchy_context_menu_new:
 *
 * Returns: (transfer floating): an empty context menu
 */
GtkWidget *
omarchy_context_menu_new (void)
{
  return g_object_new (OMARCHY_TYPE_CONTEXT_MENU, NULL);
}

/**
 * omarchy_context_menu_get_target:
 * @self: the menu
 *
 * Returns: (transfer none) (nullable): where secondary presses open it
 */
GtkWidget *
omarchy_context_menu_get_target (OmarchyContextMenu *self)
{
  return self->target;
}

/**
 * omarchy_context_menu_set_target:
 * @self: the menu
 * @target: (nullable): where secondary presses open it at the pointer
 */
void
omarchy_context_menu_set_target (OmarchyContextMenu *self, GtkWidget *target)
{
  if (self->target == target)
    return;
  context_unhook (self);
  if (target) {
    self->target = target;
    g_object_add_weak_pointer (G_OBJECT (target), (gpointer *) &self->target);
    GtkGesture *g = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (g), GDK_BUTTON_SECONDARY);
    g_signal_connect (g, "pressed", G_CALLBACK (context_pressed), self);
    self->click = GTK_EVENT_CONTROLLER (g);
    gtk_widget_add_controller (target, self->click);
    self->keys = gtk_event_controller_key_new ();
    g_signal_connect (self->keys, "key-pressed", G_CALLBACK (context_key), self);
    gtk_widget_add_controller (target, self->keys);
    if (!omarchy_menu_get_held (OMARCHY_MENU (self)))
      menu_popover_on (OMARCHY_MENU (self), target);
  }
  g_object_notify_by_pspec (G_OBJECT (self), cm_props[CM_PROP_TARGET]);
}
