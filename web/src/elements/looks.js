// The [controls] states (spec/tokens.md) as CSS, shared by every element: an element decides its
// state in JS the way the Qt kit does (pressed > focus > hover-cursor > selected > normal), sets
// data-fill / data-border on a `.box`, and these rules paint the tokens. Nothing here picks a color:
// fills and borders are the core's, or, for an element with its own ink (`.ink`, --ink), the state's
// alpha over that ink, as Qt's Style.hoverFillFor(foreground) does.

const STATES = ["normal", "hover", "focus", "selected", "pressed"];
const BORDERED = ["normal", "hover", "focus", "selected"];
const v = (state, k) => `var(--omarchy-control-${state}-${k})`;

/** `space(n)` of spec/tokens.md in CSS: max(1, round(n × scale)) px. */
export const space = (n) => `max(1px, round(calc(${n}px * var(--omarchy-space-scale)), 1px))`;

/** Qt's QColor::darker(f × 100) of a CSS color: HSV value ÷ f, which is each channel ÷ f. */
export const darker = (color, f) => `rgb(from ${color} calc(r / ${f}) calc(g / ${f}) calc(b / ${f}) / alpha)`;

/** A color at an alpha (Qt's Util.alpha: the color's own alpha times `a`). */
export const alpha = (color, a) => `rgb(from ${color} r g b / calc(alpha * ${a}))`;

/** The widest border a set of states can paint, per side (`--rt`, `--rr`, `--rb`, `--rl`), so a state
 *  change never relayouts. */
export function reserve(states) {
  const side = (s) => (states.length ? `max(${states.map((st) => v(st, `border-${s}`)).join(", ")})` : "0px");
  return `--rt: ${side("top")}; --rr: ${side("right")}; --rb: ${side("bottom")}; --rl: ${side("left")};`;
}

export const LOOK_CSS = `
.box {
  --fill: transparent; --bc: transparent; --bt: 0px; --br: 0px; --bb: 0px; --bl: 0px;
  border-style: solid;
  border-color: var(--bc);
  border-width: var(--bt) var(--br) var(--bb) var(--bl);
  background-color: var(--fill);
  background-clip: padding-box;
  border-radius: var(--omarchy-radius);
  transition: background-color var(--omarchy-motion-fast), border-color var(--omarchy-motion-fast);
}
${STATES.map((s) => `.box[data-fill="${s}"] { --fill: ${v(s, "fill")}; }`).join("\n")}
.box[data-fill="none"] { --fill: transparent; }
${BORDERED.map((s) => `.box[data-border="${s}"] { --bc: ${v(s, "border")}; --bt: ${v(s, "border-top")}; --br: ${v(s, "border-right")}; --bb: ${v(s, "border-bottom")}; --bl: ${v(s, "border-left")}; }`).join("\n")}
.box[data-border="none"] { --bc: transparent; --bt: 0px; --br: 0px; --bb: 0px; --bl: 0px; }
${STATES.map((s) => `.box.ink[data-fill="${s}"] { --fill: ${alpha("var(--ink)", v(s, "fill-alpha"))}; }`).join("\n")}
${BORDERED.map((s) => `.box.ink[data-border="${s}"] { --bc: ${alpha("var(--ink)", v(s, "border-alpha"))}; }`).join("\n")}
`;

/**
 * Track the pointer and focus on `el` (the host) and call `el.update()` on a change: `el._hot`
 * (pointer over it), `el._pressed` (primary button down), `el._focused` (keyboard focus, which
 * looks like hover). `target` is where the pointer counts (default the host).
 */
export function trackPointer(el, target = el, { focus = true } = {}) {
  const set = (k, val) => {
    if (el[k] !== val) {
      el[k] = val;
      el.update();
    }
  };
  target.addEventListener("pointerenter", () => set("_hot", true));
  target.addEventListener("pointerleave", () => {
    set("_hot", false);
    set("_pressed", false);
  });
  target.addEventListener("pointerdown", (e) => {
    if (e.button === 0) set("_pressed", true);
  });
  const up = () => set("_pressed", false);
  target.addEventListener("pointerup", up);
  target.addEventListener("pointercancel", up);
  if (!focus) return;
  el.addEventListener("focus", () => set("_focused", el.matches(":focus-visible")));
  el.addEventListener("blur", () => set("_focused", false));
}

/** Enter and Space on a focused host click it, like a native button. */
export function clickOnKeys(el, keys = ["Enter", " "]) {
  el.addEventListener("keydown", (e) => {
    if (e.target !== el || !keys.includes(e.key) || e.repeat) return;
    e.preventDefault();
    el.click();
  });
}

const glyphCanvas = typeof document !== "undefined" ? document.createElement("canvas").getContext("2d") : null;
const inkCache = new Map();

/**
 * Glyphs centered by ink (Qt's OpticalGlyph): horizontally on the outline's middle, vertically on
 * the line box rounded to whole points. A `.og` box holds one `<span>`; inkCenter(span) moves it.
 */
export const OG_CSS = `
.og { position: relative; flex: none; }
.og > span {
  position: absolute; left: 50%; white-space: pre;
  font-size: var(--f); line-height: var(--l, normal);
  top: round(calc((100% - var(--l, 1em)) / 2), 1px);
}`;

/** `--f` / `--l` for a font token (`icon`, `caption`, `icon-large`, …). */
export const ogFont = (token) => `--f: var(--omarchy-font-${token}); --l: var(--omarchy-line-${token}, normal);`;

/** Shift `span` (left: 50%) so its ink, not its advance, is centered (Qt OpticalGlyph). */
export function inkCenter(span) {
  if (!glyphCanvas || !span.isConnected) return;
  const text = span.textContent;
  if (!text) {
    span.style.marginLeft = "";
    return;
  }
  const cs = getComputedStyle(span);
  const size = parseFloat(cs.fontSize) || 12;
  const key = `${cs.fontWeight}\n${size}\n${cs.fontFamily}\n${text}`;
  let off = inkCache.get(key);
  if (off === undefined) {
    // Measured at 100× as Qt's OpticalGlyph does, so hinting doesn't round the outline.
    glyphCanvas.font = `${cs.fontWeight} ${size * 100}px ${cs.fontFamily}`;
    const m = glyphCanvas.measureText(text);
    off = (m.actualBoundingBoxRight - m.actualBoundingBoxLeft) / 200;
    inkCache.set(key, off);
  }
  span.style.marginLeft = `${-off}px`;
}

/**
 * Size a span holding an icon glyph as Qt sizes its Text: the advance, or the ink's right edge when
 * the glyph overhangs it (Nerd Font icons do, by a third), so a label after it and a centered icon
 * sit where Qt puts them. Chromium alone lays out by the advance.
 */
export function fitGlyph(span) {
  if (!glyphCanvas || !span.isConnected) return;
  span.style.width = "";
  const text = span.textContent;
  if (!text) return;
  const cs = getComputedStyle(span);
  glyphCanvas.font = `${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`;
  const m = glyphCanvas.measureText(text);
  const w = Math.max(m.width, m.actualBoundingBoxRight);
  if (w > m.width + 0.5) span.style.width = `${w}px`;
}

/** Ceil a text span's natural width to whole pixels (avoids subpixel drift in Button / ButtonGroup). */
export function ceilText(span) {
  if (!glyphCanvas || !span.isConnected) return;
  span.style.width = "";
  const text = span.textContent;
  if (!text) return;
  const cs = getComputedStyle(span);
  glyphCanvas.font = `${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`;
  const m = glyphCanvas.measureText(text);
  const w = Math.max(m.width, m.actualBoundingBoxRight);
  if (w > 0) span.style.width = `${Math.ceil(w - 1e-6)}px`;
}

/**
 * Optically center a single-glyph span horizontally by its tight ink bounds (matching Qt's
 * PanelActionButton / OpticalGlyph): vertically stays on the line box, horizontally offsets by
 * whole pixels so the ink center lands on the span box center.
 */
export function centerGlyph(span) {
  if (!glyphCanvas || !span.isConnected) return;
  span.style.width = "";
  span.style.position = "";
  span.style.left = "";
  span.style.top = "";
  const text = span.textContent;
  if (!text) return;
  const cs = getComputedStyle(span);
  const size = parseFloat(cs.fontSize) || 12;
  glyphCanvas.font = `${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`;
  const m1 = glyphCanvas.measureText(text);
  const w = Math.max(m1.width, m1.actualBoundingBoxRight);
  const boxW = w > m1.width + 0.5 ? w : m1.width;
  if (w > m1.width + 0.5) span.style.width = `${w}px`;
  glyphCanvas.font = `${cs.fontWeight} ${size * 100}px ${cs.fontFamily}`;
  const m = glyphCanvas.measureText(text);
  const inkW = (m.actualBoundingBoxLeft + m.actualBoundingBoxRight) / 100;
  if (!(inkW > 0)) return;
  const inkCenterX = (m.actualBoundingBoxRight - m.actualBoundingBoxLeft) / 200;
  const dx = Math.round(boxW / 2 - inkCenterX);
  if (dx !== 0) {
    span.style.position = "relative";
    span.style.left = `${dx}px`;
  }
}
