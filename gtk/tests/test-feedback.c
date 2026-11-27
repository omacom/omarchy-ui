/* Feedback: Meter, ProgressBar, Spinner, EmptyState, Banner and Toast (spec/components/feedback.md):
 * geometry and state for the drawn ones, clicks and keys for the actions and dismiss. */
#include <omarchy-gtk.h>

#include "drive.h"

static int actions, dismissals;

static void
on_action (GtkWidget *w, gpointer data)
{
  actions++;
}

static void
on_dismissed (GtkWidget *w, gpointer data)
{
  dismissals++;
}

static int
natural_height (GtkWidget *w, int for_width)
{
  int nat;
  gtk_widget_measure (w, GTK_ORIENTATION_VERTICAL, for_width, NULL, &nat, NULL, NULL);
  return nat;
}

/* ---- Meter / ProgressBar / Spinner ------------------------------------------------------ */

TEST (test_meter_geometry, "/feedback/meter")
{
  OmarchyTheme *theme = omarchy_theme_get_default ();
  int thick = omarchy_theme_get_spacing (theme, "meterThickness");
  GtkWidget *m = omarchy_meter_new (1.5);
  g_object_ref_sink (m);
  g_assert_cmpstr (omarchy_meter_get_tone (OMARCHY_METER (m)), ==, "neutral");
  /* Bare: the track's thickness tall, space(200) wide. */
  g_assert_cmpint (natural_height (m, -1), ==, thick);
  int nat;
  gtk_widget_measure (m, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &nat, NULL, NULL);
  g_assert_cmpint (nat, ==, omarchy_theme_space (theme, 200));
  /* Markers: as tall as a tick, round(thickness × 2.5). */
  omarchy_meter_add_marker (OMARCHY_METER (m), 0.5);
  omarchy_meter_add_marker (OMARCHY_METER (m), 0.8);
  g_assert_cmpuint (omarchy_meter_get_n_markers (OMARCHY_METER (m)), ==, 2);
  g_assert_cmpfloat (omarchy_meter_get_marker (OMARCHY_METER (m), 1), ==, 0.8);
  int tick = (int) (thick * 2.5 + 0.5);
  g_assert_cmpint (natural_height (m, -1), ==, MAX (thick, tick));
  omarchy_meter_clear_markers (OMARCHY_METER (m));
  /* A header adds a body line and md. */
  omarchy_meter_set_label (OMARCHY_METER (m), "Battery");
  g_assert_cmpint (natural_height (m, -1), >, thick + omarchy_theme_get_spacing (theme, "md"));
  g_object_unref (m);
}

static int notified;

static void
on_notify (GObject *o, GParamSpec *p, gpointer data)
{
  notified++;
}

TEST (test_meter_value_and_tone, "/feedback/meter-value")
{
  GtkWidget *m = omarchy_meter_new (0.2);
  GtkWidget *window = drive_window (m, 300, 60);
  notified = 0;
  g_signal_connect (m, "notify::value", G_CALLBACK (on_notify), NULL);
  omarchy_meter_set_value (OMARCHY_METER (m), 0.65);
  omarchy_meter_set_value (OMARCHY_METER (m), 0.65);
  g_assert_cmpint (notified, ==, 1);
  g_assert_cmpfloat (omarchy_meter_get_value (OMARCHY_METER (m)), ==, 0.65);
  omarchy_meter_set_tone (OMARCHY_METER (m), NULL);
  g_assert_cmpstr (omarchy_meter_get_tone (OMARCHY_METER (m)), ==, "neutral");
  omarchy_meter_set_alarming (OMARCHY_METER (m), TRUE);
  g_assert_true (omarchy_meter_get_alarming (OMARCHY_METER (m)));
  /* Not interactive. */
  g_assert_false (gtk_widget_get_focusable (m));
  g_assert_false (drive_key (window, m, GDK_KEY_Right, 0));
  g_assert_cmpfloat (omarchy_meter_get_value (OMARCHY_METER (m)), ==, 0.65);
  drive_close (window);
}

TEST (test_progress_bar_is_an_info_meter, "/feedback/progress-bar")
{
  GtkWidget *p = omarchy_progress_bar_new (0.4);
  g_object_ref_sink (p);
  g_assert_true (OMARCHY_IS_METER (p));
  g_assert_cmpstr (omarchy_meter_get_tone (OMARCHY_METER (p)), ==, "info");
  g_assert_false (omarchy_meter_get_indeterminate (OMARCHY_METER (p)));
  g_object_unref (p);

  GtkWidget *busy = omarchy_progress_bar_new_indeterminate ();
  GtkWidget *window = drive_window (busy, 340, 40);
  g_assert_true (omarchy_meter_get_indeterminate (OMARCHY_METER (busy)));
  drive_wait (50); /* the sweep runs (or holds still under reduced motion) without trouble */
  omarchy_meter_set_indeterminate (OMARCHY_METER (busy), FALSE);
  drive_wait (20);
  drive_close (window);
}

TEST (test_spinner_box_is_square, "/feedback/spinner")
{
  GtkWidget *s = omarchy_spinner_new ();
  gtk_widget_set_halign (s, GTK_ALIGN_START);
  gtk_widget_set_valign (s, GTK_ALIGN_START);
  GtkWidget *window = drive_window (s, 100, 100);
  g_assert_cmpstr (omarchy_spinner_get_glyph (OMARCHY_SPINNER (s)), ==, "\U000F0996");
  g_assert_cmpstr (omarchy_spinner_get_size (OMARCHY_SPINNER (s)), ==, "icon");
  int w = gtk_widget_get_width (s), h = gtk_widget_get_height (s);
  g_assert_cmpint (w, ==, h);
  g_assert_cmpint (h, >=, omarchy_theme_get_font_size (omarchy_theme_get_default (), "icon"));
  omarchy_spinner_set_size (OMARCHY_SPINNER (s), "iconLarge");
  drive_wait (20);
  g_assert_cmpint (gtk_widget_get_width (s), ==, gtk_widget_get_height (s));
  g_assert_cmpint (gtk_widget_get_height (s), >, h);
  omarchy_spinner_set_pulse (OMARCHY_SPINNER (s), TRUE);
  omarchy_spinner_set_running (OMARCHY_SPINNER (s), FALSE);
  g_assert_false (omarchy_spinner_get_running (OMARCHY_SPINNER (s)));
  drive_wait (20);
  drive_close (window);
}

/* ---- EmptyState ------------------------------------------------------------------------- */

TEST (test_empty_state_action, "/feedback/empty-state")
{
  GtkWidget *e = omarchy_empty_state_new ("No networks found");
  omarchy_empty_state_set_glyph (OMARCHY_EMPTY_STATE (e), "\U000F05AA");
  omarchy_empty_state_set_hint (OMARCHY_EMPTY_STATE (e), "Turn on Wi-Fi to scan");
  GtkWidget *window = drive_window (e, 350, 150);
  GtkWidget *action = drive_find (e, OMARCHY_TYPE_BUTTON, 0);
  g_assert_nonnull (action);
  /* No action text, no button. */
  g_assert_false (gtk_widget_get_visible (action));
  omarchy_empty_state_set_action_text (OMARCHY_EMPTY_STATE (e), "Scan");
  drive_wait (20);
  g_assert_true (gtk_widget_get_visible (action));
  g_assert_true (gtk_widget_get_focusable (action));
  g_assert_true (omarchy_button_get_bordered (OMARCHY_BUTTON (action)));

  actions = 0;
  g_signal_connect (e, "action-clicked", G_CALLBACK (on_action), NULL);
  drive_click (action, -1, -1);
  g_assert_cmpint (actions, ==, 1);
  gtk_widget_grab_focus (action);
  g_assert_true (drive_key (window, NULL, GDK_KEY_Return, 0));
  g_assert_true (drive_key (window, NULL, GDK_KEY_space, 0));
  g_assert_cmpint (actions, ==, 3);

  /* cursor forces the action's hover look. */
  omarchy_empty_state_set_cursor (OMARCHY_EMPTY_STATE (e), TRUE);
  g_assert_true (omarchy_button_get_cursor (OMARCHY_BUTTON (action)));
  /* Overlay is taller (bigger glyph and title, lg gaps). */
  int panel = natural_height (e, 350);
  omarchy_empty_state_set_size (OMARCHY_EMPTY_STATE (e), "overlay");
  g_assert_cmpint (natural_height (e, 350), >, panel);
  drive_close (window);
}

/* ---- Banner ----------------------------------------------------------------------------- */

TEST (test_banner_action_and_dismiss, "/feedback/banner")
{
  GtkWidget *b = omarchy_banner_new ("Omarchy 3.2 is available");
  omarchy_banner_set_action_text (OMARCHY_BANNER (b), "Update");
  omarchy_banner_set_dismissible (OMARCHY_BANNER (b), TRUE);
  gtk_widget_set_valign (b, GTK_ALIGN_START);
  GtkWidget *window = drive_window (b, 600, 80);
  g_assert_cmpstr (omarchy_banner_get_tone (OMARCHY_BANNER (b)), ==, "info");
  GtkWidget *action = drive_find (b, OMARCHY_TYPE_BUTTON, 0);
  GtkWidget *dismiss = drive_find (b, OMARCHY_TYPE_ICON_BUTTON, 0);
  g_assert_nonnull (action);
  g_assert_nonnull (dismiss);
  g_assert_true (gtk_widget_get_visible (action) && gtk_widget_get_visible (dismiss));
  g_assert_true (gtk_widget_get_focusable (action) && gtk_widget_get_focusable (dismiss));
  g_assert_cmpstr (omarchy_icon_button_get_icon (OMARCHY_ICON_BUTTON (dismiss)), ==, "\U000F0156");

  actions = dismissals = 0;
  g_signal_connect (b, "action-clicked", G_CALLBACK (on_action), NULL);
  g_signal_connect (b, "dismissed", G_CALLBACK (on_dismissed), NULL);

  drive_click (action, -1, -1);
  g_assert_cmpint (actions, ==, 1);
  g_assert_cmpint (dismissals, ==, 0);
  gtk_widget_grab_focus (action);
  g_assert_true (drive_key (window, NULL, GDK_KEY_Return, 0));
  g_assert_cmpint (actions, ==, 2);

  drive_click (dismiss, -1, -1);
  g_assert_cmpint (dismissals, ==, 1);
  gtk_widget_grab_focus (dismiss);
  g_assert_true (drive_key (window, NULL, GDK_KEY_space, 0));
  g_assert_true (drive_key (window, NULL, GDK_KEY_KP_Enter, 0));
  g_assert_cmpint (dismissals, ==, 3);
  g_assert_cmpint (actions, ==, 2);
  /* Dismissing doesn't hide it: the app does. */
  g_assert_true (gtk_widget_get_visible (b));

  /* Other keys aren't the banner's. */
  g_assert_false (drive_key (window, NULL, GDK_KEY_a, 0));

  /* Without action or dismiss, both are gone. */
  omarchy_banner_set_action_text (OMARCHY_BANNER (b), NULL);
  omarchy_banner_set_dismissible (OMARCHY_BANNER (b), FALSE);
  g_assert_false (gtk_widget_get_visible (action));
  g_assert_false (gtk_widget_get_visible (dismiss));
  omarchy_banner_set_cursor (OMARCHY_BANNER (b), TRUE);
  g_assert_true (omarchy_button_get_cursor (OMARCHY_BUTTON (action)));
  drive_close (window);
}

TEST (test_banner_height_and_wrap, "/feedback/banner-height")
{
  OmarchyTheme *theme = omarchy_theme_get_default ();
  GtkWidget *b = omarchy_banner_new ("Backup finished");
  g_object_ref_sink (b);
  /* At least controlHeight inside sm padding and the border. */
  int one = natural_height (b, 600);
  g_assert_cmpint (one, >=, omarchy_theme_get_spacing (theme, "controlHeight") + 2 * omarchy_theme_get_spacing (theme, "sm"));
  /* Action, dismiss-only, and text-only single-line banners share the same height. */
  GtkWidget *with_action = omarchy_banner_new ("Omarchy 3.2 is available");
  omarchy_banner_set_action_text (OMARCHY_BANNER (with_action), "Update");
  g_object_ref_sink (with_action);
  GtkWidget *dismiss_only = omarchy_banner_new ("Backup finished");
  omarchy_banner_set_dismissible (OMARCHY_BANNER (dismiss_only), TRUE);
  g_object_ref_sink (dismiss_only);
  g_assert_cmpint (natural_height (with_action, 600), ==, natural_height (dismiss_only, 600));
  g_assert_cmpint (natural_height (with_action, 600), ==, one);
  g_object_unref (with_action);
  g_object_unref (dismiss_only);

  /* A long text wraps when narrow, and the banner grows. */
  omarchy_banner_set_text (OMARCHY_BANNER (b), "Could not reach the update server; check the network and try again later");
  g_assert_cmpint (natural_height (b, 200), >, one);
  /* Every tone and a custom glyph. */
  const char *tones[] = { "success", "warning", "error", "neutral", NULL };
  for (int i = 0; tones[i]; i++) {
    omarchy_banner_set_tone (OMARCHY_BANNER (b), tones[i]);
    g_assert_cmpstr (omarchy_banner_get_tone (OMARCHY_BANNER (b)), ==, tones[i]);
  }
  omarchy_banner_set_glyph (OMARCHY_BANNER (b), "\U000F0026");
  g_assert_cmpstr (omarchy_banner_get_glyph (OMARCHY_BANNER (b)), ==, "\U000F0026");
  g_object_unref (b);
}

/* ---- Toast ------------------------------------------------------------------------------ */

TEST (test_toast_action, "/feedback/toast")
{
  GtkWidget *t = omarchy_toast_new ("Export finished");
  omarchy_toast_set_glyph (OMARCHY_TOAST (t), "\U000F012C");
  omarchy_toast_set_text (OMARCHY_TOAST (t), "omarchy-demo.mp4 · 42 MB");
  gtk_widget_set_halign (t, GTK_ALIGN_START);
  gtk_widget_set_valign (t, GTK_ALIGN_START);
  GtkWidget *window = drive_window (t, 500, 140);
  /* space(360) wide by default. */
  g_assert_cmpint (gtk_widget_get_width (t), ==, omarchy_theme_space (omarchy_theme_get_default (), 360));
  g_assert_cmpfloat (omarchy_toast_get_countdown (OMARCHY_TOAST (t)), <, 0);

  GtkWidget *action = drive_find (t, OMARCHY_TYPE_BUTTON, 0);
  g_assert_nonnull (action);
  g_assert_false (gtk_widget_get_visible (action));
  omarchy_toast_set_action_text (OMARCHY_TOAST (t), "Open");
  drive_wait (20);
  g_assert_true (gtk_widget_get_visible (action));

  actions = 0;
  g_signal_connect (t, "action-clicked", G_CALLBACK (on_action), NULL);
  drive_click (action, -1, -1);
  g_assert_cmpint (actions, ==, 1);
  gtk_widget_grab_focus (action);
  g_assert_true (drive_key (window, NULL, GDK_KEY_space, 0));
  g_assert_true (drive_key (window, NULL, GDK_KEY_Return, 0));
  g_assert_cmpint (actions, ==, 3);

  /* The app owns the timer: countdown is just drawn. */
  omarchy_toast_set_countdown (OMARCHY_TOAST (t), 0.6);
  omarchy_toast_set_cursor (OMARCHY_TOAST (t), TRUE);
  drive_wait (20);
  g_assert_cmpfloat (omarchy_toast_get_countdown (OMARCHY_TOAST (t)), ==, 0.6);
  g_assert_true (omarchy_toast_get_cursor (OMARCHY_TOAST (t)));
  drive_close (window);
}
