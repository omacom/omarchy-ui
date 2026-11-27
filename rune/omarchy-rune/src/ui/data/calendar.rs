//! Calendar: a month grid after the shell clock panel's. Month and year between previous/next
//! IconButtons, a dim caption row of weekdays, then six weeks from Monday; today ringed in the
//! accent, the selected day in the selected fill, other months' days faint, weekends dimmer.

use omarchy_theme::{BorderSpec, Rgba};
use rune::{Align, El, col, el, row, text};

use crate::ui::{IconButton, Look, chrome};
use crate::{Theme, color};

/// A calendar day. Plain numbers, so nothing depends on the time zone or the clock.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
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
}

const MONTHS: [&str; 12] =
    ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
const WEEKDAYS: [&str; 7] = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"];

type PickFn = Box<dyn Fn(El, Date) -> El>;
type StepFn = Box<dyn Fn(El, i32, u32) -> El>;

pub struct Calendar {
    key: Option<String>,
    year: i32,
    month: u32,
    today: Option<Date>,
    selected: Option<Date>,
    cursor: Option<Date>,
    pick: Option<PickFn>,
    step: Option<StepFn>,
}

impl Calendar {
    /// The month on screen; `month` is 1–12.
    pub fn new(year: i32, month: u32) -> Calendar {
        Calendar { year, month: month.clamp(1, 12), today: None, selected: None, cursor: None, pick: None, step: None, key: None }
    }
    /// The day to ring; the app passes it, so a still render never reads the clock.
    pub fn today(mut self, d: Date) -> Self {
        self.today = Some(d);
        self
    }
    pub fn selected(mut self, d: Option<Date>) -> Self {
        self.selected = d;
        self
    }
    /// Force the hover-cursor look on one day.
    pub fn cursor(mut self, d: Option<Date>) -> Self {
        self.cursor = d;
        self
    }
    /// Called with the day pressed.
    pub fn on_pick<S: 'static>(mut self, f: impl Fn(&mut S, Date) + Clone + 'static) -> Self {
        self.pick = Some(Box::new(move |e: El, d: Date| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, d))
        }));
        self
    }
    /// Called with the year and month (1–12) the previous/next buttons step to.
    pub fn on_month<S: 'static>(mut self, f: impl Fn(&mut S, i32, u32) + Clone + 'static) -> Self {
        self.step = Some(Box::new(move |e: El, y: i32, m: u32| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, y, m))
        }));
        self
    }

    /// Key the parts for tests and focus: `<prefix>-YYYY-MM-DD` for days, `<prefix>-prev` and `<prefix>-next` for the steppers.
    pub fn key(mut self, prefix: impl Into<String>) -> Self {
        self.key = Some(prefix.into());
        self
    }
    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let cw = t.space(36.0);
        let ch = sp.control_height as f32;
        let gap = sp.xxs as f32;
        let grid_w = cw * 7.0 + gap * 6.0;
        let fg = tk.palette.foreground;

        // ---- Month and year between the steppers.
        let first = Date::new(self.year, self.month, 1);
        let stepper = |glyph: &str, label: &str, delta: i32| {
            let mut b = IconButton::new(glyph).label(label).build(t);
            if let Some(k) = &self.key {
                b = b.key(format!("{k}-{}", if delta < 0 { "prev" } else { "next" }));
            }
            let to = first.add_months(delta);
            match &self.step {
                Some(attach) => attach(b, to.year, to.month),
                None => b,
            }
        };
        let title = format!("{} {}", MONTHS[self.month as usize - 1], self.year).to_uppercase();
        let btn_size = IconButton::size(t);
        let inset = ((cw - btn_size) / 2.0).round();
        let header = el()
            .w(grid_w)
            .h(btn_size)
            .child(
                row()
                    .pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0))
                    .items_center()
                    .child(text(title).font(t.text_style(tk.font.body)).color(t.dim()).align(Align::Center).w_full().single_line()),
            )
            .child(stepper("\u{f0141}", &tk.string("clock.previousMonth", "Previous month"), -1).absolute(inset, 0.0))
            .child(stepper("\u{f0142}", &tk.string("clock.nextMonth", "Next month"), 1).absolute(grid_w - cw + inset, 0.0));

        // ---- Weekdays
        let mut weekdays = row().gap(gap);
        for w in WEEKDAYS {
            weekdays = weekdays.child(
                row().w(cw).h(t.space(16.0)).center().child(text(w.to_uppercase()).font(t.text_style_bold(tk.font.caption)).color(t.dim())),
            );
        }

        // ---- Six weeks from the Monday on or before the 1st.
        let start = first.add_days(-(first.weekday() as i32));
        let ring = BorderSpec { color: tk.palette.accent, widths: [sp.hairline as f64; 4], gradient: None };
        let mut grid = col().gap(gap).child(weekdays).role("grid").focus_group();
        let mut day = start;
        for _ in 0..6 {
            let mut week = row().gap(gap).role("row");
            for i in 0..7 {
                let in_month = day.month == self.month && day.year == self.year;
                let is_today = self.today == Some(day);
                let is_selected = self.selected == Some(day);
                let ink = if !in_month {
                    t.faint()
                } else if is_selected {
                    color(c.selected.text)
                } else if i >= 5 {
                    t.dim()
                } else {
                    color(fg)
                };
                let style = if is_selected || is_today { t.text_style_bold(tk.font.body) } else { t.text_style(tk.font.body) };
                let edge = if is_today { ring.clone() } else { BorderSpec::none() };
                let rest = Look::new(if is_selected { c.selected.fill } else { Rgba::TRANSPARENT }, edge.clone());
                let hot = Look::new(c.hover.fill, edge);
                let base = row()
                    .size(cw, ch)
                    .center()
                    .role("gridcell")
                    .label(format!("{}-{:02}-{:02}", day.year, day.month, day.day))
                    .focusable(true)
                    .focus_selected(is_selected);
                let label = text(day.day.to_string()).font(style).color(ink);
                let mut cell = chrome(t, base, vec![label], &rest, &hot, self.cursor == Some(day), t.radius());
                if let Some(k) = &self.key {
                    cell = cell.key(format!("{k}-{}-{:02}-{:02}", day.year, day.month, day.day));
                }
                if let Some(attach) = &self.pick {
                    cell = attach(cell, day);
                }
                week = week.child(cell);
                day = day.add_days(1);
            }
            grid = grid.child(week);
        }

        col().w(grid_w).gap(sp.sm as f32).child(header).child(grid)
    }
}

#[cfg(test)]
mod tests {
    use super::Date;

    #[test]
    fn weekdays_and_steps() {
        assert_eq!(Date::new(2026, 11, 1).weekday(), 6); // a Sunday
        assert_eq!(Date::new(2026, 11, 8).weekday(), 6);
        assert_eq!(Date::new(2024, 2, 29).weekday(), 3); // a Thursday
        assert_eq!(Date::new(2026, 11, 1).add_days(-6), Date::new(2026, 10, 26));
        assert_eq!(Date::new(2026, 12, 31).add_days(1), Date::new(2027, 1, 1));
        assert_eq!(Date::new(2026, 1, 15).add_months(-1), Date::new(2025, 12, 1));
    }
}
