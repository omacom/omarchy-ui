//! A glyph's ink box from the font file: its `glyf` bounds beside its advance, read straight from
//! the TrueType tables (GPUI's Linux text system reports a glyph's advance as its bounds, so it
//! can't say how far a Nerd Font icon overhangs its cell).

use std::cell::RefCell;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::rc::Rc;

fn u16_at(b: &[u8], o: usize) -> Option<u16> {
    Some(u16::from_be_bytes([*b.get(o)?, *b.get(o + 1)?]))
}

fn i16_at(b: &[u8], o: usize) -> Option<i16> {
    u16_at(b, o).map(|v| v as i16)
}

fn u32_at(b: &[u8], o: usize) -> Option<u32> {
    Some(u32::from_be_bytes(b.get(o..o + 4)?.try_into().ok()?))
}

fn table<'a>(b: &'a [u8], tag: &[u8; 4]) -> Option<&'a [u8]> {
    let n = u16_at(b, 4)? as usize;
    (0..n).find_map(|i| {
        let rec = 12 + i * 16;
        if b.get(rec..rec + 4)? != tag {
            return None;
        }
        let off = u32_at(b, rec + 8)? as usize;
        let len = u32_at(b, rec + 12)? as usize;
        b.get(off..off + len)
    })
}

/// The glyph id `ch` maps to, from a format 4 or 12 Unicode subtable.
fn glyph_id(cmap: &[u8], ch: u32) -> Option<u16> {
    let n = u16_at(cmap, 2)? as usize;
    let mut best: Option<&[u8]> = None;
    for i in 0..n {
        let rec = 4 + i * 8;
        let off = u32_at(cmap, rec + 4)? as usize;
        let sub = cmap.get(off..)?;
        match u16_at(sub, 0)? {
            12 => return format12(sub, ch),
            4 if best.is_none() => best = Some(sub),
            _ => {}
        }
    }
    format4(best?, ch)
}

fn format12(sub: &[u8], ch: u32) -> Option<u16> {
    let groups = u32_at(sub, 12)? as usize;
    (0..groups).find_map(|g| {
        let o = 16 + g * 12;
        let (start, end, gid) = (u32_at(sub, o)?, u32_at(sub, o + 4)?, u32_at(sub, o + 8)?);
        (start..=end).contains(&ch).then(|| (gid + ch - start) as u16)
    })
}

fn format4(sub: &[u8], ch: u32) -> Option<u16> {
    if ch > 0xFFFF {
        return None;
    }
    let segs = u16_at(sub, 6)? as usize / 2;
    let ends = 14;
    let starts = ends + segs * 2 + 2;
    let deltas = starts + segs * 2;
    let ranges = deltas + segs * 2;
    for s in 0..segs {
        let end = u16_at(sub, ends + s * 2)? as u32;
        if ch > end {
            continue;
        }
        let start = u16_at(sub, starts + s * 2)? as u32;
        if ch < start {
            return None;
        }
        let delta = u16_at(sub, deltas + s * 2)?;
        let range = u16_at(sub, ranges + s * 2)? as usize;
        if range == 0 {
            return Some((ch as u16).wrapping_add(delta));
        }
        let at = ranges + s * 2 + range + (ch - start) as usize * 2;
        let g = u16_at(sub, at)?;
        return Some(if g == 0 { 0 } else { g.wrapping_add(delta) });
    }
    None
}

/// A glyph's TrueType ink box and advance, all in ems.
#[derive(Clone, Copy, Debug)]
pub(crate) struct GlyphInk {
    pub x_min: f32,
    pub x_max: f32,
    pub y_min: f32,
    pub y_max: f32,
    pub advance: f32,
}

fn ink_in(b: &[u8], ch: char) -> Option<GlyphInk> {
    let head = table(b, b"head")?;
    let upem = u16_at(head, 18)? as f32;
    if upem <= 0.0 {
        return None;
    }
    let long = i16_at(head, 50)? == 1;
    let gid = glyph_id(table(b, b"cmap")?, ch as u32)? as usize;
    if gid == 0 {
        return None;
    }
    let hhea = table(b, b"hhea")?;
    let metrics = u16_at(hhea, 34)? as usize;
    let hmtx = table(b, b"hmtx")?;
    let advance = u16_at(hmtx, gid.min(metrics.saturating_sub(1)) * 4)? as f32;
    let loca = table(b, b"loca")?;
    let (from, to) = if long {
        (u32_at(loca, gid * 4)? as usize, u32_at(loca, gid * 4 + 4)? as usize)
    } else {
        (u16_at(loca, gid * 2)? as usize * 2, u16_at(loca, gid * 2 + 2)? as usize * 2)
    };
    if to <= from {
        return None;
    }
    let glyf = table(b, b"glyf")?.get(from..)?;
    Some(GlyphInk {
        x_min: i16_at(glyf, 2)? as f32 / upem,
        y_min: i16_at(glyf, 4)? as f32 / upem,
        x_max: i16_at(glyf, 6)? as f32 / upem,
        y_max: i16_at(glyf, 8)? as f32 / upem,
        advance: advance / upem,
    })
}

thread_local! {
    static FONTS: RefCell<HashMap<PathBuf, Option<Rc<Vec<u8>>>>> = RefCell::new(HashMap::new());
}

fn font_bytes(path: &Path) -> Option<Rc<Vec<u8>>> {
    FONTS.with(|f| f.borrow_mut().entry(path.to_path_buf()).or_insert_with(|| std::fs::read(path).ok().map(Rc::new)).clone())
}

/// Ink box and vertical metrics of `ch` in the font file at `path`, in ems; None for a glyph the
/// font hasn't, or a font this can't read (CFF outlines, a collection).
pub(crate) fn ink(path: &Path, ch: char) -> Option<GlyphInk> {
    let bytes = font_bytes(path)?;
    ink_in(&bytes, ch)
}
