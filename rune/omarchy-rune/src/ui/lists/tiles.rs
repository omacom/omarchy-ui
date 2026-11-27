//! Badge, StatusDot, Avatar and TileGrid: the Qt binding's `Badge.qml`, `StatusDot.qml`,
//! `Avatar.qml` and `TileGrid.qml`.

use omarchy_theme::{BorderSpec, Rgba};
use rune::{Align, El, Image, SurfaceFit, col, el, row, text};

use super::Tone;
use crate::ui::{Attach, optical};
use crate::{Theme, border, color};

/// A count or short label in a small pill. [`Tone::Neutral`] is foreground glass (the normal
/// control fill and border); a tone fills at the selected alpha with text in the tone; `filled`
/// paints the tone solid with background text. Never narrower than tall.
pub struct Badge {
    label: String,
    tone: Tone,
    filled: bool,
}

impl Badge {
    pub fn new(label: impl Into<String>) -> Badge {
        Badge { label: label.into(), tone: Tone::Neutral, filled: false }
    }
    pub fn tone(mut self, tone: Tone) -> Self {
        self.tone = tone;
        self
    }
    pub fn filled(mut self, on: bool) -> Self {
        self.filled = on;
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let c = &tk.controls;
        let line = c.normal.border.max_width() as f32;
        let ink = self.tone.rgba(t, tk.palette.foreground);
        let neutral = self.tone == Tone::Neutral && !self.filled;
        let (fill, edge, label) = if self.filled {
            (ink, BorderSpec::none(), tk.palette.background)
        } else if neutral {
            (c.normal.fill, c.normal.border.clone(), ink)
        } else {
            (ink.with_alpha(c.selected.fill.alpha_f() as f64), BorderSpec::none(), ink)
        };
        let size = tk.font.caption;
        let h = tk.font.line_px(size) as f32 + (t.space(1.0) + line) * 2.0;
        let e = row()
            .h(h)
            .min_w(h)
            .px(t.space(6.0) + line)
            .center()
            .radius(t.radius_small())
            .fill(color(fill))
            .child(text(self.label).font(t.text_style_bold(size)).color(color(label)).single_line());
        border(e, &edge)
    }
}

/// A status dot ringed in the surface color. Dock it on a corner with [`Avatar::status`], or set
/// it inline. Always a circle.
pub struct StatusDot {
    tone: Tone,
    size: Option<f32>,
    ground: Option<Rgba>,
}

impl StatusDot {
    pub fn new(tone: Tone) -> StatusDot {
        StatusDot { tone, size: None, ground: None }
    }
    /// The dot's diameter, without the ring (default `space(8)`).
    pub fn size(mut self, s: f32) -> Self {
        self.size = Some(s);
        self
    }
    /// The surface it sits on, for the ring (default the popups background).
    pub fn ground(mut self, c: Rgba) -> Self {
        self.ground = Some(c);
        self
    }
    /// The ring's width: `max(1, space(2))`.
    pub fn ring(t: &Theme) -> f32 {
        t.space(2.0).max(1.0)
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let size = self.size.unwrap_or_else(|| t.space(8.0));
        let ring = StatusDot::ring(t);
        let outer = size + ring * 2.0;
        el().size(outer, outer)
            .radius(outer / 2.0)
            .fill(color(self.ground.unwrap_or(tk.surfaces.popups.background)))
            .child(el().absolute(ring, ring).size(size, size).radius(size / 2.0).fill(color(self.tone.rgba(t, tk.palette.foreground))))
    }
}

/// Avatar sizes, from the type scale: `iconLarge`, `displayLarge`, `display × 2`.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum AvatarSize {
    /// Fills a ListRow's leading slot.
    Small,
    #[default]
    Medium,
    Large,
}

impl AvatarSize {
    pub fn px(self, t: &Theme) -> f32 {
        let f = &t.tokens().font;
        match self {
            AvatarSize::Small => f.icon_large as f32,
            AvatarSize::Medium => f.display_large as f32,
            AvatarSize::Large => (f.display * 2) as f32,
        }
    }
}

/// A person's or an app's picture with fallbacks, tried in order: a decoded image, an icon from
/// the icon theme, a glyph, then the first letter of `name` on a foreground-glass tile. Corners
/// follow `radius` (square at Hyprland's default 0); `round` makes it a disc, for people. An icon
/// is drawn whole, fitted to the square, with no tile and no clipping: its artwork has its own
/// shape. A docked [`StatusDot`] sits on the bottom-right corner.
pub struct Avatar {
    name: String,
    image: Option<Image>,
    icon: String,
    glyph: String,
    size: f32,
    preset: Option<AvatarSize>,
    round: bool,
    status: Option<Tone>,
    ground: Option<Rgba>,
}

impl Avatar {
    pub fn new(name: impl Into<String>) -> Avatar {
        Avatar { name: name.into(), image: None, icon: String::new(), glyph: String::new(), size: 0.0, preset: Some(AvatarSize::Medium), round: false, status: None, ground: None }
    }
    /// The picture, from an `ImageCache`. Until it decodes, or if it fails, the fallbacks draw.
    pub fn image(mut self, image: Option<&Image>) -> Self {
        self.image = image.cloned();
        self
    }
    /// A freedesktop icon name (`firefox`, `org.gnome.Nautilus`), looked up in the theme's icon
    /// theme ([`Theme::icon`]) when there's no picture. A found icon holds its square while it
    /// decodes (and draws as soon as it has, without the view re-running); when no theme has it,
    /// or it fails to decode, the glyph or initial draws.
    pub fn icon(mut self, name: impl Into<String>) -> Self {
        self.icon = name.into();
        self
    }
    /// A Nerd Font glyph, when there's no picture or icon.
    pub fn glyph(mut self, g: impl Into<String>) -> Self {
        self.glyph = g.into();
        self
    }
    pub fn size(mut self, s: AvatarSize) -> Self {
        self.preset = Some(s);
        self
    }
    pub fn size_px(mut self, px: f32) -> Self {
        self.preset = None;
        self.size = px;
        self
    }
    pub fn round(mut self, on: bool) -> Self {
        self.round = on;
        self
    }
    /// Dock a status dot on the bottom-right corner.
    pub fn status(mut self, tone: Tone) -> Self {
        self.status = Some(tone);
        self
    }
    /// The surface it sits on, for the dot's ring.
    pub fn ground(mut self, c: Rgba) -> Self {
        self.ground = Some(c);
        self
    }

    /// The initial `Avatar` draws for `name`: its first character, uppercased.
    pub fn initial(name: &str) -> String {
        name.trim().chars().next().map(|c| c.to_uppercase().collect()).unwrap_or_default()
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let size = self.preset.map_or(self.size, |p| p.px(t));
        let radius = if self.round { size / 2.0 } else { t.radius().min(size / 2.0) };
        let pictured = self.image.as_ref().is_some_and(|i| i.is_ready() && i.error().is_none());
        let icon = if pictured || self.icon.is_empty() { None } else { t.icon(&self.icon, size) };
        let icon = icon.filter(|i| i.error().is_none());
        let face = if pictured {
            el().size(size, size).radius(radius).pixels_fit(self.image.as_ref().unwrap().pixels(), SurfaceFit::Fill)
        } else if let Some(icon) = icon {
            el().size(size, size).pixels_fit(icon.pixels(), SurfaceFit::Fit)
        } else {
            let tile = row().size(size, size).center().radius(radius).fill(color(tk.controls.selected.fill));
            let mark_size = (size * 0.5).round() as i64;
            if !self.glyph.is_empty() {
                tile.child(optical(&self.glyph, t.text_style(mark_size), t.fg(), None, size, size))
            } else {
                tile.child(text(Avatar::initial(&self.name)).font(t.text_style_bold(mark_size)).color(t.fg()).single_line())
            }
        };
        let mut e = el().size(size, size).role("img").label(self.name).child(face);
        if let Some(tone) = self.status {
            let dot = (size * 0.28).round().max(t.space(6.0));
            let ring = StatusDot::ring(t);
            let outer = dot + ring * 2.0;
            let mut d = StatusDot::new(tone).size(dot);
            if let Some(g) = self.ground {
                d = d.ground(g);
            }
            e = e.child(d.build(t).absolute(size - outer + ring, size - outer + ring));
        }
        e
    }
}

/// One tile of a [`TileGrid`]: a glyph or an Avatar above a label.
pub struct Tile {
    label: String,
    glyph: String,
    avatar: Option<Avatar>,
}

impl Tile {
    pub fn new(label: impl Into<String>) -> Tile {
        Tile { label: label.into(), glyph: String::new(), avatar: None }
    }
    pub fn glyph(mut self, g: impl Into<String>) -> Self {
        self.glyph = g.into();
        self
    }
    /// An Avatar (drawn at `AvatarSize::Medium`) instead of a glyph.
    pub fn avatar(mut self, a: Avatar) -> Self {
        self.avatar = Some(a);
        self
    }
}

/// A grid of tiles, each a glyph (`font.display`) or an Avatar above a label of up to two lines,
/// elided: the shell's emoji picker and app grid. The cursor tile takes the hover-cursor look
/// and the current one the selected fill, as ListRow's do.
pub struct TileGrid {
    tiles: Vec<Tile>,
    columns: usize,
    cell_width: Option<f32>,
    cursor: Option<usize>,
    current: Option<usize>,
    key: Option<String>,
    press: Option<std::rc::Rc<dyn Fn(usize) -> Attach>>,
    pointer: Option<std::rc::Rc<dyn Fn(usize) -> Attach>>,
}

impl TileGrid {
    pub fn new(tiles: impl IntoIterator<Item = Tile>) -> TileGrid {
        TileGrid { tiles: tiles.into_iter().collect(), columns: 4, cell_width: None, cursor: None, current: None, key: None, press: None, pointer: None }
    }
    pub fn columns(mut self, n: usize) -> Self {
        self.columns = n.max(1);
        self
    }
    /// A tile's width (default `space(88)`).
    pub fn cell_width(mut self, w: f32) -> Self {
        self.cell_width = Some(w);
        self
    }
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    pub fn current(mut self, index: Option<usize>) -> Self {
        self.current = index;
        self
    }
    /// Keys tiles `<key>-<index>`, for `Tx::scroll_into_view`.
    pub fn key(mut self, key: impl Into<String>) -> Self {
        self.key = Some(key.into());
        self
    }
    pub fn on_activate<S: 'static>(mut self, f: impl Fn(&mut S, usize) + Clone + 'static) -> Self {
        self.press = Some(std::rc::Rc::new(move |i| {
            let f = f.clone();
            Box::new(move |e: El| e.on_press(move |s: &mut S| f(s, i))) as Attach
        }));
        self
    }
    /// The pointer moved over tile `index`, at a window point: pass it to
    /// [`super::CursorModel::pointer_moved`].
    pub fn on_pointer_move<S: 'static>(mut self, f: impl Fn(&mut S, usize, f32, f32) + Clone + 'static) -> Self {
        self.pointer = Some(std::rc::Rc::new(move |i| {
            let f = f.clone();
            Box::new(move |e: El| e.on_pointer_move(move |s: &mut S, p| f(s, i, p.x, p.y))) as Attach
        }));
        self
    }

    /// The parts of a tile's height: its padding, the icon box, the gap and the two label lines.
    fn metrics(t: &Theme) -> (f32, f32, f32, f32) {
        let tk = t.tokens();
        let f = &tk.font;
        let pad = tk.spacing.md as f32;
        let icon = (tk.font.line_px(f.display) as f32).max(AvatarSize::Medium.px(t));
        let label = tk.font.line_px(f.body_small) as f32 * 2.0;
        (pad, icon, tk.spacing.md as f32, label)
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let c = &tk.controls;
        let f = &tk.font;
        let w = self.cell_width.unwrap_or_else(|| t.space(88.0));
        let (pad, icon_h, gap, label_h) = TileGrid::metrics(t);
        let h = pad + icon_h + gap + label_h + pad;
        let mut grid = col();
        let mut line = row();
        let count = self.tiles.len();
        for (i, tile) in self.tiles.into_iter().enumerate() {
            let cursor = self.cursor == Some(i);
            let (fill, edge) = if cursor {
                (c.hover.fill, c.hover.border.clone())
            } else if self.current == Some(i) {
                (c.selected.fill, c.selected.border.clone())
            } else {
                (Rgba::TRANSPARENT, BorderSpec::none())
            };
            let content_w = w - pad * 2.0;
            let icon_box = match tile.avatar {
                Some(a) => row().size(content_w, icon_h).center().child(a.size(AvatarSize::Medium).build(t)),
                None => optical(&tile.glyph, t.text_style(f.display), t.fg(), None, content_w, icon_h),
            };
            let mut e = col()
                .size(w, h)
                .center()
                .gap(gap)
                .radius(t.radius())
                .fill(color(fill))
                .role("option")
                .label(tile.label.clone())
                .child(icon_box)
                .child(
                    text(tile.label)
                        .font(t.text_style(f.body_small))
                        .color(t.fg())
                        .align(Align::Center)
                        .max_lines(2)
                        .w(content_w),
                );
            e = border(e, &edge);
            if let Some(k) = &self.key {
                e = e.key(format!("{k}-{i}"));
            }
            if let Some(p) = &self.pointer {
                e = p(i)(e);
            }
            if let Some(p) = &self.press {
                e = p(i)(e);
            }
            line = line.child(e);
            if (i + 1) % self.columns == 0 || i + 1 == count {
                grid = grid.child(line);
                line = row();
            }
        }
        grid
    }
}
