// Feedback (spec/components/feedback.md): Meter, ProgressBar, Spinner, EmptyState, Banner, Toast,
// drawn as qt/qml/Omarchy/Ui/{Meter,ProgressBar,Spinner,EmptyState,Banner,Toast}.qml draw them.
//
// Events: Banner, EmptyState and Toast emit `action` when their action button is pressed (Qt's
// actionClicked); Banner emits `dismiss` from its dismiss button (Qt's dismissed).

import { OmarchyElement, surfaceBorder } from "./base.js";
import { alpha, fitGlyph, space } from "./looks.js";
import { omarchyApi } from "../runtime.js";

/** A tone's color (spec: neutral foreground, info accent, success, warning, error urgent). */
const TONES = {
  neutral: "var(--omarchy-foreground)",
  info: "var(--omarchy-accent)",
  success: "var(--omarchy-success)",
  warning: "var(--omarchy-warning)",
  error: "var(--omarchy-urgent)",
};

// Under reduced motion every animation here holds still (stills never catch a frame of one).
// Driven by a host attribute rather than :host-context() so WebKitGTK (Tauri) matches Chromium.
const STILL = ":host([data-still])";
const isStill = () =>
  !!(omarchyApi().tokens && omarchyApi().tokens.reduceMotion) ||
  (typeof document !== "undefined" && document.documentElement.hasAttribute("data-omarchy-reduce-motion"));

// ---------------------------------------------------------------------------------------------
// Meter: a determinate level on a track, optional markers and a label / value header.

export class Meter extends OmarchyElement {
  static props = {
    value: ["number", 0],
    tone: ["string", "neutral"],
    alarming: ["bool"],
    markers: ["json", () => []],
    label: ["string", ""],
    valueText: ["string", ""],
    indeterminate: ["bool"],
  };
  static defaultTone = "neutral";
  static css = `
:host {
  --th: var(--omarchy-space-meter-thickness);
  --tick-w: max(2px, round(calc(var(--th) * 0.5), 1px));
  --tick-h: round(calc(var(--th) * 2.5), 1px);
  --r: min(var(--omarchy-radius-small), calc(var(--th) / 2));
  --fill-color: var(--omarchy-foreground);
  display: block; width: ${space(200)};
}
.header { display: flex; align-items: flex-start; gap: var(--omarchy-space-control-gap); margin-bottom: var(--omarchy-space-md); }
.header.none { display: none; }
.label, .value { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre; }
.label { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; color: var(--omarchy-foreground); }
.value { color: var(--omarchy-text-dim); }
.bar { position: relative; height: var(--th); }
.bar.marked { height: max(var(--th), var(--tick-h)); }
.track {
  position: absolute; left: 0; right: 0; top: round(calc((100% - var(--th)) / 2), 1px); height: var(--th);
  background: var(--omarchy-track); border-radius: var(--r); overflow: hidden;
}
.fill {
  position: absolute; top: 0; bottom: 0; inset-inline-start: 0; width: calc(100% * var(--f, 0));
  background: var(--fill-color); border-radius: var(--r);
  transition: width var(--omarchy-motion-press) cubic-bezier(0.33, 1, 0.68, 1);
}
.segment {
  display: none; position: absolute; top: 0; bottom: 0; width: round(25%, 1px);
  left: calc((100% - round(25%, 1px)) / 2);
  background: var(--fill-color); border-radius: var(--r);
  animation: sweep 1400ms cubic-bezier(0.455, 0.03, 0.515, 0.955) infinite;
}
:host([indeterminate]) .fill { display: none; }
:host([indeterminate]) .segment { display: block; }
${STILL} .segment { animation: none; }
@keyframes sweep {
  from { left: calc(-1 * round(25%, 1px)); }
  to { left: 100%; }
}
.tick {
  position: absolute; width: var(--tick-w); height: var(--tick-h); top: round(calc((100% - var(--tick-h)) / 2), 1px);
  inset-inline-start: calc((100% - var(--tick-w)) * var(--at));
  background: var(--fill-color); border-radius: min(var(--omarchy-radius-small), calc(var(--tick-w) / 2));
}
:host([indeterminate]) .tick { display: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="header"><span class="label"></span><span class="value"></span></div><div class="bar"><div class="track"><div class="fill"></div><div class="segment"></div></div><div class="ticks"></div></div>`;
    this.setAttribute("role", "progressbar");
  }
  update() {
    this.toggleAttribute("data-still", isStill());
    const tone = this.alarming ? "error" : this.getAttribute("tone") || this.constructor.defaultTone;
    this.style.setProperty("--fill-color", this.alarming ? "var(--omarchy-urgent)" : TONES[tone] || TONES.neutral);
    const f = Math.max(0, Math.min(1, Number(this.value) || 0));
    this.$(".fill").style.setProperty("--f", String(f));
    const header = this.label !== "" || this.valueText !== "";
    this.$(".header").classList.toggle("none", !header);
    this.$(".label").textContent = this.label;
    this.$(".value").textContent = this.valueText;
    const markers = Array.isArray(this.markers) ? this.markers : [];
    this.$(".bar").classList.toggle("marked", markers.length > 0 && !this.indeterminate);
    const key = JSON.stringify(markers);
    if (this._markers !== key) {
      this._markers = key;
      this.$(".ticks").innerHTML = markers
        .map((m) => `<div class="tick" style="--at: ${Math.max(0, Math.min(1, Number(m) || 0))}"></div>`)
        .join("");
    }
    if (this.label) this.setAttribute("aria-label", this.label);
    if (this.indeterminate) this.removeAttribute("aria-valuenow");
    else this.setAttribute("aria-valuenow", String(Math.round(f * 100)));
    this.setAttribute("aria-valuemin", "0");
    this.setAttribute("aria-valuemax", "100");
  }
}

// ---------------------------------------------------------------------------------------------
// ProgressBar: a Meter with an info (accent) fill by default, plus `indeterminate`.

export class ProgressBar extends Meter {
  static defaultTone = "info";
}

// ---------------------------------------------------------------------------------------------
// Spinner: a glyph turning once every 900 ms, or with `pulse` breathing 0.5 ↔ 1 over 420 ms each
// way. Its ink is centered in a square box the size of the glyph's line height.

const fontRatios = new Map();
const canvas = typeof document !== "undefined" ? document.createElement("canvas").getContext("2d") : null;
function lineHeightOf(px, family) {
  if (!canvas) return Math.ceil(px * 1.32);
  let r = fontRatios.get(family);
  if (!r) {
    canvas.font = `1000px ${family}`;
    const m = canvas.measureText("Ag");
    r = (m.fontBoundingBoxAscent + m.fontBoundingBoxDescent) / 1000 || 1.2;
    fontRatios.set(family, r);
  }
  return Math.ceil(px * r - 1e-6);
}

export class Spinner extends OmarchyElement {
  static props = { glyph: ["string", "\u{f0996}"], size: ["string", "icon"], pulse: ["bool"], color: ["string", ""], running: ["string", "true"] };
  static css = `
:host { display: inline-block; position: relative; vertical-align: middle; flex: none; width: var(--box, 19px); height: var(--box, 19px); }
.spin { position: absolute; inset: 0; transform-origin: 50% 50%; animation: turn 900ms linear infinite; }
.mark {
  position: absolute; top: 0; left: var(--x, 0px);
  font-size: var(--px); line-height: var(--box); white-space: pre; color: var(--ink, var(--omarchy-foreground));
}
:host([pulse]) .spin { opacity: 0.5; animation: breathe 840ms ease-in-out infinite; }
:host([running="false"]) .spin { animation: none; }
${STILL} .spin { animation: none; }
@keyframes turn { to { transform: rotate(360deg); } }
@keyframes breathe { 0%, 100% { opacity: 0.5; } 50% { opacity: 1; } }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="spin"><span class="mark"></span></div>`;
    this.setAttribute("role", "img");
    this.setAttribute("aria-label", "Busy");
  }
  update() {
    this.toggleAttribute("data-still", isStill());
    const mark = this.$(".mark");
    mark.textContent = this.glyph;
    const s = String(this.size || "icon");
    const px = /^\d+(\.\d+)?$/.test(s) ? `${s}px` : `var(--omarchy-font-${s})`;
    this.style.setProperty("--px", px);
    if (this.color) this.style.setProperty("--ink", this.color);
    else this.style.removeProperty("--ink");
    if (!this.isConnected) return;
    // Measure after styles apply: the box is the line height, the ink centered in it (Qt's
    // TextMetrics.tightBoundingRect).
    const cs = getComputedStyle(mark);
    const size = parseFloat(cs.fontSize) || 14;
    const box = lineHeightOf(size, cs.fontFamily);
    this.style.setProperty("--box", `${box}px`);
    fitGlyph(mark);
    if (canvas) {
      canvas.font = `${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`;
      const m = canvas.measureText(this.glyph);
      const left = m.actualBoundingBoxLeft;
      const ink = left + m.actualBoundingBoxRight;
      this.style.setProperty("--x", `${Math.round((box - ink) / 2 + left)}px`);
    }
  }
}

// ---------------------------------------------------------------------------------------------
// EmptyState: glyph, title, hint and an optional action, centered; `size="overlay"` for overlays.

export class EmptyState extends OmarchyElement {
  static props = { glyph: ["string", ""], title: ["string", ""], hint: ["string", ""], actionText: ["string", ""], size: ["string", "panel"], cursor: ["bool"] };
  static css = `
:host { display: flex; flex-direction: column; align-items: center; gap: var(--omarchy-space-sm); text-align: center; }
:host([size="overlay"]) { gap: var(--omarchy-space-lg); }
.glyph { font-size: var(--omarchy-font-icon-large); line-height: var(--omarchy-line-icon-large, normal); color: var(--omarchy-text-dim); white-space: pre; }
.title { align-self: stretch; font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-text-dim); overflow-wrap: anywhere; }
.hint { align-self: stretch; font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, normal); color: var(--omarchy-text-faint); overflow-wrap: anywhere; }
:host([size="overlay"]) .glyph { font-size: var(--omarchy-font-display-large); line-height: var(--omarchy-line-display-large, normal); color: var(--omarchy-menu-selected-text); opacity: 0.8; }
:host([size="overlay"]) .title { font-size: var(--omarchy-font-title); line-height: var(--omarchy-line-title, normal); color: var(--omarchy-foreground); opacity: 0.7; }
:host([size="overlay"]) .hint { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-text-dim); }
.glyph:empty, .hint:empty { display: none; }
omarchy-button.none { display: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<span class="glyph"></span><div class="title"></div><div class="hint"></div><omarchy-button bordered></omarchy-button>`;
    this.$("omarchy-button").addEventListener("click", (e) => {
      e.stopPropagation();
      this.emit("action");
    });
  }
  update() {
    const g = this.$(".glyph");
    g.textContent = this.glyph;
    fitGlyph(g);
    this.$(".title").textContent = this.title;
    this.$(".hint").textContent = this.hint;
    const b = this.$("omarchy-button");
    b.setAttribute("text", this.actionText);
    b.toggleAttribute("cursor", this.cursor);
    b.classList.toggle("none", !this.actionText);
  }
}

// ---------------------------------------------------------------------------------------------
// Banner: an inline message in a tone: glyph, text, optional action and dismiss.

const BANNER_GLYPHS = { success: "\u{f012c}", warning: "\u{f002a}", error: "\u{f015a}" };

export class Banner extends OmarchyElement {
  static props = { text: ["string", ""], tone: ["string", "info"], glyph: ["string", ""], actionText: ["string", ""], dismissible: ["bool"], cursor: ["bool"] };
  static css = `
:host {
  --tone: var(--omarchy-accent);
  --bw: max(1px, var(--omarchy-control-normal-border-top));
  --btn-h: calc(var(--omarchy-line-body, 16px) + 2 * var(--omarchy-space-control-padding-y) + 2 * max(var(--omarchy-control-normal-border-top), var(--omarchy-control-hover-border-top)));
  display: flex; align-items: center; gap: var(--omarchy-space-control-gap); box-sizing: border-box;
  min-height: calc(max(var(--omarchy-space-control-height), var(--btn-h)) + 2 * var(--omarchy-space-sm) + 2 * var(--bw));
  padding: var(--omarchy-space-sm) var(--omarchy-space-control-padding-x);
  background: var(--omarchy-control-normal-fill);
  border: var(--bw) solid var(--tone);
  border-radius: var(--omarchy-radius);
}
:host(.trailing) { padding-inline-end: var(--omarchy-space-sm); }
.glyph { font-size: var(--omarchy-font-icon); line-height: var(--omarchy-line-icon, normal); color: var(--tone); white-space: pre; flex: none; }
.text { flex: 1; min-width: 0; font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-foreground); overflow-wrap: anywhere; }
.none { display: none !important; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<span class="glyph"></span><div class="text"></div><omarchy-button bordered></omarchy-button><omarchy-icon-button class="dismiss" icon="\u{f0156}" tooltip=""></omarchy-icon-button>`;
    this.setAttribute("role", "alert");
    this.$("omarchy-button").addEventListener("click", (e) => {
      e.stopPropagation();
      this.emit("action");
    });
    this.$(".dismiss").addEventListener("click", (e) => {
      e.stopPropagation();
      this.emit("dismiss");
    });
  }
  update() {
    const tone = this.tone;
    this.style.setProperty("--tone", tone === "success" || tone === "warning" || tone === "error" ? TONES[tone] : TONES.info);
    const g = this.$(".glyph");
    g.textContent = this.glyph || BANNER_GLYPHS[tone] || "\u{f02fd}";
    fitGlyph(g);
    this.$(".text").textContent = this.text;
    const b = this.$("omarchy-button");
    b.setAttribute("text", this.actionText);
    b.toggleAttribute("cursor", this.cursor);
    b.classList.toggle("none", !this.actionText);
    const d = this.$(".dismiss");
    d.classList.toggle("none", !this.dismissible);
    d.setAttribute("aria-label", this.t("common.dismiss", "Dismiss"));
    this.classList.toggle("trailing", !!this.actionText || this.dismissible);
  }
}

// ---------------------------------------------------------------------------------------------
// Toast: a transient message on the notifications surface, held as the app shows it.

export class Toast extends OmarchyElement {
  static props = { glyph: ["string", ""], title: ["string", ""], text: ["string", ""], actionText: ["string", ""], countdown: ["number", -1], cursor: ["bool"] };
  static css = `
:host {
  --line: max(1px, ${space(2)});
  display: flex; flex-direction: column; justify-content: center; position: relative; box-sizing: border-box; width: ${space(360)};
  background: var(--omarchy-notifications-background); color: var(--omarchy-notifications-text);
  ${surfaceBorder("notifications")}
  border-radius: var(--omarchy-radius);
  padding: ${space(10)} ${space(12)};
}
:host(.has-countdown) {
  padding-top: calc(${space(10)} - round(calc(var(--line) / 2), 1px));
  padding-bottom: calc(${space(10)} + round(calc(var(--line) / 2), 1px));
}
.tint {
  position: absolute; inset: 0; display: none;
  border-radius: max(0px, calc(var(--omarchy-radius) - max(var(--omarchy-notifications-border-left), var(--omarchy-notifications-border-top))));
  background: ${alpha("var(--omarchy-notifications-text)", "var(--omarchy-control-hover-fill-alpha)")};
}
:host([cursor]) .tint { display: block; }
.row { position: relative; display: flex; align-items: center; gap: ${space(12)}; }
.glyph { font-size: var(--omarchy-font-icon-large); line-height: var(--omarchy-line-icon-large, normal); white-space: pre; flex: none; }
.words { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: ${space(3)}; }
.title { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); font-weight: bold; white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.text { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: ${alpha("var(--omarchy-notifications-text)", 0.78)}; overflow-wrap: anywhere; }
.glyph:empty, .text:empty { display: none; }
.none { display: none !important; }
.countdown {
  position: absolute; bottom: 0; inset-inline-start: var(--omarchy-radius);
  width: calc((100% - 2 * var(--omarchy-radius)) * var(--left, 0)); height: var(--line);
  background: var(--omarchy-notifications-countdown);
}
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="gradient-ring"></div><div class="tint"></div><div class="row"><span class="glyph"></span><div class="words"><div class="title"></div><div class="text"></div></div><omarchy-button bordered foreground="var(--omarchy-notifications-text)"></omarchy-button></div><div class="countdown"></div>`;
    this.setAttribute("role", "alert");
    this.$("omarchy-button").addEventListener("click", (e) => {
      e.stopPropagation();
      this.emit("action");
    });
  }
  update() {
    const g = this.$(".glyph");
    g.textContent = this.glyph;
    fitGlyph(g);
    this.$(".title").textContent = this.title;
    this.$(".text").textContent = this.text;
    const b = this.$("omarchy-button");
    b.setAttribute("text", this.actionText);
    b.classList.toggle("none", !this.actionText);
    const c = this.$(".countdown");
    const left = Number(this.countdown);
    const hasCd = left >= 0;
    this.classList.toggle("has-countdown", hasCd);
    c.classList.toggle("none", !hasCd);
    c.style.setProperty("--left", String(Math.max(0, Math.min(1, left))));
    if (this.title) this.setAttribute("aria-label", this.title);
  }
}

export const elements = {
  "omarchy-meter": Meter,
  "omarchy-progress-bar": ProgressBar,
  "omarchy-spinner": Spinner,
  "omarchy-empty-state": EmptyState,
  "omarchy-banner": Banner,
  "omarchy-toast": Toast,
};
