/* pickers: MultiSelect "Bar modules" with four of five chosen ("4 selected"); `xxl` beside it,
 * in a 280-tall box, MultiSelect "Notify for" held open with Mail and Updates. Then ColorWells
 * (accent, success with the cursor, urgent), a SwatchStrip of eight palette colors with warning
 * chosen, and a 420 × 240 box: a Card "Displays" under an open destructive Dialog.
 * (qt/gallery/sections/pickers.qml) */
#include "gallery.h"

static GtkWidget *
multi (const char *label, const char *const *options, const char *const *values)
{
  GtkWidget *m = omarchy_multi_select_new (label);
  for (const char *const *o = options; *o; o++)
    omarchy_multi_select_append (OMARCHY_MULTI_SELECT (m), *o, NULL, NULL);
  omarchy_multi_select_set_values (OMARCHY_MULTI_SELECT (m), values);
  return m;
}

static GtkWidget *
menu_row (const char *text, gboolean checked)
{
  GtkWidget *r = omarchy_menu_row_new (text);
  omarchy_menu_row_set_checked (OMARCHY_MENU_ROW (r), checked);
  return r;
}

GtkWidget *
gallery_section_pickers (void)
{
  GtkWidget *section = gallery_section ("Pickers");

  GtkWidget *row = gallery_row ("xxl");
  static const char *const bar[] = { "Clock", "Battery", "Network", "Volume", "Bluetooth", NULL };
  static const char *const bar_on[] = { "Clock", "Battery", "Network", "Volume", NULL };
  gallery_add (row, gallery_top (multi ("Bar modules", bar, bar_on)));
  static const char *const notify[] = { "Calendar", "Mail", "Messages", "Updates", "Battery", NULL };
  static const char *const notify_on[] = { "Mail", "Updates", NULL };
  GtkWidget *open = multi ("Notify for", notify, notify_on);
  /* Held open: the popup draws inside the widget, under the trigger, in this box. */
  omarchy_multi_select_set_inline_popup (OMARCHY_MULTI_SELECT (open), TRUE);
  omarchy_multi_select_open (OMARCHY_MULTI_SELECT (open));
  gtk_widget_set_valign (open, GTK_ALIGN_START);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (box), open);
  gtk_widget_set_size_request (box, -1, 280);
  gallery_add (row, gallery_top (box));
  gallery_add (section, row);

  row = gallery_row ("controlGap");
  gallery_add (row, omarchy_color_well_new_for_role ("accent"));
  GtkWidget *success = omarchy_color_well_new_for_role ("success");
  omarchy_color_well_set_cursor (OMARCHY_COLOR_WELL (success), TRUE);
  gallery_add (row, success);
  gallery_add (row, omarchy_color_well_new_for_role ("urgent"));
  gallery_add (section, row);

  GtkWidget *strip = omarchy_swatch_strip_new ();
  static const char *const roles[] = { "accent", "urgent", "warning", "success", "foreground", "dim", "faint", "muted" };
  for (guint i = 0; i < G_N_ELEMENTS (roles); i++)
    omarchy_swatch_strip_append_role (OMARCHY_SWATCH_STRIP (strip), roles[i]);
  omarchy_swatch_strip_set_current_index (OMARCHY_SWATCH_STRIP (strip), 2);
  gallery_add (section, strip);

  /* Held open over the content it would cover, so the scrim shows. */
  GtkWidget *overlay = gtk_overlay_new ();
  GtkWidget *card = omarchy_card_new ("Displays");
  GtkWidget *rows = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (rows), menu_row ("Built-in display", TRUE));
  gtk_box_append (GTK_BOX (rows), menu_row ("External monitor", FALSE));
  gtk_box_append (GTK_BOX (rows), menu_row ("Projector", FALSE));
  /* The rows run on under the dialog, past the 240-tall box, as Qt's do: let the card be
   * shorter than they are. */
  GtkWidget *clip = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (clip), GTK_POLICY_NEVER, GTK_POLICY_EXTERNAL);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (clip), rows);
  gtk_widget_set_vexpand (clip, TRUE);
  omarchy_card_append (OMARCHY_CARD (card), clip);
  gtk_overlay_set_child (GTK_OVERLAY (overlay), card);
  GtkWidget *dialog = omarchy_dialog_new ("Discard changes?", "Your edits to Tokyo Night will be lost.");
  omarchy_dialog_set_secondary_text (OMARCHY_DIALOG (dialog), "Keep editing");
  omarchy_dialog_set_primary_text (OMARCHY_DIALOG (dialog), "Discard");
  omarchy_dialog_set_destructive (OMARCHY_DIALOG (dialog), TRUE);
  omarchy_dialog_set_opened (OMARCHY_DIALOG (dialog), TRUE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), dialog);
  gtk_overlay_set_clip_overlay (GTK_OVERLAY (overlay), dialog, TRUE);
  gallery_add (section, gallery_size (overlay, 420, 240));
  return section;
}
