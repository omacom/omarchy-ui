/* omarchy-css.c: the CSS omarchy_gtk_init() installs on the display.
 *
 * Two providers:
 *  - colors: `:root { --om-*: …; --window-bg-color: …; }`, rewritten on every frame of the
 *    theme-switch fade, so every color fades (libadwaita's own variables included);
 *  - structure: everything else (rounding, sizes, the font, which state paints what), rewritten
 *    once per change, referring to the colors only through var().
 * Both sit at GTK_STYLE_PROVIDER_PRIORITY_SETTINGS: above the theme (Adwaita), below the app's
 * own CSS and the user's gtk.css. */
#include "omarchy-private.h"

#include <adwaita.h>
#include <math.h>

static GtkCssProvider *colors_provider;
static GtkCssProvider *structure_provider;
static gboolean installed;

/* ---- colors ------------------------------------------------------------------------------ */

static void
var (GString *out, const char *name, const GdkRGBA *c)
{
  char buf[64];
  omarchy_rgba_css (c, buf, sizeof buf);
  g_string_append_printf (out, "  --%s: %s;\n", name, buf);
}

static void
var_alpha (GString *out, const char *name, const GdkRGBA *c, double a)
{
  GdkRGBA x = *c;
  x.alpha *= a;
  var (out, name, &x);
}

static void
border_vars (GString *out, const char *name, const OmarchyBorder *b)
{
  char key[64];
  g_snprintf (key, sizeof key, "%s", name);
  var (out, key, &b->color);
  for (int i = 0; i < b->n_stops && b->n_stops > 1; i++) {
    g_snprintf (key, sizeof key, "%s-%d", name, i);
    var (out, key, &b->stops[i]);
  }
}

static char *
colors_css (const OmarchyTokens *t)
{
  GString *o = g_string_new (":root {\n");
  var (o, "om-fg", &t->foreground);
  var (o, "om-bg", &t->background);
  var (o, "om-accent", &t->accent);
  var (o, "om-urgent", &t->urgent);
  var (o, "om-muted", &t->muted);
  var (o, "om-success", &t->success);
  var (o, "om-warning", &t->warning);
  var (o, "om-dim", &t->text_dim);
  var (o, "om-faint", &t->text_faint);
  var (o, "om-track", &t->track);
  var (o, "om-scrim", &t->scrim);
  var (o, "om-normal-fill", &t->normal.fill);
  border_vars (o, "om-normal-border", &t->normal.border);
  var (o, "om-hover-fill", &t->hover.fill);
  border_vars (o, "om-hover-border", &t->hover.border);
  var (o, "om-hover-text", &t->hover.text);
  var (o, "om-focus-fill", &t->focus.fill);
  border_vars (o, "om-focus-border", &t->focus.border);
  var (o, "om-selected-fill", &t->selected.fill);
  border_vars (o, "om-selected-border", &t->selected.border);
  var (o, "om-selected-text", &t->selected.text);
  var (o, "om-pressed", &t->pressed);
  var (o, "om-selection", &t->selection);
  var (o, "om-popup-bg", &t->popups.background);
  var (o, "om-popup-fg", &t->popups.text);
  border_vars (o, "om-popup-border", &t->popups.border);
  var (o, "om-tooltip-bg", &t->tooltip.background);
  var (o, "om-tooltip-fg", &t->tooltip.text);
  border_vars (o, "om-tooltip-border", &t->tooltip.border);
  var (o, "om-menu-bg", &t->menu.background);
  var (o, "om-menu-fg", &t->menu.text);
  border_vars (o, "om-menu-border", &t->menu.border);
  var (o, "om-menu-selected-bg", &t->menu_selected_background);
  var (o, "om-menu-selected-fg", &t->menu_selected_text);
  var (o, "om-menu-scrim", &t->menu_scrim);
  /* Derived, as the Qt kit derives them. */
  var (o, "om-knob-off", &t->text_dim);
  GdkRGBA label = omarchy_rgba_darker (&t->foreground, 1.4);
  var (o, "om-fg-darker-14", &label);
  var (o, "om-fg-darker-12", &t->text_dim);
  var_alpha (o, "om-urgent-fill", &t->urgent, 0.22);
  var_alpha (o, "om-urgent-border", &t->urgent, 0.56);
  var_alpha (o, "om-fg-rule", &t->foreground, 0.10);
  var_alpha (o, "om-fg-38", &t->foreground, 0.38);

  /* libadwaita's palette, so stock widgets and everything derived from these follow. */
  var (o, "window-bg-color", &t->background);
  var (o, "window-fg-color", &t->foreground);
  var (o, "view-bg-color", &t->background);
  var (o, "view-fg-color", &t->foreground);
  var (o, "headerbar-bg-color", &t->background);
  var (o, "headerbar-fg-color", &t->foreground);
  var (o, "headerbar-backdrop-color", &t->background);
  var (o, "headerbar-border-color", &t->track);
  var_alpha (o, "headerbar-shade-color", &t->background, 0);
  var_alpha (o, "headerbar-darker-shade-color", &t->background, 0);
  var (o, "sidebar-bg-color", &t->background);
  var (o, "sidebar-fg-color", &t->foreground);
  var (o, "sidebar-backdrop-color", &t->background);
  var (o, "sidebar-border-color", &t->track);
  var_alpha (o, "sidebar-shade-color", &t->background, 0);
  var (o, "secondary-sidebar-bg-color", &t->background);
  var (o, "secondary-sidebar-fg-color", &t->foreground);
  var (o, "secondary-sidebar-backdrop-color", &t->background);
  var (o, "secondary-sidebar-border-color", &t->track);
  var_alpha (o, "secondary-sidebar-shade-color", &t->background, 0);
  var (o, "card-bg-color", &t->normal.fill);
  var (o, "card-fg-color", &t->foreground);
  var_alpha (o, "card-shade-color", &t->background, 0);
  var (o, "dialog-bg-color", &t->background);
  var (o, "dialog-fg-color", &t->foreground);
  var (o, "popover-bg-color", &t->popups.background);
  var (o, "popover-fg-color", &t->popups.text);
  var_alpha (o, "popover-shade-color", &t->background, 0);
  var (o, "overview-bg-color", &t->background);
  var (o, "overview-fg-color", &t->foreground);
  var (o, "thumbnail-bg-color", &t->background);
  var (o, "thumbnail-fg-color", &t->foreground);
  var_alpha (o, "shade-color", &t->background, 0);
  var (o, "scrollbar-outline-color", &t->background);
  var (o, "border-color", &t->normal.border.color);
  var (o, "accent-color", &t->accent);
  var (o, "accent-bg-color", &t->accent);
  var (o, "accent-fg-color", &t->background);
  var (o, "destructive-color", &t->urgent);
  var (o, "destructive-bg-color", &t->urgent);
  var (o, "destructive-fg-color", &t->background);
  var (o, "success-color", &t->success);
  var (o, "success-bg-color", &t->success);
  var (o, "success-fg-color", &t->background);
  var (o, "warning-color", &t->warning);
  var (o, "warning-bg-color", &t->warning);
  var (o, "warning-fg-color", &t->background);
  var (o, "error-color", &t->urgent);
  var (o, "error-bg-color", &t->urgent);
  var (o, "error-fg-color", &t->background);
  var (o, "active-toggle-bg-color", &t->selected.fill);
  var (o, "active-toggle-fg-color", &t->selected.text);
  var (o, "slider-border-color", &t->popups.background);
  g_string_append (o, "}\n");
  return g_string_free (o, FALSE);
}

/* ---- structure --------------------------------------------------------------------------- */

/* A border declaration: per-side widths in the state's color (or its gradient). */
static char *
border_decl (const OmarchyBorder *b, const char *name, const float reserve[4])
{
  float w[4];
  for (int i = 0; i < 4; i++)
    w[i] = reserve ? MAX (reserve[i], b->widths[i]) : b->widths[i];
  GString *o = g_string_new (NULL);
  g_string_append_printf (o, "border-style: solid; border-width: %gpx %gpx %gpx %gpx; ", w[0], w[1], w[2], w[3]);
  if (omarchy_border_is_none (b)) {
    g_string_append (o, "border-color: transparent; border-image: none;");
  } else if (b->n_stops > 1) {
    g_string_append_printf (o, "border-color: var(--%s); border-image: linear-gradient(%gdeg", name, b->angle + 90);
    for (int i = 0; i < b->n_stops; i++)
      g_string_append_printf (o, ", var(--%s-%d)", name, i);
    g_string_append (o, ") 1; ");
    if (reserve) /* the gradient only where this state paints */
      g_string_append_printf (o, "border-width: %gpx %gpx %gpx %gpx;", b->widths[0], b->widths[1], b->widths[2], b->widths[3]);
  } else {
    /* Sides this state doesn't paint stay reserved, transparent. */
    if (reserve && (b->widths[0] < w[0] || b->widths[1] < w[1] || b->widths[2] < w[2] || b->widths[3] < w[3])) {
      const char *side[4];
      for (int i = 0; i < 4; i++)
        side[i] = b->widths[i] > 0 ? NULL : "transparent";
      g_string_append_printf (o, "border-color: %s%s%s %s%s%s %s%s%s %s%s%s; border-image: none;",
                              side[0] ? side[0] : "var(--", side[0] ? "" : name, side[0] ? "" : ")",
                              side[1] ? side[1] : "var(--", side[1] ? "" : name, side[1] ? "" : ")",
                              side[2] ? side[2] : "var(--", side[2] ? "" : name, side[2] ? "" : ")",
                              side[3] ? side[3] : "var(--", side[3] ? "" : name, side[3] ? "" : ")");
    } else {
      g_string_append_printf (o, "border-color: var(--%s); border-image: none;", name);
    }
  }
  return g_string_free (o, FALSE);
}

static char *
structure_css (const OmarchyTokens *t)
{
  GString *o = g_string_new (NULL);
  const int R = t->radius, r = MIN (4, t->radius);
  g_autofree char *family = g_strescape (t->resolved_family ? t->resolved_family : "monospace", NULL);

  const OmarchyBorder *reserve_specs[] = { &t->normal.border, &t->hover.border, &t->focus.border, &t->selected.border };
  float reserve[4];
  omarchy_border_reserve (reserve, reserve_specs, 4);
  g_autofree char *b_normal = border_decl (&t->normal.border, "om-normal-border", reserve);
  g_autofree char *b_hover = border_decl (&t->hover.border, "om-hover-border", reserve);
  g_autofree char *b_focus = border_decl (&t->focus.border, "om-focus-border", reserve);
  OmarchyBorder sel = omarchy_border_is_none (&t->selected.border) ? t->normal.border : t->selected.border;
  g_autofree char *b_selected = border_decl (&sel, omarchy_border_is_none (&t->selected.border) ? "om-normal-border" : "om-selected-border", reserve);
  OmarchyBorder none = omarchy_border_none ();
  g_autofree char *b_none = border_decl (&none, "om-normal-border", reserve);
  OmarchyBorder popup_b = t->popups.border;
  g_autofree char *b_popup = border_decl (&popup_b, "om-popup-border", NULL);
  OmarchyBorder popup_thin = omarchy_border_with_width (&t->popups.border, MAX (1, t->normal.border.widths[0]));
  g_autofree char *b_popup_thin = border_decl (&popup_thin, "om-popup-border", NULL);
  OmarchyBorder tip = omarchy_border_with_width (&t->tooltip.border, MAX (1, omarchy_border_max (&t->normal.border)));
  g_autofree char *b_tip = border_decl (&tip, "om-tooltip-border", NULL);
  g_autofree char *b_menu = border_decl (&t->menu.border, "om-menu-border", NULL);
  g_autofree char *b_plain = border_decl (&t->normal.border, "om-normal-border", NULL);
  g_autofree char *b_dialog = border_decl (&t->normal.border, "om-menu-selected-fg", NULL);

  /* The switch (ToggleSwitch): track max(22, round(controlHeight × 0.55)) × 1.9, knob × 0.72. */
  int track_h = MAX (22, (int) lround (t->control_height * 0.55));
  int track_w = (int) lround (track_h * 1.9);
  int knob = MAX (6, (int) lround (track_h * 0.72));
  int knob_inset = MAX (1, (int) lround ((track_h - knob) / 2.0));
  int nb = (int) t->normal.border.widths[0];
  /* The slider (PanelSlider). */
  int slider_track = MAX (4, (int) lround (t->control_height * 0.11));
  int slider_knob = MAX (14, (int) lround (t->control_height * 0.38));
  int ring = MAX (1, omarchy_space (2));
  int box = omarchy_space (16);
  int body_line = omarchy_line_height (t->body);
  int entry_h = body_line + 2 * t->input_padding_y;

#define P(...) g_string_append_printf (o, __VA_ARGS__)
  /* The one font, everywhere, at the token sizes. */
  P ("* { font-family: \"%s\", monospace; -gtk-icon-shadow: none; text-shadow: none; box-shadow: none; outline: none; }\n", family);
  P ("window, popover, tooltip, dialog { font-size: %dpx; font-weight: normal; }\n", t->body);
  P ("window, window.background, dialog.background, .background { background-color: var(--om-bg); color: var(--om-fg); }\n");
  P ("window.csd, window.solid-csd, window.dialog, dialog.floating sheet, window.messagedialog { border-radius: %dpx; }\n", R);
  P (".title-1, .large-title { font-size: %dpx; font-weight: bold; }\n", t->display);
  P (".title-2 { font-size: %dpx; font-weight: bold; }\n", t->heading);
  P (".title-3 { font-size: %dpx; font-weight: bold; }\n", t->title);
  P (".title-4 { font-size: %dpx; font-weight: bold; }\n", t->subtitle);
  P (".heading, .title, windowtitle .title { font-weight: bold; font-size: %dpx; }\n", t->body);
  P (".caption, .subtitle, windowtitle .subtitle { font-size: %dpx; }\n", t->caption);
  P (".caption-heading { font-size: %dpx; font-weight: bold; }\n", t->caption);
  P (".body, .monospace { font-size: %dpx; }\n", t->body);
  P (".dim-label, .subtitle, label.dim-label, row .subtitle, .dimmed { color: var(--om-dim); opacity: 1; }\n");
  P ("label:disabled, *:disabled > label { color: var(--om-faint); }\n");
  P ("selection, *:selected selection, text selection, label selection, entry selection { background-color: var(--om-selection); color: var(--om-fg); }\n");
  P ("separator { background-color: var(--om-track); min-width: 1px; min-height: 1px; }\n");
  P ("link, .link { color: var(--om-accent); }\n");
  P ("*:focus-visible { outline: none; }\n");

  /* Header bars: the window background, a track rule, no shade. */
  P ("headerbar { background: var(--om-bg); color: var(--om-fg); border-bottom: 1px solid var(--om-track); min-height: %dpx; padding: %dpx %dpx; }\n",
     t->control_height + 2 * t->sm, t->xxs, t->sm);
  P ("headerbar.flat, headerbar:backdrop { background: var(--om-bg); }\n");
  P ("windowcontrols > button, windowcontrols > button.image-button { min-width: %dpx; min-height: %dpx; padding: 0; margin: 0 %dpx; border-radius: %dpx; }\n",
     t->control_height - 2 * (int) reserve[0], t->control_height - 2 * (int) reserve[0], t->xxs, r);
  P ("windowcontrols > button > image { background: none; box-shadow: none; padding: 0; border-radius: 0; }\n");

  /* Buttons: foreground glass. Plain (flat, in headerbars and toolbars) is borderless at rest. */
  P ("button { min-height: %dpx; min-width: %dpx; padding: %dpx %dpx; border-radius: %dpx; background: var(--om-normal-fill); color: var(--om-fg); font-weight: normal; %s }\n",
     MAX (0, t->control_height - 2 * (int) reserve[0] - 2 * t->control_padding_y), MAX (0, t->control_height - 2 * (int) reserve[3] - 2 * t->control_padding_x),
     t->control_padding_y, t->control_padding_x, R, b_normal);
  P ("button.flat, headerbar button:not(.suggested-action):not(.destructive-action), .toolbar button, button.image-button.flat, "
     "actionbar button, searchbar button, popover button.flat, .linked.flat button { background: transparent; %s }\n", b_none);
  P ("button:hover, button:focus-visible, button.flat:hover, button.flat:focus-visible, headerbar button:hover, headerbar button:focus-visible, "
     ".toolbar button:hover, actionbar button:hover { background: var(--om-hover-fill); %s }\n", b_hover);
  P ("button:checked, button.flat:checked, headerbar button:checked, togglebutton:checked { background: var(--om-selected-fill); color: var(--om-selected-text); font-weight: bold; %s }\n", b_selected);
  P ("button:checked:hover, button:checked:focus-visible { background: var(--om-hover-fill); %s }\n", b_hover);
  P ("button:active, button:checked:active, button.flat:active { background: var(--om-pressed); }\n");
  P ("button:disabled { background: transparent; color: var(--om-faint); }\n");
  P ("button.suggested-action, button.default { background: var(--om-selected-fill); color: var(--om-selected-text); %s }\n", b_selected);
  P ("button.suggested-action:hover, button.suggested-action:focus-visible { background: var(--om-hover-fill); %s }\n", b_hover);
  P ("button.destructive-action { background: transparent; color: var(--om-urgent); border-style: solid; border-color: var(--om-urgent-border); border-image: none; }\n");
  P ("button.destructive-action:hover, button.destructive-action:focus-visible { background: var(--om-urgent-fill); border-color: var(--om-urgent); }\n");
  P ("button.circular, button.pill { border-radius: %dpx; }\n", R);
  P ("button.image-button { min-width: %dpx; padding-left: %dpx; padding-right: %dpx; }\n",
     MAX (0, t->control_height - 2 * (int) reserve[0] - 2 * t->control_padding_y), t->control_padding_y, t->control_padding_y);
  P (".linked > button, .linked > button:first-child, .linked > button:last-child { border-radius: %dpx; }\n", R);
  P (".linked:not(.vertical) > button:not(:first-child), .linked:not(.vertical) > menubutton:not(:first-child) > button { margin-left: %dpx; }\n", t->md);
  P ("menubutton > button > box > arrow, dropdown arrow, combobox arrow, expander-widget > title > expander { color: var(--om-fg-darker-12); }\n");

  /* Entries: bordered at rest; hover and focus share the hover-cursor look. */
  P ("entry, spinbutton, passwordentry, searchentry, .entry { min-height: %dpx; padding: 0 %dpx; border-radius: %dpx; background: var(--om-normal-fill); color: var(--om-fg); caret-color: var(--om-fg); %s }\n",
     entry_h, t->control_padding_x, R, b_normal);
  P ("entry:hover, spinbutton:hover, searchentry:hover, passwordentry:hover { background: var(--om-hover-fill); %s }\n", b_hover);
  P ("entry:focus-within, spinbutton:focus-within, searchentry:focus-within, passwordentry:focus-within { background: var(--om-focus-fill); %s }\n", b_focus);
  P ("entry:disabled, spinbutton:disabled { color: var(--om-faint); }\n");
  P ("text > placeholder, entry > text > placeholder, entry placeholder { color: var(--om-faint); opacity: 1; }\n");
  P ("entry image, searchentry image { color: var(--om-dim); }\n");
  P ("spinbutton { padding: 0; }\n");
  P ("spinbutton > text { padding: 0 %dpx; }\n", t->control_padding_x);
  P ("spinbutton > button, spinbutton > button.image-button { border: none; border-image: none; background: transparent; min-width: %dpx; min-height: 0; padding: 0; border-radius: %dpx; margin: %dpx; }\n",
     t->control_height - 2 * nb - 2, r, nb);
  P ("spinbutton > button:hover { background: var(--om-hover-fill); } spinbutton > button:active { background: var(--om-pressed); }\n");
  P ("textview, textview > text { background: transparent; color: var(--om-fg); caret-color: var(--om-fg); }\n");
  P ("textview.view, .frame > textview { background: var(--om-normal-fill); }\n");

  /* Switch (ToggleSwitch): a sharp track unless Hyprland rounds; knob foreground darker 1.25,
   * selected text when on. The cursor ring stands outside. */
  P ("switch { min-width: %dpx; min-height: %dpx; padding: 0; border-radius: %dpx; background: var(--om-normal-fill); border: %dpx solid var(--om-normal-border); border-image: none; }\n",
     track_w - 2 * nb, track_h - 2 * nb, R > 0 ? track_h / 2 : 0, nb);
  P ("switch:checked { background: var(--om-selected-fill); border-color: %s; }\n",
     omarchy_border_is_none (&t->selected.border) ? "transparent" : "var(--om-selected-border)");
  P ("switch > slider { min-width: %dpx; min-height: %dpx; margin: %dpx; border-radius: %dpx; background: var(--om-knob-off); border: none; border-image: none; }\n",
     knob, knob, MAX (0, knob_inset - nb), R > 0 ? knob / 2 : 0);
  P ("switch:checked > slider { background: var(--om-selected-text); }\n");
  P ("switch > image { color: transparent; }\n");
  P ("switch:hover, switch:focus-visible { outline: 1px solid var(--om-hover-border); outline-offset: %dpx; }\n", omarchy_space (6) - nb);

  /* Checkboxes and radios (Checkbox, Radio): a radius_small box, accent mark when checked. */
  P ("check, radio { min-width: %dpx; min-height: %dpx; margin: 0 %dpx 0 0; padding: 0; background: var(--om-normal-fill); color: var(--om-accent); -gtk-icon-size: %dpx; "
     "border: %dpx solid var(--om-normal-border); border-image: none; border-radius: %dpx; }\n",
     box - 2 * nb, box - 2 * nb, t->row_gap, (int) lround (box * 0.85), nb, r);
  P ("radio { border-radius: 50%%; }\n");
  P ("check:checked, radio:checked, check:indeterminate { background: var(--om-selected-fill); border-color: %s; color: var(--om-accent); }\n",
     omarchy_border_is_none (&t->selected.border) ? "var(--om-normal-border)" : "var(--om-selected-border)");
  P ("check:hover, radio:hover, checkbutton:hover check, checkbutton:hover radio, checkbutton:focus-visible check, checkbutton:focus-visible radio "
     "{ background: var(--om-hover-fill); border-color: var(--om-hover-border); }\n");
  P ("checkbutton { padding: 0; } checkbutton > label { margin-left: 0; }\n");

  /* Scale (Slider): a thin track in the selected fill, foreground fill, knob ringed in the
   * surface color. */
  P ("scale { padding: %dpx %dpx; min-height: %dpx; }\n", (MAX (omarchy_space (22), slider_knob + t->md) - slider_track) / 2,
     slider_knob / 2, slider_track);
  P ("scale > trough { min-height: %dpx; min-width: %dpx; border-radius: %dpx; background: var(--om-selected-fill); border: none; outline: none; }\n",
     slider_track, slider_track, slider_track / 2);
  P ("scale > trough > highlight { background: var(--om-fg); border-radius: %dpx; border: none; min-height: %dpx; }\n", slider_track / 2, slider_track);
  P ("scale > trough > fill { background: transparent; }\n");
  P ("scale > trough > slider { min-width: %dpx; min-height: %dpx; margin: -%dpx; border-radius: 50%%; background: var(--om-fg); border: %dpx solid var(--om-popup-bg); border-image: none; }\n",
     slider_knob - 2 * ring, slider_knob - 2 * ring, (slider_knob - slider_track) / 2, ring);
  P ("scale > marks > mark > indicator { background: var(--om-popup-bg); min-width: %dpx; min-height: %dpx; }\n", MAX (1, omarchy_space (2)), slider_track + omarchy_space (4));
  P ("scale value { color: var(--om-dim); font-size: %dpx; }\n", t->body_small);

  /* Progress (ProgressBar): meterThickness on the track role, accent fill. */
  P ("progressbar > trough, levelbar > trough { min-height: %dpx; border-radius: %dpx; background: var(--om-track); border: none; padding: 0; }\n",
     t->meter_thickness, MIN (r, t->meter_thickness / 2));
  P ("progressbar > trough > progress { min-height: %dpx; border-radius: %dpx; background: var(--om-accent); border: none; }\n",
     t->meter_thickness, MIN (r, t->meter_thickness / 2));
  P ("levelbar > trough > block.filled { background: var(--om-fg); border-radius: %dpx; } levelbar > trough > block.empty { background: transparent; }\n",
     MIN (r, t->meter_thickness / 2));
  P ("levelbar > trough > block.low { background: var(--om-warning); } levelbar > trough > block.high, levelbar > trough > block.full { background: var(--om-success); }\n");
  P ("progressbar > text { color: var(--om-dim); font-size: %dpx; }\n", t->caption);

  /* Scrollbars (ScrollArea): thin, overlaid, a track groove and a normal-border thumb. */
  P ("scrollbar { background: transparent; border: none; }\n");
  P ("scrollbar > range > trough { background: var(--om-track); border-radius: %dpx; min-width: %dpx; min-height: %dpx; }\n", r,
     t->scrollbar_thickness, t->scrollbar_thickness);
  P ("scrollbar > range > trough > slider { background: var(--om-normal-border); border-radius: %dpx; min-width: %dpx; min-height: %dpx; margin: 0; border: none; }\n",
     r, t->scrollbar_thickness, MAX (t->scrollbar_thickness, omarchy_space (24)));
  P ("scrollbar > range > trough > slider:hover, scrollbar > range > trough > slider:active { background: var(--om-dim); }\n");
  P ("scrollbar.overlay-indicator:not(.hovering) > range > trough { background: transparent; }\n");
  P ("scrollbar.overlay-indicator:not(.hovering) > range > trough > slider { min-width: %dpx; min-height: %dpx; }\n", MAX (2, t->scrollbar_thickness / 2), MAX (2, t->scrollbar_thickness / 2));

  /* Lists: rows take the hover fill under the pointer or keyboard, selected rows the selected
   * fill and bold accent text, as ListRow and MenuRow draw. */
  P ("list, listview, columnview, gridview, treeview { background: transparent; color: var(--om-fg); }\n");
  P ("list > row, listview > row, gridview > child, columnview > listview > row { border-radius: %dpx; padding: %dpx %dpx; min-height: %dpx; background: transparent; }\n",
     R, 0, 0, t->popup_row_height);
  P ("list > row:hover, listview > row:hover, gridview > child:hover, list > row:focus-visible, listview > row:focus-visible, gridview > child:focus-visible "
     "{ background: var(--om-hover-fill); }\n");
  P ("list > row:selected, listview > row:selected, gridview > child:selected, columnview > listview > row:selected { background: var(--om-selected-fill); color: var(--om-selected-text); }\n");
  P ("list > row:active, listview > row:active { background: var(--om-pressed); }\n");
  P ("list.boxed-list, listview.boxed-list, .card, list.content { background: var(--om-normal-fill); color: var(--om-fg); border-radius: %dpx; %s }\n",
     R, b_plain);
  P (".card:hover.activatable { background: var(--om-hover-fill); }\n");
  P ("list.boxed-list > row, list.content > row { border-radius: 0; border-bottom: 1px solid var(--om-track); padding: %dpx %dpx; }\n", t->md, t->row_padding_x);
  P ("list.boxed-list > row:last-child, list.content > row:last-child { border-bottom: none; }\n");
  P ("row.expander list.nested { background: transparent; } row .title, row label.title { font-weight: normal; font-size: %dpx; }\n", t->body);
  P ("row.property .subtitle { color: var(--om-fg); } row.property .title { color: var(--om-dim); }\n");
  P ("preferencesgroup > box > label.heading, .preferences-group-title, stacksidebar row label { font-size: %dpx; }\n", t->caption);
  P ("preferencesgroup > box > box > label.heading { color: var(--om-dim); font-weight: bold; font-size: %dpx; }\n", t->caption);
  P ("columnview > header > button { background: transparent; border: none; border-image: none; color: var(--om-dim); font-weight: bold; font-size: %dpx; border-bottom: 1px solid var(--om-track); }\n",
     t->caption);

  /* Popovers and menus: the popups surface. */
  P ("popover > contents, popover.background > contents { background: var(--om-popup-bg); color: var(--om-popup-fg); border-radius: %dpx; padding: %dpx; %s }\n",
     R, t->hairline, b_popup_thin);
  P ("popover > arrow { background: var(--om-popup-bg); %s }\n", b_popup_thin);
  P ("popover.menu > contents { padding: %dpx; }\n", t->hairline);
  P ("popover.menu modelbutton, popover.menu menuitem, popover modelbutton { min-height: %dpx; padding: 0 %dpx; border-radius: %dpx; color: var(--om-popup-fg); background: transparent; }\n",
     t->popup_row_height, t->control_padding_x, R);
  P ("popover.menu modelbutton:hover, popover.menu modelbutton:selected, popover.menu modelbutton:focus-visible, popover modelbutton:hover, "
     "popover.menu menuitem:hover { background: var(--om-hover-fill); color: var(--om-hover-text); }\n");
  P ("popover.menu modelbutton:active { background: var(--om-pressed); }\n");
  P ("popover.menu check, popover.menu radio { background: transparent; border: none; border-image: none; color: var(--om-accent); }\n");
  P ("popover.menu separator { margin: %dpx 0; }\n", t->xxs);
  P ("popover.menu accelerator, modelbutton accelerator { color: var(--om-dim); }\n");
  P ("dropdown > popover listview > row, combobox popover row { min-height: %dpx; padding: 0 %dpx; border-radius: %dpx; }\n",
     t->popup_row_height, t->control_padding_x, R);
  P ("dropdown > popover listview > row:selected { background: transparent; color: var(--om-popup-fg); }\n");
  P ("dropdown > popover listview > row:hover, dropdown > popover listview > row:focus-visible { background: var(--om-hover-fill); }\n");
  P ("dropdown > button, combobox > button { padding: 0 %dpx 0 %dpx; }\n", t->control_gap, t->control_padding_x);
  P ("dropdown > popover check, dropdown > popover row image { color: var(--om-accent); }\n");

  /* Tooltips: the tooltip surface, bodySmall. */
  P ("tooltip, tooltip.background { background: var(--om-tooltip-bg); color: var(--om-tooltip-fg); border-radius: %dpx; padding: %dpx %dpx; font-size: %dpx; %s }\n",
     R, t->control_padding_y, t->control_padding_x, t->body_small, b_tip);
  P ("tooltip > box { padding: 0; }\n");

  /* Tabs (Tabs): dim labels, the selected one bold in the selected text with an accent
   * underline over a track rule. */
  P ("notebook > header { background: transparent; border-bottom: 1px solid var(--om-track); }\n");
  P ("notebook > header > tabs > tab, stackswitcher > button, viewswitcher button, tabbar tab { padding: %dpx %dpx; border-radius: %dpx; background: transparent; color: var(--om-dim); border: none; border-image: none; min-height: 0; }\n",
     t->control_padding_y, t->control_padding_x, r);
  P ("notebook > header > tabs > tab:hover, stackswitcher > button:hover, viewswitcher button:hover, tabbar tab:hover { background: var(--om-hover-fill); color: var(--om-fg); }\n");
  P ("notebook > header > tabs > tab:checked, stackswitcher > button:checked, viewswitcher button:checked, tabbar tab:selected "
     "{ color: var(--om-selected-text); font-weight: bold; background: transparent; border: none; border-image: none; box-shadow: inset 0 -%dpx var(--om-accent); }\n", t->xxs);
  P ("notebook > stack { background: transparent; }\n");

  /* Dialogs: the background with the accent (selected text) border, square buttons. */
  P ("dialog.alert > sheet, window.messagedialog, dialog.floating > sheet, window.dialog.message { background: var(--om-bg); border-radius: %dpx; %s }\n",
     R, b_dialog);
  P ("dialog.alert .message-area .heading, window.messagedialog .title { font-size: %dpx; font-weight: bold; }\n", t->title);
  P ("dialog.alert .response-area > button, window.messagedialog .dialog-action-area > button { border-radius: 0; font-size: %dpx; }\n", t->caption);

  /* Sidebars, toolbars, banners, toasts. */
  P ("toolbarview > .top-bar, toolbarview > .bottom-bar, actionbar > revealer > box { background: var(--om-bg); }\n");
  P ("toolbarview > .top-bar.raised, toolbarview > .bottom-bar.raised { box-shadow: none; border-bottom: 1px solid var(--om-track); }\n");
  P ("banner > revealer > widget { background: var(--om-normal-fill); color: var(--om-fg); }\n");
  P ("toast { background: var(--om-popup-bg); color: var(--om-popup-fg); border-radius: %dpx; %s }\n", R, b_popup_thin);
  P (".navigation-sidebar > row, stacksidebar row { min-height: %dpx; padding: 0 %dpx; border-radius: %dpx; }\n",
     t->popup_row_height, t->control_padding_x, R);
  P (".navigation-sidebar > row:selected, stacksidebar row:selected { background: var(--om-selected-fill); color: var(--om-selected-text); font-weight: bold; }\n");
  P ("frame > border, .frame { border: 1px solid var(--om-normal-border); border-radius: %dpx; }\n", R);
  P ("expander-widget > title > expander { color: var(--om-dim); }\n");
  P ("calendar { color: var(--om-fg); border: 1px solid var(--om-normal-border); border-radius: %dpx; } calendar > grid > label.today { color: var(--om-accent); font-weight: bold; box-shadow: none; } "
     "calendar > grid > label:selected { background: var(--om-selected-fill); color: var(--om-selected-text); border-radius: %dpx; }\n", R, r);
  P ("spinner { color: var(--om-fg); }\n");
  P ("statuspage > scrolledwindow > viewport > box > clamp > box > .title { font-size: %dpx; } statuspage .description { color: var(--om-dim); }\n", t->heading);
  P ("avatar { background: var(--om-selected-fill); color: var(--om-fg); }\n");
  P ("paned > separator { background: var(--om-track); min-width: 1px; min-height: 1px; }\n");
  P ("label.osd, .osd { background: var(--om-popup-bg); color: var(--om-popup-fg); }\n");
  P ("menubar > item:hover, menubar > item:selected { background: var(--om-hover-fill); border-radius: %dpx; }\n", R);

  /* The kit's own widgets that wrap a stock text field. */
  P ("omarchy-text-field > text, omarchy-number-field text, omarchy-text-area textview, omarchy-text-area textview > text, omarchy-password-field > text, omarchy-search-field > text "
     "{ background: transparent; color: var(--om-fg); caret-color: var(--om-fg); font-size: %dpx; padding: 0; border: none; border-image: none; min-height: 0; }\n", t->body);
  P ("omarchy-text-field > text > placeholder, omarchy-password-field > text > placeholder, omarchy-search-field > text > placeholder, omarchy-text-area placeholder { color: var(--om-faint); }\n");
  P ("omarchy-text-field selection, omarchy-number-field selection, omarchy-text-area selection, omarchy-password-field selection, omarchy-search-field selection "
     "{ background-color: var(--om-selection); color: var(--om-fg); }\n");
  P ("omarchy-number-field text { color: var(--om-fg); }\n");
  P ("omarchy-popup, popover.omarchy-popup > contents { background: transparent; border: none; border-image: none; padding: 0; border-radius: 0; }\n");
  P ("popover.omarchy-popup { background: transparent; } popover.omarchy-popup > arrow { background: transparent; border: none; border-image: none; min-height: 0; min-width: 0; }\n");
#undef P
  (void) b_popup;
  (void) b_menu;
  return g_string_free (o, FALSE);
}

/* ---- install ----------------------------------------------------------------------------- */

static void
parse_error (GtkCssProvider *p, GtkCssSection *section, GError *error, gpointer data)
{
  if (g_getenv ("OMARCHY_GTK_DEBUG_CSS")) {
    g_autofree char *where = gtk_css_section_to_string (section);
    g_printerr ("omarchy-gtk css: %s: %s\n", where, error->message);
  }
}

void
omarchy_css_update_colors (const OmarchyTokens *t)
{
  if (!installed)
    return;
  g_autofree char *css = colors_css (t);
  gtk_css_provider_load_from_string (colors_provider, css);
}

void
omarchy_css_update_structure (const OmarchyTokens *t)
{
  if (!installed)
    return;
  g_autofree char *css = structure_css (t);
  if (g_getenv ("OMARCHY_GTK_DUMP_CSS"))
    g_print ("%s\n", css);
  gtk_css_provider_load_from_string (structure_provider, css);
  /* libadwaita's light and dark stylesheets, by the theme's mode; colors come from ours. */
  if (adw_is_initialized ())
    adw_style_manager_set_color_scheme (adw_style_manager_get_default (),
                                        t->dark ? ADW_COLOR_SCHEME_FORCE_DARK : ADW_COLOR_SCHEME_FORCE_LIGHT);
  GtkSettings *settings = gtk_settings_get_default ();
  if (settings)
    g_object_set (settings, "gtk-enable-animations", !t->reduce_motion, NULL);
}

void
omarchy_css_install (GdkDisplay *display)
{
  if (installed)
    return;
  colors_provider = gtk_css_provider_new ();
  structure_provider = gtk_css_provider_new ();
  g_signal_connect (colors_provider, "parsing-error", G_CALLBACK (parse_error), NULL);
  g_signal_connect (structure_provider, "parsing-error", G_CALLBACK (parse_error), NULL);
  installed = TRUE;
  omarchy_css_update_structure (omarchy_tokens ());
  omarchy_css_update_colors (omarchy_paint ());
  gtk_style_context_add_provider_for_display (display, GTK_STYLE_PROVIDER (structure_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
  gtk_style_context_add_provider_for_display (display, GTK_STYLE_PROVIDER (colors_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
}
