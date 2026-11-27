// Editing (spec/components/editing.md): Timeline, Scrubber, TrimHandle, CropFrame, DragHandle,
// DropIndicator, ColorPicker, drawn as qt/qml/Omarchy/Ui draws them (Timeline.qml, Scrubber.qml,
// TrimHandle.qml, CropFrame.qml, DragHandle.qml, DropIndicator.qml, ColorPicker.qml).
//
// Handles and the playhead are accent; clips, lanes and swatches foreground glass; small parts round
// by min(4, radius). Geometry that Qt computes in whole pixels (positions on a time axis, grips,
// knobs) is computed here the same way, from the core's tokens.

import { OmarchyElement, esc } from "./base.js";
import { space } from "./looks.js";
import { omarchyApi } from "../runtime.js";

// ---------------------------------------------------------------------------------------------
// Numbers from the tokens, as Qt's Style has them.

const tok = () => omarchyApi().tokens;
/** Style.space(n): max(1, round(n × scale)). */
const sp = (n) => {
  const t = tok();
  const s = t && t.spacing ? Number(t.spacing.scale) || 1 : 1;
  return Math.max(1, Math.round(n * s));
};
const spacing = (k, d) => {
  const t = tok();
  return t && t.spacing && typeof t.spacing[k] === "number" ? t.spacing[k] : d;
};
const radius = () => {
  const t = tok();
  return t ? Number(t.radius) || 0 : 0;
};
const smallRadius = () => Math.min(4, radius());
const knobSize = () => Math.max(14, Math.round(spacing("controlHeight", 28) * 0.38));
const trackHeight = () => Math.max(4, Math.round(spacing("controlHeight", 28) * 0.11));
const ring = () => Math.max(1, sp(2));

/** "Hot accent": the color with a quarter of the foreground over it (Qt.tint(c, alpha(fg, 0.25))). */
const hotMix = (c) => `color-mix(in srgb, ${c} 75%, var(--omarchy-foreground) 25%)`;

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

/** Call `fn` whenever `el` changes size (and once). */
function onResize(el, fn) {
  const ro = new ResizeObserver(() => fn());
  ro.observe(el);
  return ro;
}

/** Pointer capture drag on `target`: down(e) → move(dx, dy, e) → up(e); travel since the press. */
function drag(target, { down, move, up, button = 0 }) {
  let origin = null;
  target.addEventListener("pointerdown", (e) => {
    if (e.button !== button) return;
    e.stopPropagation();
    target.setPointerCapture(e.pointerId);
    origin = [e.clientX, e.clientY];
    if (down) down(e);
  });
  target.addEventListener("pointermove", (e) => {
    if (!origin) return;
    if (move) move(e.clientX - origin[0], e.clientY - origin[1], e);
  });
  const end = (e) => {
    if (!origin) return;
    origin = null;
    if (up) up(e);
  };
  target.addEventListener("pointerup", end);
  target.addEventListener("pointercancel", end);
}

// ---------------------------------------------------------------------------------------------
// TrimHandle: a trim cap (edge left/right) or a crop handle (crop, any of the eight edges).

const CROP_EDGES = ["top-left", "top", "top-right", "left", "right", "bottom-left", "bottom", "bottom-right"];

export class TrimHandle extends OmarchyElement {
  static focusable = true;
  static props = { edge: ["string", "left"], crop: ["bool"], cursor: ["bool"], color: ["string", null], gripColor: ["string", null] };
  static css = `
:host {
  --c: var(--omarchy-accent);
  --draw: var(--c);
  display: inline-block; position: relative; flex: none; outline: none;
  width: ${space(8)}; height: ${space(40)};
  touch-action: none; cursor: ew-resize;
}
:host([crop]) { width: ${space(4)}; height: ${space(18)}; }
:host([crop][edge="top"]), :host([crop][edge="bottom"]) { width: ${space(18)}; height: ${space(4)}; cursor: ns-resize; }
:host([crop][edge*="-"]) { width: ${space(18)}; height: ${space(18)}; }
:host([edge="top-left"]), :host([edge="bottom-right"]) { cursor: nwse-resize; }
:host([edge="top-right"]), :host([edge="bottom-left"]) { cursor: nesw-resize; }
:host(.hot) { --draw: ${hotMix("var(--c)")}; }
.part { position: absolute; background: var(--draw); }
.grip { position: absolute; }
.hit { position: absolute; inset: calc(-1 * var(--omarchy-space-sm)); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="part a"></div><div class="part b"></div><div class="grip"></div><div class="hit"></div>`;
    this.setAttribute("role", "slider");
    this._hover = false;
    this._dragging = false;
    const hit = this.$(".hit");
    hit.addEventListener("pointerenter", () => ((this._hover = true), this.update()));
    hit.addEventListener("pointerleave", () => ((this._hover = false), this.update()));
    this.addEventListener("focus", () => this.update());
    this.addEventListener("blur", () => this.update());
    drag(hit, {
      down: () => {
        this.focus({ preventScroll: true });
        this._dragging = true;
        this.update();
        this.emit("press", { edge: this.edge });
      },
      move: (dx, dy) => this.emit("move", { edge: this.edge, dx, dy }),
      up: () => {
        this._dragging = false;
        this.update();
        this.emit("release", { edge: this.edge });
      },
    });
    this.addEventListener("keydown", (e) => {
      if (e.composedPath()[0] !== this) return;
      const step = e.shiftKey ? 10 : 1;
      let dx = 0;
      let dy = 0;
      if (e.key === "ArrowLeft") dx = -step;
      else if (e.key === "ArrowRight") dx = step;
      else if (e.key === "ArrowUp") dy = -step;
      else if (e.key === "ArrowDown") dy = step;
      else return;
      if (!this.crop) dy = 0;
      if (this.crop && (this.edge === "top" || this.edge === "bottom")) dx = 0;
      if (this.crop && (this.edge === "left" || this.edge === "right")) dy = 0;
      if (dx === 0 && dy === 0) return;
      e.preventDefault();
      e.stopPropagation();
      this.emit("nudge", { edge: this.edge, dx, dy });
    });
    onResize(this, () => this.update());
  }
  /** Pointer on it, focus, a drag or `cursor`. */
  get hot() {
    return this._hover || this._dragging || this.cursor || this.matches(":focus");
  }
  get dragging() {
    return !!this._dragging;
  }
  // A handle inside a CropFrame or Timeline (data-internal) keeps its events in their shadow root;
  // they re-emit them with the edge, clip and time.
  emit(name, detail) {
    if (!this.hasAttribute("data-internal")) return super.emit(name, detail);
    return this.dispatchEvent(new CustomEvent(name, { detail, bubbles: true, composed: false, cancelable: true }));
  }
  update() {
    this.classList.toggle("hot", this.hot);
    if (this.color) this.style.setProperty("--c", this.color);
    else this.style.removeProperty("--c");
    const w = this.clientWidth;
    const h = this.clientHeight;
    const small = smallRadius();
    const a = this.$(".a").style;
    const b = this.$(".b").style;
    const g = this.$(".grip").style;
    const edge = this.edge;
    if (!this.crop) {
      // The cap: rounded all round, then squared on its inner half; a grip cut into it.
      Object.assign(a, { left: "0", top: "0", width: `${w}px`, height: `${h}px`, borderRadius: `${small}px`, display: "" });
      Object.assign(b, { left: edge === "right" ? "0" : `${w / 2}px`, top: "0", width: `${w / 2}px`, height: `${h}px`, borderRadius: "0", display: "" });
      const gw = Math.max(1, sp(2));
      const gh = Math.round(h * (this.hot ? 0.6 : 0.4));
      Object.assign(g, {
        display: "",
        left: `${Math.round((w - gw) / 2)}px`,
        top: `${Math.round((h - gh) / 2)}px`,
        width: `${gw}px`,
        height: `${gh}px`,
        borderRadius: `${Math.min(small, gw / 2)}px`,
        background: this.gripColor || "var(--omarchy-background)",
      });
      this.setAttribute("aria-orientation", "horizontal");
    } else {
      const t = sp(4);
      const corner = edge.indexOf("-") > 0;
      const horizontal = edge === "top" || edge === "bottom";
      const cr = `${Math.min(small, t / 2)}px`;
      const aTL = edge === "bottom" || edge === "right" || edge === "bottom-right" ? cr : "0";
      const aTR = edge === "bottom" || edge === "left" || edge === "bottom-left" ? cr : "0";
      const aBL = edge === "top" || edge === "right" || edge === "top-right" ? cr : "0";
      const aBR = edge === "top" || edge === "left" || edge === "top-left" ? cr : "0";
      Object.assign(a, {
        display: "",
        left: "0",
        top: `${corner && edge.indexOf("bottom") === 0 ? h - t : 0}px`,
        width: `${corner || horizontal ? w : t}px`,
        height: `${corner || horizontal ? t : h}px`,
        borderRadius: `${aTL} ${aTR} ${aBR} ${aBL}`,
      });
      const bTL = edge === "bottom-right" ? cr : "0";
      const bTR = edge === "bottom-left" ? cr : "0";
      const bBL = edge === "top-right" ? cr : "0";
      const bBR = edge === "top-left" ? cr : "0";
      Object.assign(b, {
        display: corner ? "" : "none",
        left: `${edge.indexOf("right") > 0 ? w - t : 0}px`,
        top: "0",
        width: `${t}px`,
        height: `${h}px`,
        borderRadius: `${bTL} ${bTR} ${bBR} ${bBL}`,
      });
      g.display = "none";
    }
    this.setAttribute("aria-label", `${this.crop ? "Crop" : "Trim"} ${edge}`);
  }
}

// ---------------------------------------------------------------------------------------------
// CropFrame: a 1px accent outline, rule-of-thirds guides and the eight crop handles.

export class CropFrame extends OmarchyElement {
  static props = { cursorEdge: ["string", ""] };
  static css = `
:host { display: block; position: relative; width: ${space(240)}; height: ${space(140)}; }
.third { position: absolute; background: var(--omarchy-track); }
.outline { position: absolute; inset: 0; border: 1px solid var(--omarchy-accent); pointer-events: none; }
omarchy-trim-handle { position: absolute; }
`;
  setup() {
    this.shadowRoot.innerHTML =
      `<div class="third v"></div><div class="third v"></div><div class="third h"></div><div class="third h"></div><div class="outline"></div>` +
      CROP_EDGES.map((e) => `<omarchy-trim-handle data-internal crop edge="${e}"></omarchy-trim-handle>`).join("");
    // The handles' own events become the frame's, with the edge.
    for (const type of ["press", "move", "release", "nudge"]) {
      this.shadowRoot.addEventListener(type, (e) => {
        if (e.target === this) return;
        e.stopPropagation();
        this.emit(type, e.detail);
      });
    }
    onResize(this, () => this.update());
  }
  update() {
    const w = this.clientWidth;
    const h = this.clientHeight;
    const v = this.shadowRoot.querySelectorAll(".third.v");
    const hz = this.shadowRoot.querySelectorAll(".third.h");
    v.forEach((el, i) => Object.assign(el.style, { left: `${Math.round((w * (i + 1)) / 3)}px`, top: "0", width: "1px", height: `${h}px` }));
    hz.forEach((el, i) => Object.assign(el.style, { top: `${Math.round((h * (i + 1)) / 3)}px`, left: "0", height: "1px", width: `${w}px` }));
    for (const hd of this.shadowRoot.querySelectorAll("omarchy-trim-handle")) {
      const edge = hd.getAttribute("edge");
      hd.toggleAttribute("cursor", this.cursorEdge === edge);
      const hw = hd.offsetWidth;
      const hh = hd.offsetHeight;
      hd.style.left = `${edge.includes("left") ? 0 : edge.includes("right") ? w - hw : Math.round((w - hw) / 2)}px`;
      hd.style.top = `${edge.indexOf("top") === 0 ? 0 : edge.indexOf("bottom") === 0 ? h - hh : Math.round((h - hh) / 2)}px`;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// DragHandle: a 2×3 grid of dots to drag a row by; Up/Down ask to move it.

export class DragHandle extends OmarchyElement {
  static focusable = true;
  static props = { cursor: ["bool"] };
  static css = `
:host { display: inline-flex; align-items: center; justify-content: center; position: relative; flex: none; width: ${space(16)}; height: ${space(22)}; outline: none; cursor: grab; touch-action: none; }
:host(.dragging) { cursor: grabbing; }
.grid { display: grid; grid-template-columns: repeat(2, auto); }
.dot { background: var(--omarchy-text-dim); }
:host(.hot) .dot { background: var(--omarchy-foreground); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="grid">${'<div class="dot"></div>'.repeat(6)}</div>`;
    this.setAttribute("role", "button");
    this.setAttribute("aria-label", "Drag to reorder");
    this._hover = false;
    this._dragging = false;
    this.addEventListener("pointerenter", () => ((this._hover = true), this.update()));
    this.addEventListener("pointerleave", () => ((this._hover = false), this.update()));
    this.addEventListener("focus", () => this.update());
    this.addEventListener("blur", () => this.update());
    drag(this, {
      down: () => {
        this.focus({ preventScroll: true });
        this._dragging = true;
        this.update();
        this.emit("press");
      },
      move: (dx, dy) => this.emit("move", { dx, dy }),
      up: () => {
        this._dragging = false;
        this.update();
        this.emit("release");
      },
    });
    this.addEventListener("keydown", (e) => {
      if (e.key === "ArrowUp") this.emit("move-request", { delta: -1 });
      else if (e.key === "ArrowDown") this.emit("move-request", { delta: 1 });
      else return;
      e.preventDefault();
    });
  }
  get hot() {
    return this._hover || this._dragging || this.cursor || this.matches(":focus");
  }
  get dragging() {
    return !!this._dragging;
  }
  update() {
    this.classList.toggle("hot", this.hot);
    this.classList.toggle("dragging", !!this._dragging);
    const d = sp(3);
    const g = sp(3);
    this.$(".grid").style.gap = `${g}px`;
    for (const dot of this.shadowRoot.querySelectorAll(".dot")) {
      Object.assign(dot.style, { width: `${d}px`, height: `${d}px`, borderRadius: `${Math.min(smallRadius(), d / 2)}px` });
    }
  }
}

// ---------------------------------------------------------------------------------------------
// DropIndicator: where a dragged row lands; an accent line across the list, taking no layout space
// when positioned by its list.

export class DropIndicator extends OmarchyElement {
  static css = `
:host {
  --t: max(1px, ${space(2)});
  display: block; width: ${space(200)}; height: var(--t);
  border-radius: min(var(--omarchy-radius-small), calc(var(--t) / 2));
  background: var(--omarchy-accent); pointer-events: none;
}`;
  setup() {
    this.setAttribute("role", "presentation");
  }
}

// ---------------------------------------------------------------------------------------------
// Scrubber: elapsed time, a position bar (the Slider's metrics) and the total.

const clock = (seconds) => {
  const s = Math.max(0, Math.floor(seconds));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  const pad = (n) => (n < 10 ? "0" : "") + n;
  return h > 0 ? `${h}:${pad(m)}:${pad(sec)}` : `${m}:${pad(sec)}`;
};

export class Scrubber extends OmarchyElement {
  static focusable = true;
  static props = { position: ["number", 0], duration: ["number", 0], cursor: ["bool"], surfaceColor: ["string", null] };
  static css = `
:host {
  --knob: max(14px, round(calc(var(--omarchy-space-control-height) * 0.38), 1px));
  --ring: max(1px, ${space(2)});
  --inner-knob: max(4px, calc(var(--knob) - 2 * var(--ring)));
  --track: max(4px, round(calc(var(--omarchy-space-control-height) * 0.11), 1px));
  display: inline-flex; align-items: center; gap: var(--omarchy-space-control-gap); vertical-align: middle;
  width: ${space(360)}; height: max(${space(22)}, calc(var(--knob) + var(--omarchy-space-md)));
  outline: none; user-select: none; -webkit-user-select: none;
}
.time { font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, normal); color: var(--omarchy-text-dim); white-space: pre; flex: none; }
.bar { position: relative; flex: 1; align-self: stretch; cursor: pointer; touch-action: none; }
.track, .fill { position: absolute; top: calc(50% - var(--track) / 2); height: var(--track); border-radius: calc(var(--track) / 2); }
.track { background: var(--omarchy-track); }
.fill { background: var(--omarchy-foreground); }
.knob {
  position: absolute; top: calc(50% - var(--inner-knob) / 2); width: var(--inner-knob); height: var(--inner-knob); border-radius: 50%;
  background: var(--omarchy-foreground);
  transition: transform var(--omarchy-motion-fast) cubic-bezier(0.33, 1, 0.68, 1);
}
:host(.hot) .knob { transform: scale(1.15); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<span class="time elapsed"></span><div class="bar"><div class="track"></div><div class="fill"></div><div class="knob"></div></div><span class="time total"></span>`;
    this.setAttribute("role", "slider");
    this._hover = false;
    this._pressed = false;
    const bar = this.$(".bar");
    bar.addEventListener("pointerenter", () => ((this._hover = true), this.update()));
    bar.addEventListener("pointerleave", () => ((this._hover = false), this.update()));
    this.addEventListener("focus", () => this.update());
    this.addEventListener("blur", () => this.update());
    const timeAt = (clientX) => {
      const r = bar.getBoundingClientRect();
      const k = knobSize();
      const tw = r.width - k;
      if (tw <= 0) return 0;
      return clamp((clientX - r.left - k / 2) / tw, 0, 1) * this.duration;
    };
    drag(bar, {
      down: (e) => {
        this.focus({ preventScroll: true });
        this._pressed = true;
        this.seek(timeAt(e.clientX));
      },
      move: (_dx, _dy, e) => this.seek(timeAt(e.clientX)),
      up: () => ((this._pressed = false), this.update()),
    });
    this.addEventListener("keydown", (e) => {
      const step = e.shiftKey ? 30 : 5;
      if (e.key === "ArrowLeft") this.seek(Math.max(0, this.position - step));
      else if (e.key === "ArrowRight") this.seek(Math.min(this.duration, this.position + step));
      else if (e.key === "Home") this.seek(0);
      else if (e.key === "End") this.seek(this.duration);
      else return;
      e.preventDefault();
    });
    onResize(bar, () => this.update());
  }
  /** Ask to play from `time` (the `seek` event); the position follows unless it's prevented. */
  seek(time) {
    if (this.emit("seek", { time })) this.position = time;
    else this.update();
  }
  update() {
    this.$(".elapsed").textContent = clock(this.position);
    this.$(".total").textContent = clock(this.duration);
    const p = this.duration > 0 ? clamp(this.position / this.duration, 0, 1) : 0;
    const bar = this.$(".bar");
    const k = knobSize();
    const r = ring();
    const innerK = Math.max(4, k - r * 2);
    const tx = k / 2;
    const tw = Math.max(0, bar.clientWidth - k);
    const knobCenter = tx + Math.round(tw * p);
    const fillW = Math.max(0, Math.min(tw, knobCenter - k / 2 - tx));
    const unStart = Math.min(tx + tw, Math.max(tx, knobCenter + k / 2));
    const unW = Math.max(0, tx + tw - unStart);
    const fillEl = this.$(".fill");
    fillEl.style.left = `${tx}px`;
    fillEl.style.width = `${fillW}px`;
    fillEl.style.display = fillW > 0 ? "" : "none";
    const trackEl = this.$(".track");
    trackEl.style.left = `${unStart}px`;
    trackEl.style.width = `${unW}px`;
    trackEl.style.display = unW > 0 ? "" : "none";
    this.$(".knob").style.left = `${knobCenter - innerK / 2}px`;
    if (this.surfaceColor) this.style.setProperty("--surface", this.surfaceColor);
    this.classList.toggle("hot", this.cursor || this._hover || this._pressed || this.matches(":focus"));
    this.setAttribute("aria-valuenow", String(this.position));
    this.setAttribute("aria-valuemin", "0");
    this.setAttribute("aria-valuemax", String(this.duration));
    this.setAttribute("aria-valuetext", `${clock(this.position)} of ${clock(this.duration)}`);
  }
}

// ---------------------------------------------------------------------------------------------
// Timeline: a time ruler over clip tracks, with a playhead.

const STEPS = [0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];

export class Timeline extends OmarchyElement {
  static focusable = true;
  static props = {
    duration: ["number", 30],
    position: ["number", 0],
    tracks: ["json", () => []],
    selectedTrack: ["number", -1],
    selectedClip: ["number", -1],
    zoom: ["number", 1],
    viewStart: ["number", 0],
    maxZoom: ["number", 64],
    cursor: ["bool"],
  };
  static css = `
:host { display: block; position: relative; width: ${space(720)}; height: ${space(160)}; outline: none; user-select: none; -webkit-user-select: none; touch-action: none; }
.scrub { position: absolute; inset: 0; }
.lanes { position: absolute; inset: 0; overflow: hidden; pointer-events: none; }
.tick { position: absolute; width: 1px; background: var(--omarchy-text-faint); pointer-events: none; }
.tlabel { position: absolute; font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); color: var(--omarchy-text-dim); white-space: pre; pointer-events: none; }
.lane { position: absolute; left: 0; background: var(--omarchy-control-normal-fill); pointer-events: none; }
.clip { position: absolute; background: var(--omarchy-control-normal-fill); cursor: pointer; pointer-events: auto; }
.clip::after {
  content: ""; position: absolute; inset: 0; border-radius: inherit; pointer-events: none; border-style: solid;
  border-color: var(--omarchy-control-normal-border); border-width: var(--omarchy-control-normal-border-width);
}
.clip:not(.sel):hover { background: var(--omarchy-control-hover-fill); }
.clip:not(.sel):hover::after { border-color: var(--omarchy-control-hover-border); border-width: var(--omarchy-control-hover-border-width); }
.clip.sel { background: var(--omarchy-control-selected-fill); }
.clip.sel::after { border-color: var(--omarchy-accent); border-width: var(--sel-border); }
.clabel {
  position: absolute; top: 0; bottom: 0; left: var(--omarchy-space-control-padding-x); right: var(--omarchy-space-control-padding-x);
  display: flex; align-items: center; min-width: 0; pointer-events: none;
  font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, normal); color: var(--omarchy-foreground);
}
.clabel > span { white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.clip.sel .clabel { font-weight: bold; left: calc(${space(8)} + var(--omarchy-space-control-padding-x)); right: calc(${space(8)} + var(--omarchy-space-control-padding-x)); }
.clip omarchy-trim-handle { position: absolute; top: 0; }
.playhead { position: absolute; pointer-events: none; --ph: var(--omarchy-accent); }
:host(.hot) .playhead { --ph: ${hotMix("var(--omarchy-accent)")}; }
.phead, .pline { position: absolute; background: var(--ph); }
.phit { position: absolute; pointer-events: auto; cursor: ew-resize; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="scrub"></div><div class="ruler"></div><div class="lanes"></div><div class="playhead"><div class="phead"></div><div class="pline"></div><div class="phit"></div></div>`;
    this.setAttribute("role", "slider");
    this.setAttribute("aria-label", "Timeline");
    this._key = "";
    this._headHover = false;
    this._headDrag = false;
    const scrub = this.$(".scrub");
    drag(scrub, {
      down: (e) => {
        this.focus({ preventScroll: true });
        this.seek(this.timeAt(this.localX(e)));
      },
      move: (_dx, _dy, e) => this.seek(this.timeAt(this.localX(e))),
    });
    const hit = this.$(".phit");
    hit.addEventListener("pointerenter", () => ((this._headHover = true), this.update()));
    hit.addEventListener("pointerleave", () => ((this._headHover = false), this.update()));
    drag(hit, {
      down: () => {
        this.focus({ preventScroll: true });
        this._headDrag = true;
        this.update();
      },
      move: (_dx, _dy, e) => this.seek(this.timeAt(this.localX(e))),
      up: () => ((this._headDrag = false), this.update()),
    });
    this.addEventListener("focus", () => this.update());
    this.addEventListener("blur", () => this.update());
    this.addEventListener("keydown", (e) => {
      if (e.composedPath()[0] !== this) return;
      const step = e.shiftKey ? this.tickStep() : 1;
      if (e.key === "ArrowLeft") this.seek(Math.max(0, this.position - step));
      else if (e.key === "ArrowRight") this.seek(Math.min(this.duration, this.position + step));
      else if (e.key === "Home") this.seek(0);
      else if (e.key === "End") this.seek(this.duration);
      else return;
      e.preventDefault();
    });
    this.addEventListener(
      "wheel",
      (e) => {
        const steps = -e.deltaY / (e.deltaMode === 1 ? 3 : 100);
        if (!steps) return;
        e.preventDefault();
        const x = this.localX(e);
        const anchor = this.timeAt(x);
        const z = this.clampView(this.zoom * Math.pow(1.25, steps), 0).zoom;
        const next = this.clampView(z, anchor - x / ((this.clientWidth * z) / this.duration));
        this.zoom = next.zoom;
        this.viewStart = next.start;
        this.emit("view-change", { zoom: next.zoom, viewStart: next.start });
      },
      { passive: false },
    );
    // Trim handles: from the clip's edge when the drag starts, by the travel since.
    const lanes = this.$(".lanes");
    lanes.addEventListener("press", (e) => {
      e.stopPropagation();
      const h = e.target;
      const { track, clip } = h.dataset;
      const c = this.tracks[track][clip];
      h._origin = h.dataset.edge === "left" ? c.start : c.end;
    });
    lanes.addEventListener("move", (e) => {
      e.stopPropagation();
      const h = e.target;
      this.trim(+h.dataset.track, +h.dataset.clip, h.dataset.edge, h._origin + e.detail.dx / this.pps());
    });
    lanes.addEventListener("release", (e) => e.stopPropagation());
    lanes.addEventListener("nudge", (e) => {
      e.stopPropagation();
      const h = e.target;
      const c = this.tracks[h.dataset.track][h.dataset.clip];
      const at = h.dataset.edge === "left" ? c.start : c.end;
      this.trim(+h.dataset.track, +h.dataset.clip, h.dataset.edge, at + e.detail.dx / this.pps());
    });
    lanes.addEventListener("pointerdown", (e) => {
      const el = e.target.closest && e.target.closest(".clip");
      if (!el || e.button !== 0) return;
      e.stopPropagation();
      this.focus({ preventScroll: true });
      const track = +el.dataset.track;
      const clip = +el.dataset.clip;
      if (this.emit("clip-press", { track, clip })) {
        this.selectedTrack = track;
        this.selectedClip = clip;
      }
    });
    onResize(this, () => this.update());
  }

  pps() {
    return (this.clientWidth * this.zoom) / Math.max(0.001, this.duration);
  }
  /** The labeled step: the first that keeps major ticks space(90) apart. */
  tickStep() {
    const target = sp(90) / Math.max(0.001, this.pps());
    for (const s of STEPS) if (s >= target) return s;
    return 3600;
  }
  xAt(t) {
    return Math.round((t - this.viewStart) * this.pps());
  }
  timeAt(x) {
    return clamp(this.viewStart + x / this.pps(), 0, this.duration);
  }
  localX(e) {
    return e.clientX - this.getBoundingClientRect().left;
  }
  clampView(z, start) {
    z = clamp(z, 1, this.maxZoom);
    const visible = this.duration / z;
    return { zoom: z, start: clamp(start, 0, this.duration - visible) };
  }
  format(seconds) {
    const s = Math.max(0, Math.round(seconds * 10) / 10);
    const m = Math.floor(s / 60);
    const rest = s - m * 60;
    const whole = Math.floor(rest);
    let text = `${m}:${whole < 10 ? "0" : ""}${whole}`;
    if (this.tickStep() < 1) text += `.${Math.round((rest - whole) * 10)}`;
    return text;
  }
  /** Ask to play from `time` (the `seek` event); the playhead follows unless it's prevented. */
  seek(time) {
    if (this.emit("seek", { time })) this.position = time;
  }
  /** Ask to move a clip's edge (the `trim` event); the clip follows unless it's prevented. */
  trim(track, clip, edge, time) {
    time = clamp(time, 0, this.duration);
    if (!this.emit("trim", { track, clip, edge, time })) return;
    const tracks = this.tracks.map((t) => t.map((c) => ({ ...c })));
    const c = tracks[track] && tracks[track][clip];
    if (!c) return;
    if (edge === "left") c.start = Math.min(time, c.end);
    else c.end = Math.max(time, c.start);
    this.tracks = tracks;
  }
  get hot() {
    return this.cursor || this.matches(":focus") || this._headHover || this._headDrag;
  }

  update() {
    const W = this.clientWidth;
    const H = this.clientHeight;
    if (!W) return;
    const tracks = this.tracks || [];
    const rulerH = sp(24);
    const laneGap = spacing("xs", 3);
    const inset = spacing("xxs", 2);
    const small = smallRadius();
    const laneH = tracks.length ? Math.floor((H - rulerH - laneGap * (tracks.length - 1)) / tracks.length) : 0;
    this.classList.toggle("hot", this.hot);

    // Ruler: minor ticks (five per step) and labeled major ones.
    const step = this.tickStep();
    const minor = step / 5;
    const end = this.viewStart + this.duration / this.zoom;
    let ticks = "";
    for (let i = Math.max(0, Math.floor(this.viewStart / minor)); i <= Math.ceil(end / minor); i++) {
      const t = i * minor;
      if (t > this.duration + 1e-6) break;
      const th = i % 5 === 0 ? sp(6) : sp(3);
      ticks += `<div class="tick" style="left:${Math.min(W - 1, this.xAt(t))}px;top:${rulerH - th}px;height:${th}px"></div>`;
    }
    const labels = [];
    for (let i = Math.max(0, Math.ceil(this.viewStart / step - 1e-6)); i <= Math.floor(Math.min(this.duration, end) / step + 1e-6); i++) labels.push(i * step);
    const ruler = this.$(".ruler");
    ruler.innerHTML = ticks + labels.map((t) => `<span class="tlabel" data-x="${this.xAt(t)}" style="top:${spacing("xxs", 2)}px">${esc(this.format(t))}</span>`).join("");
    for (const l of ruler.querySelectorAll(".tlabel")) {
      const w = l.getBoundingClientRect().width;
      l.style.left = `${Math.max(0, Math.min(W - w, +l.dataset.x - Math.round(w / 2)))}px`;
    }

    // Lanes and clips: built when their shape changes, placed on every update (a trim drag keeps
    // its handle).
    const key = JSON.stringify([tracks.map((t) => t.map((c) => c.label || "")), this.selectedTrack, this.selectedClip]);
    const lanes = this.$(".lanes");
    if (key !== this._key) {
      this._key = key;
      lanes.innerHTML = tracks
        .map(
          (t, ti) =>
            `<div class="lane" data-track="${ti}"></div>` +
            t
              .map((c, ci) => {
                const sel = ti === this.selectedTrack && ci === this.selectedClip;
                const handles = sel
                  ? ["left", "right"].map((e) => `<omarchy-trim-handle data-internal edge="${e}" data-edge="${e}" data-track="${ti}" data-clip="${ci}"></omarchy-trim-handle>`).join("")
                  : "";
                return `<div class="clip${sel ? " sel" : ""}" data-track="${ti}" data-clip="${ci}"><div class="clabel"><span>${esc(c.label || "")}</span></div>${handles}</div>`;
              })
              .join(""),
        )
        .join("");
    }
    this.style.setProperty("--sel-border", `${ring()}px`);
    for (const lane of lanes.querySelectorAll(".lane")) {
      const y = rulerH + +lane.dataset.track * (laneH + laneGap);
      Object.assign(lane.style, { top: `${y}px`, width: `${W}px`, height: `${laneH}px`, borderRadius: `${small}px` });
    }
    for (const el of lanes.querySelectorAll(".clip")) {
      const ti = +el.dataset.track;
      const c = tracks[ti][+el.dataset.clip];
      const x = this.xAt(c.start);
      const w = this.xAt(c.end) - x;
      const ch = laneH - inset * 2;
      Object.assign(el.style, { left: `${x}px`, top: `${rulerH + ti * (laneH + laneGap) + inset}px`, width: `${w}px`, height: `${ch}px`, borderRadius: `${small}px` });
      for (const h of el.querySelectorAll("omarchy-trim-handle")) {
        h.style.height = `${ch}px`;
        h.style.left = h.dataset.edge === "left" ? "0px" : `${w - sp(8)}px`;
      }
    }

    // The playhead: a head on the ruler's bottom edge and a line to the bottom of the last lane.
    const lw = ring();
    const head = sp(10);
    const ph = this.$(".playhead");
    const visible = this.position >= this.viewStart && this.position <= end;
    const py = rulerH - head;
    const pBottom = tracks.length > 0 ? rulerH + (tracks.length - 1) * (laneH + laneGap) + laneH : H;
    const phH = Math.max(head, pBottom - py);
    Object.assign(ph.style, { display: visible ? "" : "none", left: `${this.xAt(this.position) - Math.round(head / 2)}px`, top: `${py}px`, width: `${head}px`, height: `${phH}px` });
    Object.assign(this.$(".phead").style, { left: "0", top: "0", width: `${head}px`, height: `${head}px`, borderRadius: `${small}px` });
    Object.assign(this.$(".pline").style, { left: `${Math.round((head - lw) / 2)}px`, top: `${head}px`, width: `${lw}px`, height: `${phH - head}px` });
    const sm = spacing("sm", 4);
    Object.assign(this.$(".phit").style, { left: `${-sm}px`, top: "0", width: `${head + sm * 2}px`, height: `${head}px` });
    this.setAttribute("aria-valuenow", String(this.position));
    this.setAttribute("aria-valuemin", "0");
    this.setAttribute("aria-valuemax", String(this.duration));
  }
}

// ---------------------------------------------------------------------------------------------
// ColorPicker: an HSV square, a hue strip, an alpha strip, a preview with a hex field and the
// theme's palette.

/** HSV (h 0–1) to 8-bit RGB, rounding each channel, as both kits convert. */
export function hsvToRgb(h, s, v) {
  const h6 = (h - Math.floor(h)) * 6;
  const i = Math.floor(h6);
  const f = h6 - i;
  const p = v * (1 - s);
  const q = v * (1 - s * f);
  const t = v * (1 - s * (1 - f));
  const c = [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i % 6];
  return [Math.round(c[0] * 255), Math.round(c[1] * 255), Math.round(c[2] * 255)];
}

const hex2 = (n) => (n < 16 ? "0" : "") + n.toString(16);

// The theme's named colors the palette row can show: the state's `colors` (every key of
// colors.toml). A state without them (an old CLI) maps a name to the role that comes from it.
const NAMED_ROLES = { red: "urgent", green: "success", yellow: "warning", foreground: "foreground", background: "background", accent: "accent", muted: "muted" };
function namedColor(name) {
  const t = tok();
  if (!t) return null;
  const named = omarchyApi().colors;
  const v = named && named[name] ? named[name] : NAMED_ROLES[name] ? t.palette[NAMED_ROLES[name]] : null;
  return v ? String(v).slice(0, 7).toLowerCase() : null;
}

const DEFAULT_PALETTE = ["red", "orange", "yellow", "green", "cyan", "blue", "magenta", "foreground"];

export class ColorPicker extends OmarchyElement {
  static props = {
    hue: ["number", 0],
    saturation: ["number", 0],
    value: ["number", 1],
    alpha: ["number", 1],
    showAlpha: ["bool"],
    cursor: ["bool"],
    surfaceColor: ["string", null],
    paletteNames: ["json", () => DEFAULT_PALETTE],
    color: ["string", ""],
  };
  static css = `
:host {
  --knob: max(14px, round(calc(var(--omarchy-space-control-height) * 0.38), 1px));
  --ring: max(1px, ${space(2)});
  display: flex; flex-direction: column; gap: var(--omarchy-space-lg);
  width: ${space(300)}; height: ${space(260)}; box-sizing: border-box; user-select: none; -webkit-user-select: none;
}
.top { flex: 1; min-height: 0; display: flex; gap: var(--omarchy-space-md); }
.square, .strip { position: relative; outline: none; touch-action: none; }
.square { flex: 1; min-width: 0; cursor: crosshair; }
.strip { flex: none; width: ${space(14)}; cursor: pointer; }
:host([show-alpha="false"]) .strip.alpha { display: none; }
.layer { position: absolute; inset: 0; border-radius: var(--omarchy-radius-small); }
.white { background: linear-gradient(to right, #ffffffff, #ffffff00); }
.black { background: linear-gradient(to bottom, #00000000, #000000ff); }
.huebar { background: linear-gradient(to bottom, #ff0000 0%, #ffff00 16.6667%, #00ff00 33.3333%, #00ffff 50%, #0000ff 66.6667%, #ff00ff 83.3333%, #ff0000 100%); }
.track { background: var(--omarchy-track); }
.knob {
  position: absolute; border-radius: 50%; box-sizing: border-box;
  background: var(--omarchy-background); padding: 1px;
  transition: transform var(--omarchy-motion-fast) cubic-bezier(0.33, 1, 0.68, 1); pointer-events: none;
}
.knob.sv { width: var(--knob); height: var(--knob); }
.knob.sk { --pad: ${space(2)}; --sk: max(8px, calc(${space(14)} - 2 * var(--pad))); width: var(--sk); height: var(--sk); }
.knob .k-mid { width: 100%; height: 100%; border-radius: 50%; background: var(--omarchy-foreground); padding: 1px; box-sizing: border-box; }
.knob .k-fill { width: 100%; height: 100%; border-radius: 50%; }
.knob.hot { transform: scale(1.15); }
.field { display: flex; gap: var(--omarchy-space-md); align-items: center; }
.preview { flex: none; box-sizing: border-box; border: 1px solid var(--omarchy-control-normal-border); border-radius: var(--omarchy-radius); background-clip: padding-box; }
.field omarchy-text-field { flex: 1; min-width: 0; width: auto; }
.palette { display: flex; gap: var(--omarchy-space-md); }
.palette:empty { display: none; }
.swatch {
  flex: none; width: ${space(20)}; height: ${space(20)}; box-sizing: border-box; border-radius: var(--omarchy-radius-small);
  border: 1px solid var(--omarchy-control-normal-border); background-clip: padding-box; cursor: pointer; outline: none;
}
.swatch:hover, .swatch:focus { border-color: var(--omarchy-control-hover-border); }
.swatch.chosen { border: var(--ring) solid var(--omarchy-foreground); }
`;
  get showAlpha() {
    return this.getAttribute("show-alpha") !== "false";
  }
  set showAlpha(v) {
    this.setAttribute("show-alpha", v ? "true" : "false");
  }
  /** The picked color as #rrggbb, or #rrggbbaa below full alpha. Setting it picks that color. */
  get color() {
    return this.hex;
  }
  set color(v) {
    this.setColor(v);
  }
  get rgb() {
    return hsvToRgb(this.hue, this.saturation, this.value);
  }
  get hex() {
    const [r, g, b] = this.rgb;
    const out = `#${hex2(r)}${hex2(g)}${hex2(b)}`;
    return this.alpha < 1 ? out + hex2(Math.round(this.alpha * 255)) : out;
  }
  /** Pick `#rrggbb` / `#rrggbbaa` (keeping the hue when it's a gray). False when it isn't one. */
  setColor(c) {
    const m = /^#?([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?$/.exec(String(c).trim());
    if (!m) return false;
    const r = parseInt(m[1].slice(0, 2), 16) / 255;
    const g = parseInt(m[1].slice(2, 4), 16) / 255;
    const b = parseInt(m[1].slice(4, 6), 16) / 255;
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const d = max - min;
    let h = this.hue;
    if (d > 0) {
      if (max === r) h = (g - b) / d / 6;
      else if (max === g) h = ((b - r) / d + 2) / 6;
      else h = ((r - g) / d + 4) / 6;
      if (h < 0) h += 1;
    }
    this._batch = true;
    this.hue = h;
    this.saturation = max > 0 ? d / max : 0;
    this.value = max;
    this.alpha = m[2] ? parseInt(m[2], 16) / 255 : 1;
    this._batch = false;
    if (this._built) this.update();
    return true;
  }
  attributeChangedCallback(name, old, v) {
    if (name === "color") {
      if (v && v !== old && this.hex !== v.toLowerCase()) this.setColor(v);
      return;
    }
    if (!this._batch) super.attributeChangedCallback();
  }
  changed() {
    this.emit("change", { color: this.hex, hue: this.hue, saturation: this.saturation, value: this.value, alpha: this.alpha });
  }
  setup() {
    this.shadowRoot.innerHTML = `
<div class="top">
  <div class="square" tabindex="0" role="slider" aria-label="Saturation and value"><div class="layer hue"></div><div class="layer white"></div><div class="layer black"></div><div class="knob sv"><div class="k-mid"><div class="k-fill"></div></div></div></div>
  <div class="strip huestrip" tabindex="0" role="slider" aria-label="Hue" aria-orientation="vertical"><div class="layer huebar"></div><div class="knob sk"><div class="k-mid"></div></div></div>
  <div class="strip alpha" tabindex="0" role="slider" aria-label="Alpha" aria-orientation="vertical"><div class="layer track"></div><div class="layer fade"></div><div class="knob sk"><div class="k-mid"></div></div></div>
</div>
<div class="field"><div class="preview"></div><omarchy-text-field spellcheck="false" aria-label="Hex"></omarchy-text-field></div>
<div class="palette" role="listbox" aria-label="Palette"></div>`;
    if (this.hasAttribute("color")) this.setColor(this.getAttribute("color"));
    const square = this.$(".square");
    const pickSquare = (e) => {
      const r = square.getBoundingClientRect();
      this._batch = true;
      this.saturation = clamp((e.clientX - r.left) / r.width, 0, 1);
      this.value = 1 - clamp((e.clientY - r.top) / r.height, 0, 1);
      this._batch = false;
      this.update();
      this.changed();
    };
    drag(square, {
      down: (e) => {
        square.focus({ preventScroll: true });
        this._squareDown = true;
        pickSquare(e);
      },
      move: (_dx, _dy, e) => pickSquare(e),
      up: () => ((this._squareDown = false), this.update()),
    });
    square.addEventListener("keydown", (e) => {
      const step = e.shiftKey ? 0.1 : 0.01;
      if (e.key === "ArrowLeft") this.saturation = Math.max(0, this.saturation - step);
      else if (e.key === "ArrowRight") this.saturation = Math.min(1, this.saturation + step);
      else if (e.key === "ArrowUp") this.value = Math.min(1, this.value + step);
      else if (e.key === "ArrowDown") this.value = Math.max(0, this.value - step);
      else return;
      e.preventDefault();
      this.changed();
    });
    const strip = (el, picked, fraction) => {
      const pick = (e) => {
        const r = el.getBoundingClientRect();
        const pad = sp(2);
        const sk = Math.max(8, sp(14) - pad * 2);
        const span = Math.max(1, r.height - sk - pad * 2);
        picked(clamp((e.clientY - r.top - pad - sk / 2) / span, 0, 1));
      };
      drag(el, {
        down: (e) => {
          el.focus({ preventScroll: true });
          el._down = true;
          pick(e);
        },
        move: (_dx, _dy, e) => pick(e),
        up: () => ((el._down = false), this.update()),
      });
      el.addEventListener("keydown", (e) => {
        const step = e.shiftKey ? 0.1 : 0.01;
        if (e.key === "ArrowUp") picked(Math.max(0, fraction() - step));
        else if (e.key === "ArrowDown") picked(Math.min(1, fraction() + step));
        else return;
        e.preventDefault();
      });
      el.addEventListener("focus", () => this.update());
      el.addEventListener("blur", () => this.update());
    };
    strip(this.$(".huestrip"), (f) => ((this.hue = Math.min(f, 0.9999)), this.changed()), () => this.hue);
    strip(this.$(".alpha"), (f) => ((this.alpha = 1 - f), this.changed()), () => 1 - this.alpha);
    square.addEventListener("focus", () => this.update());
    square.addEventListener("blur", () => this.update());
    const field = this.$("omarchy-text-field");
    field.addEventListener("input", (e) => {
      e.stopPropagation();
      this._typing = true;
      if (this.setColor(field.value)) this.changed();
      this._typing = false;
    });
    field.addEventListener("change", (e) => {
      e.stopPropagation();
      field.value = this.hex;
    });
    const palette = this.$(".palette");
    const choose = (sw) => {
      const a = this.alpha;
      this.setColor(sw.dataset.color);
      this.alpha = a;
      this.changed();
    };
    palette.addEventListener("click", (e) => {
      const sw = e.target.closest(".swatch");
      if (sw) choose(sw);
    });
    palette.addEventListener("keydown", (e) => {
      const sw = e.target.closest(".swatch");
      if (!sw || (e.key !== "Enter" && e.key !== " ")) return;
      e.preventDefault();
      choose(sw);
    });
    onResize(this.$(".top"), () => this.update());
    onResize(field, () => this.update());
  }
  update() {
    const [r, g, b] = this.rgb;
    const opaque = `rgb(${r}, ${g}, ${b})`;
    const [hr, hg, hb] = hsvToRgb(this.hue, 1, 1);
    this.$(".hue").style.background = `rgb(${hr}, ${hg}, ${hb})`;
    this.$(".fade").style.background = `linear-gradient(to bottom, rgba(${r}, ${g}, ${b}, 1), rgba(${r}, ${g}, ${b}, 0))`;
    if (this.surfaceColor) this.style.setProperty("--surface", this.surfaceColor);
    const k = knobSize();
    const square = this.$(".square");
    const sv = this.$(".knob.sv");
    sv.querySelector(".k-fill").style.background = opaque;
    sv.style.left = `${Math.max(0, Math.min(square.clientWidth - k, Math.round(this.saturation * square.clientWidth - k / 2)))}px`;
    sv.style.top = `${Math.max(0, Math.min(square.clientHeight - k, Math.round((1 - this.value) * square.clientHeight - k / 2)))}px`;
    sv.classList.toggle("hot", this.cursor || square.matches(":focus") || !!this._squareDown);
    const pad = sp(2);
    const sk = Math.max(8, sp(14) - pad * 2);
    const place = (el, fraction) => {
      const knob = el.querySelector(".knob");
      knob.style.left = `${Math.round((el.clientWidth - sk) / 2)}px`;
      knob.style.top = `${pad + Math.round(fraction * Math.max(0, el.clientHeight - sk - pad * 2))}px`;
      knob.classList.toggle("hot", el.matches(":focus") || !!el._down);
    };
    place(this.$(".huestrip"), this.hue);
    place(this.$(".alpha"), 1 - this.alpha);
    const field = this.$("omarchy-text-field");
    const hex = this.hex;
    if (!this._typing && field.value !== hex) field.value = hex;
    const fh = field.offsetHeight;
    Object.assign(this.$(".preview").style, { width: `${fh}px`, height: `${fh}px`, background: `rgba(${r}, ${g}, ${b}, ${this.alpha})` });
    const colors = (this.paletteNames || []).map(namedColor).filter(Boolean);
    const palette = this.$(".palette");
    const key = colors.join(",");
    if (palette.dataset.key !== key) {
      palette.dataset.key = key;
      palette.innerHTML = colors.map((c) => `<div class="swatch" tabindex="0" role="option" data-color="${c}" style="background:${c}" aria-label="${c}"></div>`).join("");
    }
    for (const sw of palette.querySelectorAll(".swatch")) {
      const chosen = sw.dataset.color === hex.slice(0, 7);
      sw.classList.toggle("chosen", chosen);
      sw.setAttribute("aria-selected", String(chosen));
    }
  }
}

export const elements = {
  "omarchy-trim-handle": TrimHandle,
  "omarchy-crop-frame": CropFrame,
  "omarchy-drag-handle": DragHandle,
  "omarchy-drop-indicator": DropIndicator,
  "omarchy-scrubber": Scrubber,
  "omarchy-timeline": Timeline,
  "omarchy-color-picker": ColorPicker,
};
