//! Calendar: a month grid after the shell clock panel's. Month and year between previous/next
//! IconButtons, a dim caption row of weekdays, then six weeks from Monday; today ringed in the
//! accent, the selected day in the selected fill, other months' days faint, weekends dimmer.

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{App, KeyDownEvent, MouseButton, SharedString, Window, div, px};

use super::{Walk, row_fill, walk_key};
use crate::ui::{Handler, IconButton, focus_handle, keyed_state, label, tab_keys};
use crate::{Paint, Theme, color};

/// A calendar day. Plain numbers, so nothing depends on the time zone or the clock.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Date {
    pub year: i32,
    /// 1–12.
    pub month: u32,
    pub day: u32,
}

impl Date {
    pub const fn new(year: i32, month: u32, day: u32) -> Date {
        Date { year, month, day }
    }

    pub fn days_in_month(year: i32, month: u32) -> u32 {
        match month {
            1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
            4 | 6 | 9 | 11 => 30,
            _ if (year % 4 == 0 && year % 100 != 0) || year % 400 == 0 => 29,
            _ => 28,
        }
    }

    /// 0 for Monday through 6 for Sunday.
    pub fn weekday(self) -> u32 {
        // Sakamoto's method: 0 is Sunday.
        const T: [i32; 12] = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4];
        let y = if self.month < 3 { self.year - 1 } else { self.year };
        let sunday0 = (y + y / 4 - y / 100 + y / 400 + T[self.month as usize - 1] + self.day as i32).rem_euclid(7);
        ((sunday0 + 6) % 7) as u32
    }

    /// The day `n` days later (earlier when negative).
    pub fn add_days(self, n: i32) -> Date {
        let mut d = self;
        let mut n = n;
        while n > 0 {
            if d.day < Date::days_in_month(d.year, d.month) {
                d.day += 1;
            } else {
                d = Date::new(d.year, d.month, 1).add_months(1);
            }
            n -= 1;
        }
        while n < 0 {
            if d.day > 1 {
                d.day -= 1;
            } else {
                let p = Date::new(d.year, d.month, 1).add_months(-1);
                d = Date::new(p.year, p.month, Date::days_in_month(p.year, p.month));
            }
            n += 1;
        }
        d
    }

    /// The first of the month `n` months on.
    pub fn add_months(self, n: i32) -> Date {
        let m = self.year * 12 + self.month as i32 - 1 + n;
        Date::new(m.div_euclid(12), m.rem_euclid(12) as u32 + 1, 1)
    }

    /// `YYYY-MM-DD`.
    pub fn iso(self) -> String {
        format!("{}-{:02}-{:02}", self.year, self.month, self.day)
    }
}

const MONTHS: [&str; 12] =
    ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
const WEEKDAYS: [&str; 7] = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"];

/// A month grid. The app owns the month shown, today and the selected day: the steppers and
/// PageUp/PageDown ask for another month with `on_month`, a click or Return/Space on the cursor
/// day picks with `on_pick`. Arrows move the cursor a day or a week, asking for the next month
/// when it leaves this one.
#[derive(IntoElement)]
pub struct Calendar {
    id: SharedString,
    year: i32,
    month: u32,
    today: Option<Date>,
    selected: Option<Date>,
    cursor: Option<Date>,
    on_pick: Option<Handler<Date>>,
    on_month: Option<Handler<(i32, u32)>>,
}

impl Calendar {
    /// The month on screen; `month` is 1–12.
    pub fn new(year: i32, month: u32) -> Calendar {
        Calendar { id: "omarchy-calendar".into(), year, month: month.clamp(1, 12), today: None, selected: None, cursor: None, on_pick: None, on_month: None }
    }
    /// The id its cursor and focus are kept under (and its parts' debug selectors start with).
    pub fn id(mut self, id: impl Into<SharedString>) -> Self {
        self.id = id.into();
        self
    }
    /// The day to ring; the app passes it, so a still never reads the clock.
    pub fn today(mut self, d: Date) -> Self {
        self.today = Some(d);
        self
    }
    pub fn selected(mut self, d: Option<Date>) -> Self {
        self.selected = d;
        self
    }
    /// Force the hover-cursor look on one day (and start the keyboard cursor there).
    pub fn cursor(mut self, d: Option<Date>) -> Self {
        self.cursor = d;
        self
    }
    /// Called with the day clicked or chosen with Return/Space.
    pub fn on_pick(mut self, f: impl Fn(&Date, &mut Window, &mut App) + 'static) -> Self {
        self.on_pick = Some(Rc::new(f));
        self
    }
    /// Called with the `(year, month)` to show (1–12).
    pub fn on_month(mut self, f: impl Fn(&(i32, u32), &mut Window, &mut App) + 'static) -> Self {
        self.on_month = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for Calendar {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let theme = Theme::get(cx);
        let (prev_name, next_name) = (theme.string("clock.previousMonth", "Previous month"), theme.string("clock.nextMonth", "Next month"));
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let cw = t.space(36.0);
        let ch = sp.control_height as f32;
        let gap = px(sp.xxs as f32);
        let grid_w = cw * 7.0 + sp.xxs as f32 * 6.0;
        let id: gpui::ElementId = self.id.clone().into();
        let focus = focus_handle(&id, window, cx);
        let forced = self.cursor;
        let walk = keyed_state(&id, "calendar", window, cx, || Walk::new(forced));
        walk.update(cx, |w, _| w.sync(&forced));
        let (cursor, hovering) = {
            let w = walk.read(cx);
            (w.cursor, w.hovering)
        };
        let shown = forced.is_some() || hovering || focus.is_focused(window);

        // ---- Month and year between the steppers, centered in the MO and SU columns.
        let first = Date::new(self.year, self.month, 1);
        let btn_inset = ((cw - crate::ui::button::icon_button_size(&t)) / 2.0).round();
        let stepper = |glyph: &'static str, name: SharedString, delta: i32| {
            let to = first.add_months(delta);
            let which = if delta < 0 { "prev" } else { "next" };
            let mut b = IconButton::new(glyph).id(SharedString::from(format!("{}-{which}", self.id)));
            if let Some(f) = self.on_month.clone() {
                b = b.on_click(move |_, window, cx| f(&(to.year, to.month), window, cx));
            }
            let selector = format!("{}-{which}", self.id);
            div().w(px(cw)).flex().flex_shrink_0().items_center().pl(px(btn_inset)).child(
                div().id(which).debug_selector(|| selector).flex_shrink_0().child(b).tooltip(crate::ui::Tooltip::text(name)),
            )
        };
        let title = format!("{} {}", MONTHS[self.month as usize - 1], self.year).to_uppercase();
        let header = div()
            .relative()
            .flex()
            .flex_row()
            .w(px(grid_w))
            .items_center()
            .justify_between()
            .child(div().absolute().inset_0().flex().items_center().justify_center().child(label(&t, title, tk.font.body, false, t.dim())))
            .child(stepper("\u{f0141}", prev_name, -1))
            .child(stepper("\u{f0142}", next_name, 1));

        // ---- Weekdays
        let mut weekdays = div().flex().flex_row().gap(gap);
        for w in WEEKDAYS {
            weekdays = weekdays.child(
                div()
                    .flex()
                    .flex_shrink_0()
                    .w(px(cw))
                    .h(px(t.space(16.0)))
                    .items_center()
                    .justify_center()
                    .child(label(&t, w.to_uppercase(), tk.font.caption, true, t.dim())),
            );
        }

        // ---- Six weeks from the Monday on or before the 1st.
        let start = first.add_days(-(first.weekday() as i32));
        let mut grid = div().flex().flex_col().gap(gap).child(weekdays);
        let mut day = start;
        for _ in 0..6 {
            let mut week = div().flex().flex_row().gap(gap);
            for i in 0..7 {
                let in_month = day.month == self.month && day.year == self.year;
                let is_today = self.today == Some(day);
                let is_selected = self.selected == Some(day);
                let hot = shown && cursor == Some(day);
                let ink = if !in_month {
                    t.faint()
                } else if is_selected {
                    color(c.selected.text)
                } else if i >= 5 {
                    t.dim()
                } else {
                    t.fg()
                };
                let d = day;
                let (w1, w2, fo, f) = (walk.clone(), walk.clone(), focus.clone(), self.on_pick.clone());
                let selector = format!("{}-{}", self.id, d.iso());
                let mut cell = div()
                    .id(SharedString::from(d.iso()))
                    .debug_selector(|| selector)
                    .flex()
                    .flex_shrink_0()
                    .items_center()
                    .justify_center()
                    .w(px(cw))
                    .h(px(ch))
                    .rounded(t.radius())
                    .bg(row_fill(&t, is_selected, hot))
                    .cursor_pointer()
                    .child(label(&t, d.day.to_string(), tk.font.body, is_selected || is_today, ink))
                    .on_hover(move |on, _, cx| {
                        w1.update(cx, |w, cx| {
                            w.hovering = *on;
                            if *on {
                                w.cursor = Some(d);
                            }
                            cx.notify();
                        })
                    })
                    .on_mouse_down(MouseButton::Left, move |_, window, cx| {
                        fo.focus(window, cx);
                        w2.update(cx, |w, cx| {
                            w.cursor = Some(d);
                            cx.notify();
                        });
                    })
                    .on_click(move |_, window, cx| {
                        if let Some(f) = &f {
                            f(&d, window, cx);
                        }
                    });
                if is_today {
                    cell = cell.border(px(sp.hairline as f32)).border_color(t.accent());
                }
                week = week.child(cell);
                day = day.add_days(1);
            }
            grid = grid.child(week);
        }

        let today = self.today;
        let selected = self.selected;
        let (w3, pick, month) = (walk.clone(), self.on_pick.clone(), self.on_month.clone());
        div()
            .id(id)
            .flex()
            .flex_col()
            .flex_shrink_0()
            .w(px(grid_w))
            .gap(px(sp.sm as f32))
            .track_focus(&focus)
            .child(header)
            .child(grid)
            .on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                let Some(key) = walk_key(ev) else { return };
                let at = w3.read(cx).cursor;
                let days = match key {
                    "left" => -1,
                    "right" => 1,
                    "up" => -7,
                    "down" => 7,
                    "pageup" | "pagedown" => {
                        let to = first.add_months(if key == "pageup" { -1 } else { 1 });
                        if let Some(f) = &month {
                            f(&(to.year, to.month), window, cx);
                        }
                        cx.stop_propagation();
                        return;
                    }
                    "choose" => {
                        if let (Some(d), Some(f)) = (at, &pick) {
                            f(&d, window, cx);
                        }
                        cx.stop_propagation();
                        return;
                    }
                    _ => return,
                };
                let from = at.or(selected).or(today).unwrap_or(first);
                let to = from.add_days(days);
                w3.update(cx, |w, cx| {
                    w.cursor = Some(to);
                    cx.notify();
                });
                if (to.year, to.month) != (first.year, first.month)
                    && let Some(f) = &month
                {
                    f(&(to.year, to.month), window, cx);
                }
                cx.stop_propagation();
            })
    }
}

#[cfg(test)]
mod tests {
    use super::Date;

    #[test]
    fn weekdays_and_steps() {
        assert_eq!(Date::new(2026, 11, 1).weekday(), 6); // a Sunday
        assert_eq!(Date::new(2024, 2, 29).weekday(), 3); // a Thursday
        assert_eq!(Date::new(2026, 11, 1).add_days(-6), Date::new(2026, 10, 26));
        assert_eq!(Date::new(2026, 12, 31).add_days(1), Date::new(2027, 1, 1));
        assert_eq!(Date::new(2026, 1, 15).add_months(-1), Date::new(2025, 12, 1));
    }
}
