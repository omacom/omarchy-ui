/* Toggles, fields, slider, dialog, pane: clicks and keys (qt/tests/tst_surfaces.qml and more). */
#include <omarchy-gtk.h>

#include "drive.h"

static int fired;

static void
count (gpointer a, gpointer b)
{
  fired++;
}

TEST (test_toggle_row_flips_on_click_and_space, "/surfaces/toggle-row")
{
  GtkWidget *t = omarchy_toggle_new ("Wi-Fi", "Basecamp");
  GtkWidget *window = drive_window (t, 360, 100);
  fired = 0;
  g_signal_connect (t, "toggled", G_CALLBACK (count), NULL);
  drive_click (t, 20, 20);
  g_assert_true (omarchy_toggle_get_checked (OMARCHY_TOGGLE (t)));
  gtk_widget_grab_focus (t);
  drive_key (window, NULL, GDK_KEY_space, 0);
  g_assert_false (omarchy_toggle_get_checked (OMARCHY_TOGGLE (t)));
  drive_key (window, NULL, GDK_KEY_Return, 0);
  g_assert_true (omarchy_toggle_get_checked (OMARCHY_TOGGLE (t)));
  g_assert_cmpint (fired, ==, 3);
  drive_close (window);
}

TEST (test_switch_flips_unless_busy, "/toggles/switch")
{
  GtkWidget *s = omarchy_toggle_switch_new ();
  gtk_widget_set_halign (s, GTK_ALIGN_CENTER);
  GtkWidget *window = drive_window (s, 200, 100);
  drive_click (s, -1, -1);
  g_assert_true (omarchy_toggle_switch_get_checked (OMARCHY_TOGGLE_SWITCH (s)));
  omarchy_toggle_switch_set_busy (OMARCHY_TOGGLE_SWITCH (s), TRUE);
  drive_click (s, -1, -1);
  g_assert_true (omarchy_toggle_switch_get_checked (OMARCHY_TOGGLE_SWITCH (s)));
  omarchy_toggle_switch_set_busy (OMARCHY_TOGGLE_SWITCH (s), FALSE);
  gtk_widget_grab_focus (s);
  drive_key (window, NULL, GDK_KEY_space, 0);
  g_assert_false (omarchy_toggle_switch_get_checked (OMARCHY_TOGGLE_SWITCH (s)));
  drive_close (window);
}

TEST (test_text_field_is_editable, "/inputs/text-field")
{
  GtkWidget *f = omarchy_text_field_new ();
  omarchy_text_field_set_placeholder_text (OMARCHY_TEXT_FIELD (f), "Search…");
  GtkWidget *window = drive_window (f, 240, 60);
  fired = 0;
  g_signal_connect (f, "changed", G_CALLBACK (count), NULL);
  gtk_editable_set_text (GTK_EDITABLE (f), "Omarchy");
  g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (f)), ==, "Omarchy");
  g_assert_cmpint (fired, ==, 1);
  g_assert_true (gtk_widget_grab_focus (f));
  g_assert_true (gtk_widget_get_state_flags (f) & GTK_STATE_FLAG_FOCUS_WITHIN);
  drive_close (window);
}

static int modified;

static void
on_modified (OmarchyNumberField *f, int value, gpointer data)
{
  modified = value;
}

TEST (test_number_field_steps_and_clamps, "/inputs/number-field")
{
  GtkWidget *n = omarchy_number_field_new ("Gaps", 0, 40);
  omarchy_number_field_set_value (OMARCHY_NUMBER_FIELD (n), 10);
  gtk_widget_set_halign (n, GTK_ALIGN_START);
  gtk_widget_set_valign (n, GTK_ALIGN_START);
  GtkWidget *window = drive_window (n, 200, 100);
  g_signal_connect (n, "modified", G_CALLBACK (on_modified), NULL);
  gtk_widget_grab_focus (n);
  drive_key (window, NULL, GDK_KEY_Up, 0);
  g_assert_cmpint (omarchy_number_field_get_value (OMARCHY_NUMBER_FIELD (n)), ==, 11);
  g_assert_cmpint (modified, ==, 11);
  drive_key (window, NULL, GDK_KEY_Down, 0);
  drive_key (window, NULL, GDK_KEY_Down, 0);
  g_assert_cmpint (omarchy_number_field_get_value (OMARCHY_NUMBER_FIELD (n)), ==, 9);
  omarchy_number_field_set_value (OMARCHY_NUMBER_FIELD (n), 99);
  g_assert_cmpint (omarchy_number_field_get_value (OMARCHY_NUMBER_FIELD (n)), ==, 40);
  drive_close (window);
}

static char *chosen;

static void
on_changed (GtkWidget *w, const char *value, gpointer data)
{
  g_free (chosen);
  chosen = g_strdup (value);
}

TEST (test_dropdown_opens_walks_and_chooses, "/inputs/dropdown")
{
  GtkWidget *d = omarchy_dropdown_new ("Theme");
  OmarchyDropdown *dd = OMARCHY_DROPDOWN (d);
  omarchy_dropdown_append (dd, "tokyo-night", "Tokyo Night");
  omarchy_dropdown_append (dd, "nord", "Nord");
  omarchy_dropdown_append (dd, "white", "White");
  omarchy_dropdown_set_value (dd, "tokyo-night");
  gtk_widget_set_valign (d, GTK_ALIGN_START);
  g_signal_connect (d, "changed", G_CALLBACK (on_changed), NULL);
  GtkWidget *window = drive_window (d, 300, 300);
  gtk_widget_grab_focus (d);
  drive_key (window, d, GDK_KEY_space, 0);
  drive_wait (50);
  g_assert_true (omarchy_dropdown_get_popup_open (dd));
  g_assert_cmpint (omarchy_dropdown_get_current_index (dd), ==, 0);
  GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (gtk_widget_get_root (gtk_widget_get_first_child (d) ? d : d)));
  (void) focus;
  drive_key (window, NULL, GDK_KEY_j, 0);
  drive_key (window, NULL, GDK_KEY_Down, 0);
  drive_key (window, NULL, GDK_KEY_k, 0);
  g_assert_cmpint (omarchy_dropdown_get_current_index (dd), ==, 1);
  drive_key (window, NULL, GDK_KEY_Return, 0);
  drive_wait (50);
  g_assert_cmpstr (chosen, ==, "nord");
  g_assert_cmpstr (omarchy_dropdown_get_value (dd), ==, "nord");
  g_assert_false (omarchy_dropdown_get_popup_open (dd));
  /* Escape closes without choosing. */
  drive_key (window, d, GDK_KEY_Return, 0);
  drive_wait (50);
  g_assert_true (omarchy_dropdown_get_popup_open (dd));
  drive_key (window, NULL, GDK_KEY_Escape, 0);
  drive_wait (50);
  g_assert_false (omarchy_dropdown_get_popup_open (dd));
  g_assert_cmpstr (omarchy_dropdown_get_value (dd), ==, "nord");
  drive_close (window);
}

static double moved = -1;

static void
on_moved (OmarchySlider *s, double v, gpointer data)
{
  moved = v;
}

TEST (test_slider_keys_and_clicks, "/slider/keys")
{
  GtkWidget *s = omarchy_slider_new (0.5);
  GtkWidget *window = drive_window (s, 300, 60);
  g_signal_connect (s, "moved", G_CALLBACK (on_moved), NULL);
  gtk_widget_grab_focus (s);
  drive_key (window, NULL, GDK_KEY_l, 0);
  g_assert_cmpfloat_with_epsilon (omarchy_slider_get_value (OMARCHY_SLIDER (s)), 0.55, 1e-9);
  drive_key (window, NULL, GDK_KEY_Home, 0);
  g_assert_cmpfloat (omarchy_slider_get_value (OMARCHY_SLIDER (s)), ==, 0);
  drive_key (window, NULL, GDK_KEY_End, 0);
  g_assert_cmpfloat (moved, ==, 1);
  drive_close (window);
}

static int canceled, confirmed;

static void on_cancel (GtkWidget *w, gpointer d) { canceled++; }
static void on_confirm (GtkWidget *w, gpointer d) { confirmed++; }

TEST (test_confirm_dialog_keys, "/surfaces/confirm-dialog")
{
  GtkWidget *c = omarchy_confirm_dialog_new ("Remove?");
  GtkWidget *window = drive_window (c, 400, 200);
  g_signal_connect (c, "canceled", G_CALLBACK (on_cancel), NULL);
  g_signal_connect (c, "confirmed", G_CALLBACK (on_confirm), NULL);
  gtk_widget_grab_focus (c);
  g_assert_cmpint (omarchy_confirm_dialog_get_selected_index (OMARCHY_CONFIRM_DIALOG (c)), ==, 1);
  drive_key (window, NULL, GDK_KEY_Return, 0);
  g_assert_cmpint (confirmed, ==, 1);
  drive_key (window, NULL, GDK_KEY_h, 0);
  g_assert_cmpint (omarchy_confirm_dialog_get_selected_index (OMARCHY_CONFIRM_DIALOG (c)), ==, 0);
  drive_key (window, NULL, GDK_KEY_Return, 0);
  drive_key (window, NULL, GDK_KEY_Escape, 0);
  g_assert_cmpint (canceled, ==, 2);
  drive_close (window);
}

TEST (test_pane_action_ends_the_header_and_runs, "/surfaces/pane-action")
{
  GtkWidget *pane = omarchy_pane_new ("Layers");
  GtkWidget *add = omarchy_icon_button_new ("+");
  omarchy_pane_add_action (OMARCHY_PANE (pane), add);
  omarchy_pane_append (OMARCHY_PANE (pane), omarchy_menu_row_new ("Background"));
  GtkWidget *window = drive_window (pane, 240, 200);
  graphene_point_t p;
  g_assert_true (gtk_widget_compute_point (add, pane, &GRAPHENE_POINT_INIT (0, 0), &p));
  /* At the end of the header, inside the padding. */
  int pad = omarchy_theme_get_spacing (omarchy_theme_get_default (), "panelPadding");
  g_assert_cmpfloat (p.x + gtk_widget_get_width (add), ==, gtk_widget_get_width (pane) - pad);
  fired = 0;
  g_signal_connect (add, "clicked", G_CALLBACK (count), NULL);
  drive_click (add, -1, -1);
  g_assert_cmpint (fired, ==, 1);
  drive_close (window);
}

TEST (test_round2_polish_geometry, "/polish/geometry")
{
  OmarchyTheme *theme = omarchy_theme_get_default ();
  int control_h = omarchy_theme_get_spacing (theme, "controlHeight");
  int md = omarchy_theme_get_spacing (theme, "md");

  /* Labeled NumberField, Dropdown, and MultiSelect share the same natural height. */
  GtkWidget *nf = omarchy_number_field_new ("Gaps", 0, 40);
  GtkWidget *dd = omarchy_dropdown_new ("Theme");
  GtkWidget *ms = omarchy_multi_select_new ("Tags");
  g_object_ref_sink (nf);
  g_object_ref_sink (dd);
  g_object_ref_sink (ms);
  int nf_h, dd_h, ms_h;
  gtk_widget_measure (nf, GTK_ORIENTATION_VERTICAL, 220, NULL, &nf_h, NULL, NULL);
  gtk_widget_measure (dd, GTK_ORIENTATION_VERTICAL, 220, NULL, &dd_h, NULL, NULL);
  gtk_widget_measure (ms, GTK_ORIENTATION_VERTICAL, 220, NULL, &ms_h, NULL, NULL);
  g_assert_cmpint (nf_h, ==, dd_h);
  g_assert_cmpint (ms_h, ==, dd_h);
  g_object_unref (nf);
  g_object_unref (dd);
  g_object_unref (ms);

  /* SettingRow with ToggleSwitch or ColorWell stays controlHeight + 2 * md (40pt at scale 1). */
  GtkWidget *row_sw = omarchy_setting_row_new ("Auto-hide", NULL, omarchy_toggle_switch_new ());
  GtkWidget *row_cw = omarchy_setting_row_new ("Accent", NULL, omarchy_color_well_new (NULL));
  g_object_ref_sink (row_sw);
  g_object_ref_sink (row_cw);
  int sw_h, cw_h;
  gtk_widget_measure (row_sw, GTK_ORIENTATION_VERTICAL, 360, NULL, &sw_h, NULL, NULL);
  gtk_widget_measure (row_cw, GTK_ORIENTATION_VERTICAL, 360, NULL, &cw_h, NULL, NULL);
  g_assert_cmpint (sw_h, ==, control_h + 2 * md);
  g_assert_cmpint (cw_h, ==, control_h + 2 * md);
  g_object_unref (row_sw);
  g_object_unref (row_cw);
}
