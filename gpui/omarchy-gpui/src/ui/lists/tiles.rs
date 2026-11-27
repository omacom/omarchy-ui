//! Badge, StatusDot, Avatar and TileGrid: the Qt binding's `Badge.qml`, `StatusDot.qml`,
//! `Avatar.qml` and `TileGrid.qml`.

use std::cell::RefCell;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::Arc;

use gpui::prelude::*;
use gpui::{
    AnyElement, App, ClickEvent, DevicePixels, ElementId, Image, ImageFormat, MouseMoveEvent, ObjectFit, Pixels, Point, RenderImage,
    SharedString, SvgSize, Window, div, img, px,
};
use omarchy_theme::{BorderSpec, Rgba, Tokens};

use super::Tone;
use crate::ui::{Handler, border, id_for, label};
use crate::{Paint, color};

/// A count or short label in a small pill. [`Tone::Neutral`] is foreground glass (the normal
/// control fill and border); a tone fills at the selected alpha with text in the tone; `filled`
/// paints the tone solid with background text. Never narrower than tall.
#[derive(IntoElement)]
pub struct Badge {
    label: SharedString,
    tone: Tone,
    filled: bool,
}

impl Badge {
    pub fn new(label: impl Into<SharedString>) -> Badge {
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
}

impl RenderOnce for Badge {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let c = &tk.controls;
        let line = c.normal.border.max_width() as f32;
        let ink = self.tone.rgba(tk, tk.palette.foreground);
        let neutral = self.tone == Tone::Neutral && !self.filled;
        let (fill, edge, text) = if self.filled {
            (ink, BorderSpec::none(), tk.palette.background)
        } else if neutral {
            (c.normal.fill, c.normal.border.clone(), ink)
        } else {
            (ink.with_alpha(c.selected.fill.alpha_f() as f64), BorderSpec::none(), ink)
        };
        let size = tk.font.caption;
        let h = tk.font.line_px(size) as f32 + (t.space(1.0) + line) * 2.0;
        // The neutral border is reserved for every tone, so badges line up.
        let pad = t.space(6.0) + line - if neutral { line } else { 0.0 };
        let e = div()
            .flex()
            .flex_shrink_0()
            .items_center()
            .justify_center()
            .h(px(h))
            .min_w(px(h))
            .px(px(pad))
            .rounded(t.radius_small())
            .bg(color(fill))
            .child(label(&t, self.label, size, true, color(text)));
        border(e, &edge)
    }
}

/// A status dot ringed in the surface color. Dock it on a corner with [`Avatar::status`], or set
/// it inline. Always a circle.
#[derive(IntoElement)]
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
    pub fn ring(t: &Tokens) -> f32 {
        t.spacing.space(2.0).max(1.0) as f32
    }
}

impl RenderOnce for StatusDot {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let size = self.size.unwrap_or_else(|| t.space(8.0));
        let ring = StatusDot::ring(tk);
        let outer = size + ring * 2.0;
        div()
            .relative()
            .flex_shrink_0()
            .size(px(outer))
            .rounded(px(outer / 2.0))
            .bg(color(self.ground.unwrap_or(tk.surfaces.popups.background)))
            .child(
                div()
                    .absolute()
                    .left(px(ring))
                    .top(px(ring))
                    .size(px(size))
                    .rounded(px(size / 2.0))
                    .bg(color(self.tone.rgba(tk, tk.palette.foreground))),
            )
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
    pub fn px(self, t: &Tokens) -> f32 {
        let f = &t.font;
        match self {
            AvatarSize::Small => f.icon_large as f32,
            AvatarSize::Medium => f.display_large as f32,
            AvatarSize::Large => (f.display * 2) as f32,
        }
    }
}

thread_local! {
    /// Decoded pictures and icons, by file and device size; `None` for a file that didn't load.
    static IMAGES: RefCell<HashMap<(PathBuf, u32), Option<Arc<RenderImage>>>> = RefCell::new(HashMap::new());
}

/// `path` decoded at `px` device pixels (SVGs rasterized at that width, so they stay crisp),
/// on the spot and cached: stills are one frame, so nothing waits on a load.
fn load(path: &Path, px: u32, cx: &App) -> Option<Arc<RenderImage>> {
    let key = (path.to_path_buf(), px);
    if let Some(hit) = IMAGES.with(|m| m.borrow().get(&key).cloned()) {
        return hit;
    }
    let decode = || -> Option<Arc<RenderImage>> {
        let bytes = std::fs::read(path).ok()?;
        let ext = path.extension().and_then(|e| e.to_str()).unwrap_or("").to_ascii_lowercase();
        if ext == "svg" {
            let r = cx.svg_renderer();
            let tree = r.parse_svg(&bytes).ok()?;
            return r.render_parsed(&tree, SvgSize::Size(gpui::size(DevicePixels(px as i32), DevicePixels(px as i32)))).ok();
        }
        let format = match ext.as_str() {
            "png" => ImageFormat::Png,
            "jpg" | "jpeg" => ImageFormat::Jpeg,
            "webp" => ImageFormat::Webp,
            "gif" => ImageFormat::Gif,
            "bmp" => ImageFormat::Bmp,
            _ => ImageFormat::Png,
        };
        Image::from_bytes(format, bytes).to_image_data(cx.svg_renderer()).ok()
    };
    let out = decode();
    IMAGES.with(|m| m.borrow_mut().insert(key, out.clone()));
    out
}

/// A person's or an app's picture with fallbacks, tried in order: a picture file, an icon from
/// the icon theme, a glyph, then the first letter of `name` on a foreground-glass tile. Corners
/// follow `radius` (square at Hyprland's default 0); `round` makes it a disc, for people. An icon
/// is drawn whole, fitted to the square, with no tile and no clipping: its artwork has its own
/// shape. A docked [`StatusDot`] sits on the bottom-right corner.
#[derive(IntoElement)]
pub struct Avatar {
    name: SharedString,
    source: Option<PathBuf>,
    icon: SharedString,
    glyph: SharedString,
    size: f32,
    preset: Option<AvatarSize>,
    round: bool,
    status: Option<Tone>,
    ground: Option<Rgba>,
}

impl Avatar {
    pub fn new(name: impl Into<SharedString>) -> Avatar {
        Avatar {
            name: name.into(),
            source: None,
            icon: SharedString::default(),
            glyph: SharedString::default(),
            size: 0.0,
            preset: Some(AvatarSize::Medium),
            round: false,
            status: None,
            ground: None,
        }
    }
    /// The picture, a file (PNG, JPEG, WebP, SVG...). One that doesn't load gives way to the
    /// fallbacks.
    pub fn source(mut self, path: impl Into<PathBuf>) -> Self {
        self.source = Some(path.into());
        self
    }
    /// A freedesktop icon name (`firefox`, `org.gnome.Nautilus`), looked up in the theme's icon
    /// theme ([`crate::Theme::icon`]) when there's no picture; when no theme has it, the glyph or
    /// initial draws.
    pub fn icon(mut self, name: impl Into<SharedString>) -> Self {
        self.icon = name.into();
        self
    }
    /// A Nerd Font glyph, when there's no picture or icon.
    pub fn glyph(mut self, g: impl Into<SharedString>) -> Self {
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
}

impl RenderOnce for Avatar {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens().clone();
        let size = self.preset.map_or(self.size, |p| p.px(&tk));
        let device = (size * window.scale_factor()).ceil().max(1.0) as u32;
        let radius = if self.round { size / 2.0 } else { (tk.radius as f32).min(size / 2.0) };
        let picture = self.source.as_deref().and_then(|p| load(p, device, cx));
        let icon = if picture.is_some() || self.icon.is_empty() {
            None
        } else {
            crate::Theme::get(cx).icon(&self.icon, device).and_then(|p| load(&p, device, cx))
        };
        let face: AnyElement = if let Some(pic) = picture {
            img(pic).size(px(size)).rounded(px(radius)).object_fit(ObjectFit::Cover).into_any_element()
        } else if let Some(icon) = icon {
            img(icon).size(px(size)).object_fit(ObjectFit::Contain).into_any_element()
        } else {
            let tile = div()
                .flex()
                .items_center()
                .justify_center()
                .size(px(size))
                .rounded(px(radius))
                .bg(color(tk.controls.selected.fill));
            let mark = (size * 0.5).round() as i64;
            if !self.glyph.is_empty() {
                tile.child(crate::ui::navigation::optical(window, cx, &t, &self.glyph, mark, t.fg(), None, size, size)).into_any_element()
            } else {
                tile.child(label(&t, Avatar::initial(&self.name), mark, true, t.fg())).into_any_element()
            }
        };
        let mut e = div().relative().flex_shrink_0().size(px(size)).child(face);
        if let Some(tone) = self.status {
            let dot = (size * 0.28).round().max(t.space(6.0));
            let ring = StatusDot::ring(&tk);
            let outer = dot + ring * 2.0;
            let mut d = StatusDot::new(tone).size(dot);
            if let Some(g) = self.ground {
                d = d.ground(g);
            }
            e = e.child(div().absolute().left(px(size - outer + ring)).top(px(size - outer + ring)).child(d));
        }
        e
    }
}

/// One tile of a [`TileGrid`]: a glyph or an Avatar above a label.
pub struct Tile {
    label: SharedString,
    glyph: SharedString,
    avatar: Option<Avatar>,
}

impl Tile {
    pub fn new(label: impl Into<SharedString>) -> Tile {
        Tile { label: label.into(), glyph: SharedString::default(), avatar: None }
    }
    pub fn glyph(mut self, g: impl Into<SharedString>) -> Self {
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
/// and the current one the selected fill, as ListRow's do. Drive it with a
/// [`super::CursorModel`] with `columns` set.
#[derive(IntoElement)]
pub struct TileGrid {
    id: ElementId,
    tiles: Vec<Tile>,
    columns: usize,
    cell_width: Option<f32>,
    cursor: Option<usize>,
    current: Option<usize>,
    on_activate: Option<Handler<usize>>,
    on_pointer_move: Option<Rc<dyn Fn(&(usize, Point<Pixels>), &mut Window, &mut App)>>,
}

impl TileGrid {
    pub fn new(tiles: impl IntoIterator<Item = Tile>) -> TileGrid {
        TileGrid {
            id: id_for("tile-grid", ""),
            tiles: tiles.into_iter().collect(),
            columns: 4,
            cell_width: None,
            cursor: None,
            current: None,
            on_activate: None,
            on_pointer_move: None,
        }
    }
    /// The element id; tiles are `(id, index)` under it.
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
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
    /// A tile was clicked, with its index.
    pub fn on_activate(mut self, f: impl Fn(&usize, &mut Window, &mut App) + 'static) -> Self {
        self.on_activate = Some(Rc::new(f));
        self
    }
    /// The pointer moved over tile `index`, at a window point: hand it to
    /// [`super::CursorModel::pointer_moved`].
    pub fn on_pointer_move(mut self, f: impl Fn(&(usize, Point<Pixels>), &mut Window, &mut App) + 'static) -> Self {
        self.on_pointer_move = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for TileGrid {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let c = &tk.controls;
        let f = &tk.font;
        let w = self.cell_width.unwrap_or_else(|| t.space(88.0));
        let pad = tk.spacing.md as f32;
        let inner_w = (w - pad * 2.0).max(0.0);
        let icon_h = (tk.font.line_px(f.display) as f32).max(AvatarSize::Medium.px(tk));
        let gap = tk.spacing.md as f32;
        let label_h = tk.font.line_px(f.body_small) as f32 * 2.0;
        let h = pad + icon_h + gap + label_h + pad;
        let mut grid = div().flex().flex_col().flex_shrink_0();
        let mut line = div().flex().flex_row();
        let count = self.tiles.len();
        for (i, tile) in self.tiles.into_iter().enumerate() {
            let (fill, edge) = if self.cursor == Some(i) {
                (c.hover.fill, c.hover.border.clone())
            } else if self.current == Some(i) {
                (c.selected.fill, c.selected.border.clone())
            } else {
                (Rgba::TRANSPARENT, BorderSpec::none())
            };
            let icon: AnyElement = match tile.avatar {
                Some(a) => a.size(AvatarSize::Medium).into_any_element(),
                None => crate::ui::navigation::optical(window, cx, &t, &tile.glyph, f.display, t.fg(), None, inner_w, icon_h).into_any_element(),
            };
            let words = div()
                .w(px(inner_w))
                .font_family(t.family())
                .text_size(px(f.body_small as f32))
                .line_height(t.line(f.body_small))
                .text_color(t.fg())
                .text_center()
                .line_clamp(2)
                .text_ellipsis()
                .child(tile.label);
            let content = div()
                .flex()
                .flex_col()
                .items_center()
                .w(px(inner_w))
                .gap(px(gap))
                .child(div().flex().items_center().justify_center().w(px(inner_w)).h(px(icon_h)).child(icon))
                .child(words);
            let tile_id = ElementId::NamedInteger(format!("{}", self.id).into(), i as u64);
            let selector = tile_id.clone();
            let mut e = div()
                .id(tile_id)
                .debug_selector(move || selector.to_string())
                .relative()
                .flex()
                .flex_col()
                .flex_shrink_0()
                .items_center()
                .justify_center()
                .w(px(w))
                .h(px(h))
                .rounded(t.radius())
                .bg(color(fill))
                .child(content);
            if !edge.is_none() {
                e = e.child(border(div().absolute().top_0().left_0().size_full().rounded(t.radius()), &edge));
            }
            if let Some(p) = self.on_pointer_move.clone() {
                e = e.on_mouse_move(move |ev: &MouseMoveEvent, w, cx| p(&(i, ev.position), w, cx));
            }
            if let Some(a) = self.on_activate.clone() {
                e = e.on_click(move |_: &ClickEvent, w, cx| a(&i, w, cx));
            }
            line = line.child(e);
            if (i + 1) % self.columns == 0 || i + 1 == count {
                grid = grid.child(line);
                line = div().flex().flex_row();
            }
        }
        grid
    }
}
