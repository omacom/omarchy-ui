// Buttons, switches, fields, the dropdown and the slider (spec/components.md), drawn as the Qt kit
// draws them (qt/qml/Omarchy/Ui: Button, PanelActionButton, ButtonGroup, ToggleSwitch, Toggle,
// TextField, NumberField, Dropdown, PanelSlider): the same metrics, states, keys and strings.

import { OmarchyElement, esc, optionIcon, optionLabel, optionValue, readingKey } from "./base.js";
import { LOOK_CSS, alpha, centerGlyph, clickOnKeys, fitGlyph, reserve, space, trackPointer } from "./looks.js";
import { isOpen, place, popoverCss } from "./popup.js";
import { omarchyApi } from "../runtime.js";

const selectedHasWidth = () => {
  const t = omarchyApi().tokens;
  const w = t && t.controls && t.controls.selected.border && t.controls.selected.border.widths;
  return !!w && w.some((n) => n > 0);
};

const tooltipCss = `
.tip {
  position: fixed; inset: auto; margin: 0; padding: var(--omarchy-space-control-padding-y) var(--omarchy-space-control-padding-x);
  background: var(--omarchy-tooltip-background); color: var(--omarchy-tooltip-text);
  border-style: solid; border-width: var(--omarchy-tooltip-border-width); border-color: var(--omarchy-tooltip-border-solid);
  border-radius: var(--omarchy-radius); font-family: var(--omarchy-font-family); font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, normal); white-space: pre; pointer-events: none;
}
.tip:not(:popover-open) { display: none; }`;

/** A hover tooltip (Qt's ToolTip on Button / IconButton: 400 ms, the tooltip surface, bodySmall). */
function attachTooltip(el, text) {
  let tip = null;
  let timer = 0;
  const hide = () => {
    clearTimeout(timer);
    if (tip && isOpen(tip)) tip.hidePopover();
  };
  el.addEventListener("pointerenter", () => {
    const t = text();
    if (!t) return;
    clearTimeout(timer);
    timer = setTimeout(() => {
      if (!tip) {
        tip = document.createElement("div");
        tip.className = "tip";
        tip.popover = "manual";
        tip.setAttribute("role", "tooltip");
        el.shadowRoot.append(tip);
      }
      tip.textContent = text();
      tip.showPopover();
      place(tip, el, { side: "above", align: "center", gap: 4 });
    }, 400);
  });
  el.addEventListener("pointerleave", hide);
  el.addEventListener("pointerdown", hide);
}

// ---------------------------------------------------------------------------------------------
// Button

export class Button extends OmarchyElement {
  static focusable = true;
  static props = {
    text: ["string", ""],
    icon: ["string", ""],
    tooltip: ["string", ""],
    selected: ["bool"],
    active: ["bool"],
    cursor: ["bool"],
    bordered: ["bool"],
    leftAlign: ["bool"],
    foreground: ["string", null],
    disabled: ["bool"],
  };
  static css = `${LOOK_CSS}${tooltipCss}
:host { display: inline-flex; vertical-align: middle; cursor: pointer; outline: none; user-select: none; -webkit-user-select: none; }
:host([disabled]) { pointer-events: none; }
.box {
  ${reserve(["hover", "selected", "focus"])}
  flex: 1; min-width: 0;
  display: flex; align-items: center; justify-content: center; gap: var(--omarchy-space-control-gap);
  padding: calc(var(--omarchy-space-control-padding-y) + var(--rt) - var(--bt)) calc(var(--omarchy-space-control-padding-x) + var(--rr) - var(--br))
           calc(var(--omarchy-space-control-padding-y) + var(--rb) - var(--bb)) calc(var(--omarchy-space-control-padding-x) + var(--rl) - var(--bl));
  color: var(--ink, var(--omarchy-foreground));
  transition: background-color var(--omarchy-motion-fast), border-color var(--omarchy-motion-fast), transform var(--omarchy-motion-press) var(--omarchy-ease-out);
}
:host([bordered]) .box { ${reserve(["normal", "hover", "selected", "focus"])} }
:host([left-align]) .box { justify-content: flex-start; }
.box.down { transform: scale(0.97); }
.icon { font-size: var(--omarchy-font-icon); line-height: var(--omarchy-line-icon, normal); white-space: pre; }
.box.has-label .icon:not(:empty) { line-height: var(--omarchy-line-body, normal); height: var(--omarchy-line-body, auto); display: inline-flex; align-items: center; }
.label { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); }
:host([selected]) .box { color: var(--omarchy-control-selected-text); }
:host([selected][foreground]) .box { color: var(--ink); }
:host([selected]) .label { font-weight: bold; }
.icon:empty, .label:empty { display: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="box" part="box"><span class="icon" part="icon"></span><span class="label" part="label"></span><slot></slot></div>`;
    this.setAttribute("role", "button");
    if (this.hasAttribute("nofocus")) this.removeAttribute("tabindex");
    trackPointer(this);
    clickOnKeys(this);
    attachTooltip(this, () => this.tooltip);
  }
  update() {
    const box = this.$(".box");
    this.$(".icon").textContent = this.icon;
    fitGlyph(this.$(".icon"));
    this.$(".label").textContent = this.text;
    box.classList.toggle("has-label", this.text !== "");
    const hot = !!this._hot || this.cursor;
    const focus = !!this._focused;
    box.dataset.fill = this._pressed ? "pressed" : focus ? "focus" : hot ? "hover" : this.selected || this.active ? "selected" : "none";
    box.dataset.border = focus
      ? "focus"
      : hot
        ? "hover"
        : this.selected
          ? selectedHasWidth() ? "selected" : this.bordered ? "normal" : "none"
          : this.bordered ? "normal" : "none";
    box.classList.toggle("down", !!this._pressed);
    box.classList.toggle("ink", !!this.foreground);
    box.style.setProperty("--ink", this.foreground || "");
    if (!this.foreground) box.style.removeProperty("--ink");
    this.setAttribute("aria-pressed", String(this.selected));
    if (this.text) this.setAttribute("aria-label", this.text);
  }
}

// ---------------------------------------------------------------------------------------------
// IconButton (Qt's PanelActionButton): square, odd-sized, one glyph.

export class IconButton extends OmarchyElement {
  static focusable = true;
  static props = {
    icon: ["string", ""],
    tooltip: ["string", ""],
    cursor: ["bool"],
    bordered: ["bool"],
    disabled: ["bool"],
    foreground: ["string", null],
    size: ["number", 0],
  };
  static css = `${LOOK_CSS}${tooltipCss}
:host {
  --size: calc(2 * round(down, calc(round(max(${space(22)}, calc(var(--omarchy-font-icon) + 2 * var(--omarchy-space-sm))), 1px) / 2), 1px) + 1px);
  display: inline-flex; vertical-align: middle; width: var(--size); height: var(--size); flex: none;
  cursor: pointer; outline: none; user-select: none; -webkit-user-select: none;
}
:host([disabled]) { pointer-events: none; cursor: default; }
.box { flex: 1; display: flex; align-items: center; justify-content: center; border-radius: var(--omarchy-radius-small); transition: background-color 60ms; color: var(--ink, var(--omarchy-foreground)); }
:host([disabled]) .box { color: var(--omarchy-text-faint); }
.icon { font-size: var(--omarchy-font-icon); line-height: var(--omarchy-line-icon, normal); white-space: pre; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="box" part="box"><span class="icon" part="icon"></span></div>`;
    this.setAttribute("role", "button");
    if (this.hasAttribute("nofocus")) this.removeAttribute("tabindex");
    trackPointer(this);
    clickOnKeys(this);
    attachTooltip(this, () => this.tooltip);
  }
  update() {
    const box = this.$(".box");
    this.$(".icon").textContent = this.icon;
    centerGlyph(this.$(".icon"));
    const hot = (!!this._hot || this.cursor) && !this.disabled;
    const focus = !!this._focused;
    box.dataset.fill = focus ? "focus" : hot ? "hover" : "none";
    box.dataset.border = focus ? "focus" : hot && this.bordered ? "hover" : this.bordered ? "normal" : "none";
    box.classList.toggle("ink", !!this.foreground);
    if (this.foreground) box.style.setProperty("--ink", this.foreground);
    else box.style.removeProperty("--ink");
    if (this.size > 0) this.style.setProperty("--size", `${this.size}px`);
    if (this.tooltip) this.setAttribute("aria-label", this.tooltip);
    this.setAttribute("aria-disabled", String(this.disabled));
  }
}

// ---------------------------------------------------------------------------------------------
// ButtonGroup: bordered buttons md apart, one selected; Left/Right (h/l) rove, Return/Space choose.

export class ButtonGroup extends OmarchyElement {
  static focusable = true;
  static props = { options: ["json", () => []], value: ["string", ""], cursorIndex: ["number", -1] };
  static css = `
:host { display: inline-flex; gap: var(--omarchy-space-md); outline: none; vertical-align: middle; }
`;
  setup() {
    this._index = -1;
    this.setAttribute("role", "radiogroup");
    this.addEventListener("focus", () => {
      const i = this.options.findIndex((o) => optionValue(o) === this.value);
      this._index = i < 0 ? 0 : i;
      this.update();
    });
    this.addEventListener("blur", () => {
      this._index = -1;
      this.update();
    });
    this.addEventListener("keydown", (e) => {
      const k = readingKey(e, true);
      const n = this.options.length;
      if (k === "ArrowLeft") this._index = Math.max(0, (this._index < 0 ? 0 : this._index) - 1);
      else if (k === "ArrowRight") this._index = Math.min(n - 1, (this._index < 0 ? 0 : this._index) + 1);
      else if (e.key === "Enter" || e.key === " ") {
        if (this._index >= 0 && this._index < n) this.choose(optionValue(this.options[this._index]));
      } else return;
      e.preventDefault();
      this.update();
    });
    this.shadowRoot.addEventListener("click", (e) => {
      const b = e.target.closest && e.target.closest("omarchy-button");
      if (b) this.choose(b.dataset.value);
    });
    // The buttons never take focus from the group.
    this.shadowRoot.addEventListener("mousedown", (e) => e.preventDefault());
  }
  choose(v) {
    this.value = v;
    this.emit("change", { value: v });
  }
  update() {
    const opts = this.options;
    const key = JSON.stringify(opts);
    if (this._rendered !== key) {
      this._rendered = key;
      this.shadowRoot.innerHTML = opts
        .map((o) => `<omarchy-button nofocus bordered data-value="${esc(optionValue(o))}" text="${esc(optionLabel(o))}" icon="${esc(optionIcon(o))}" tooltip="${esc(o && o.tooltip ? o.tooltip : "")}"></omarchy-button>`)
        .join("");
    }
    [...this.shadowRoot.querySelectorAll("omarchy-button")].forEach((b, i) => {
      const sel = b.dataset.value === this.value;
      b.toggleAttribute("selected", sel);
      b.toggleAttribute("cursor", this.cursorIndex === i || this._index === i);
      b.setAttribute("role", "radio");
      b.setAttribute("aria-checked", String(sel));
    });
  }
}

// ---------------------------------------------------------------------------------------------
// ToggleSwitch: a bare switch; the cursor ring sits space(6) outside it.

const SWITCH_VARS = `
  --th: max(22px, round(calc(var(--omarchy-space-control-height) * 0.55), 1px));
  --tw: round(calc(var(--th) * 1.9), 1px);
  --knob: max(6px, round(calc(var(--th) * 0.72), 1px));
  --inset: max(1px, round(calc((var(--th) - var(--knob)) / 2), 1px));
  --pad: ${space(6)};`;

export class ToggleSwitch extends OmarchyElement {
  static focusable = true;
  static props = { checked: ["bool"], cursor: ["bool"], busy: ["bool"], passive: ["bool"], foreground: ["string", null] };
  static css = `${LOOK_CSS}
:host { ${SWITCH_VARS}
  display: inline-block; vertical-align: middle; position: relative; flex: none;
  width: calc(var(--tw) + 2 * var(--pad)); height: calc(var(--th) + 2 * var(--pad));
  cursor: pointer; outline: none; }
:host([passive]) { --pad: 0px; cursor: inherit; pointer-events: none; }
.ring { position: absolute; inset: 0; display: none; }
.ring.on { display: block; }
.track { position: absolute; left: var(--pad); top: var(--pad); width: var(--tw); height: var(--th); }
.track > .box { position: absolute; inset: 0; border-radius: calc(var(--th) / 2 * var(--omarchy-rounded)); }
.knob {
  position: absolute; top: calc((var(--th) - var(--knob)) / 2); left: var(--inset);
  width: var(--knob); height: var(--knob); border-radius: calc(var(--knob) / 2 * var(--omarchy-rounded));
  background: var(--omarchy-text-dim);
  transition: left var(--omarchy-motion-fast) cubic-bezier(0.33, 1, 0.68, 1), background-color var(--omarchy-motion-fast);
}
:host([checked]) .knob { left: calc(var(--tw) - var(--knob) - var(--inset)); background: var(--omarchy-control-selected-text); }
:host(:dir(rtl)) .knob { left: calc(var(--tw) - var(--knob) - var(--inset)); }
:host([checked]:dir(rtl)) .knob { left: var(--inset); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="ring box" data-border="hover"></div><div class="track"><div class="box"></div><div class="knob"></div></div>`;
    this.setAttribute("role", "switch");
    if (this.passive) this.removeAttribute("tabindex");
    trackPointer(this);
    clickOnKeys(this);
    this.addEventListener("click", () => {
      if (this.passive || this.busy) return;
      this.checked = !this.checked;
      this.emit("change", { checked: this.checked });
    });
  }
  update() {
    const hot = !this.passive && (!!this._hot || this.cursor || !!this._focused);
    this.$(".ring").classList.toggle("on", hot);
    const box = this.$(".track > .box");
    box.dataset.fill = this.checked ? "selected" : "normal";
    box.dataset.border = this.checked ? "selected" : "normal";
    box.classList.toggle("ink", !!this.foreground);
    if (this.foreground) this.style.setProperty("--ink", this.foreground);
    this.setAttribute("aria-checked", String(this.checked));
  }
}

// ---------------------------------------------------------------------------------------------
// Toggle: a labeled row with the switch at the end; the whole row is the control.

export class Toggle extends OmarchyElement {
  static focusable = true;
  static props = { label: ["string", ""], description: ["string", ""], checked: ["bool"], cursor: ["bool"] };
  static css = `${LOOK_CSS}
:host { display: block; width: ${space(240)}; cursor: pointer; outline: none; user-select: none; -webkit-user-select: none; }
.box {
  display: flex; align-items: center; gap: var(--omarchy-space-row-padding-x);
  min-height: 54px;
  padding: calc(var(--omarchy-space-huge) / 2 - var(--bt)) var(--omarchy-space-row-padding-x) calc(var(--omarchy-space-huge) / 2 - var(--bb));
  transition: background-color 100ms, border-color 100ms;
}
.words { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: var(--omarchy-space-xs); }
.title { font-size: var(--omarchy-font-subtitle); line-height: var(--omarchy-line-subtitle, normal); font-weight: bold; white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.desc { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); color: var(--omarchy-text-dim); overflow-wrap: anywhere; }
.desc:empty { display: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="box"><div class="words"><div class="title"></div><div class="desc"></div></div><omarchy-toggle-switch passive></omarchy-toggle-switch></div>`;
    this.setAttribute("role", "switch");
    trackPointer(this);
    clickOnKeys(this);
    this.addEventListener("click", () => {
      this.checked = !this.checked;
      this.emit("change", { checked: this.checked });
    });
  }
  update() {
    this.$(".title").textContent = this.label;
    this.$(".desc").textContent = this.description;
    const box = this.$(".box");
    const hot = !!this._hot || this.cursor;
    const focus = !!this._focused;
    box.dataset.fill = focus ? "focus" : hot ? "hover" : "normal";
    box.dataset.border = focus ? "focus" : hot ? "hover" : "normal";
    this.$("omarchy-toggle-switch").checked = this.checked;
    this.setAttribute("aria-checked", String(this.checked));
    this.setAttribute("aria-label", this.label);
  }
}

// ---------------------------------------------------------------------------------------------
// TextField

export const FIELD_CSS = `${LOOK_CSS}
.box {
  display: flex; align-items: center; gap: var(--omarchy-space-control-gap);
  padding: calc(var(--omarchy-space-input-padding-y)) var(--omarchy-space-control-padding-x);
  cursor: text;
}
input, textarea {
  all: unset; flex: 1; min-width: 0; box-sizing: border-box;
  font-family: var(--omarchy-font-family); font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal);
  color: var(--omarchy-foreground); caret-color: var(--omarchy-foreground);
}
input::placeholder, textarea::placeholder { color: var(--omarchy-text-faint); opacity: 1; }
input::selection, textarea::selection { background: var(--omarchy-control-selection-fill); color: var(--omarchy-foreground); }
`;

export class TextField extends OmarchyElement {
  static delegatesFocus = true;
  static props = { value: ["string", ""], placeholder: ["string", ""], cursor: ["bool"], password: ["bool"], disabled: ["bool"], name: ["string", ""] };
  static css = `${FIELD_CSS}
:host { display: inline-block; width: 200px; vertical-align: middle; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="box" part="box"><slot name="start"></slot><input part="input" spellcheck="false"><slot name="end"></slot></div>`;
    const input = (this.input = this.$("input"));
    trackPointer(this, this, { focus: false });
    input.addEventListener("focus", () => {
      this._focused = true;
      this.update();
    });
    input.addEventListener("blur", () => {
      this._focused = false;
      this.update();
    });
    input.addEventListener("input", () => {
      this._typing = true;
      this.value = input.value;
      this._typing = false;
    });
    input.addEventListener("change", () => this.emit("change", { value: input.value }));
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter") this.emit("accept", { value: input.value });
    });
    this.$(".box").addEventListener("mousedown", (e) => {
      if (e.target !== input) {
        e.preventDefault();
        input.focus();
      }
    });
  }
  focus(opts) {
    if (this.input) this.input.focus(opts);
    else super.focus(opts);
  }
  select() {
    this.input && this.input.select();
  }
  update() {
    const input = this.input;
    if (!this._typing && input.value !== this.value) input.value = this.value;
    input.placeholder = this.placeholder;
    input.type = this.password ? "password" : "text";
    input.disabled = this.disabled;
    if (this.name) input.name = this.name;
    const box = this.$(".box");
    const focus = !!this._focused;
    const hot = !!this._hot || this.cursor;
    box.dataset.fill = focus ? "focus" : hot ? "hover" : "normal";
    box.dataset.border = focus ? "focus" : hot ? "hover" : "normal";
    const label = this.getAttribute("aria-label") || this.placeholder;
    if (label) input.setAttribute("aria-label", label);
  }
}

// ---------------------------------------------------------------------------------------------
// NumberField: [−] value [+] with a label above.

export class NumberField extends OmarchyElement {
  static delegatesFocus = true;
  static props = { label: ["string", ""], value: ["number", 0], from: ["number", 0], to: ["number", 100], step: ["number", 1], cursor: ["bool"] };
  static css = `${LOOK_CSS}
:host { display: inline-flex; flex-direction: column; align-items: flex-start; gap: var(--omarchy-space-md); vertical-align: middle; }
.label { font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, normal); color: var(--omarchy-text-dim); white-space: pre; }
.label:empty { display: none; }
.spin {
  --h: max(var(--omarchy-space-control-height), calc(var(--omarchy-font-body) + 2 * var(--omarchy-space-control-padding-y)));
  position: relative; width: var(--omarchy-space-number-field-width); height: var(--h);
  display: flex; align-items: center;
  padding: 0 calc(var(--omarchy-space-control-padding-x));
}
input {
  all: unset; box-sizing: border-box; flex: 1; min-width: 0; text-align: center;
  font-family: var(--omarchy-font-family); font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal);
  color: var(--omarchy-foreground); caret-color: var(--omarchy-foreground);
}
input::selection { background: var(--omarchy-control-selection-fill); color: var(--omarchy-foreground); }
.step {
  position: absolute; top: calc(var(--bt) - var(--bt) * 0); top: 0;
  width: calc(var(--h) - 2 * var(--bt)); height: calc(var(--h) - 2 * var(--bt));
  display: flex; align-items: center; justify-content: center;
  border-radius: var(--omarchy-radius-small); cursor: pointer; user-select: none; -webkit-user-select: none;
  font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-foreground);
}
.step:hover { background: var(--omarchy-control-hover-fill); }
.step:active { background: var(--omarchy-control-pressed-fill); }
.step.off { opacity: 0.4; pointer-events: none; }
.down { left: 0; }
.up { right: 0; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<span class="label"></span><div class="spin box"><span class="step down" aria-hidden="true">−</span><input part="input" inputmode="numeric" spellcheck="false"><span class="step up" aria-hidden="true">+</span></div>`;
    const input = (this.input = this.$("input"));
    input.setAttribute("role", "spinbutton");
    trackPointer(this, this.$(".spin"), { focus: false });
    input.addEventListener("focus", () => ((this._focused = true), this.update()));
    input.addEventListener("blur", () => {
      this._focused = false;
      this.commit(input.value);
    });
    input.addEventListener("keydown", (e) => {
      if (e.key === "ArrowUp") this.nudge(1);
      else if (e.key === "ArrowDown") this.nudge(-1);
      else if (e.key === "PageUp") this.nudge(10);
      else if (e.key === "PageDown") this.nudge(-10);
      else if (e.key === "Enter") this.commit(input.value);
      else if (e.key === "Escape") input.value = String(this.value);
      else return;
      e.preventDefault();
    });
    const press = (sel, d) =>
      this.$(sel).addEventListener("mousedown", (e) => {
        e.preventDefault();
        this.nudge(d);
      });
    press(".down", -1);
    press(".up", 1);
  }
  clamp(n) {
    return Math.max(this.from, Math.min(this.to, Math.round(n)));
  }
  nudge(steps) {
    this.commit(this.value + steps * this.step);
  }
  commit(text) {
    const n = Number(text);
    const v = Number.isFinite(n) ? this.clamp(n) : this.value;
    const changed = v !== this.value;
    this.value = v;
    this.input.value = String(v);
    this.update();
    if (changed) this.emit("change", { value: v });
  }
  update() {
    const input = this.input;
    this.$(".label").textContent = this.label;
    if (document.activeElement !== this || this.shadowRoot.activeElement !== input) input.value = String(this.value);
    input.setAttribute("aria-valuenow", String(this.value));
    input.setAttribute("aria-valuemin", String(this.from));
    input.setAttribute("aria-valuemax", String(this.to));
    if (this.label) input.setAttribute("aria-label", this.label);
    const box = this.$(".spin");
    const focus = !!this._focused;
    const hot = !!this._hot || this.cursor;
    box.dataset.fill = focus ? "focus" : hot ? "hover" : "normal";
    box.dataset.border = focus ? "focus" : hot ? "hover" : "normal";
    this.$(".down").classList.toggle("off", this.value <= this.from);
    this.$(".up").classList.toggle("off", this.value >= this.to);
  }
}

// ---------------------------------------------------------------------------------------------
// Dropdown: a labeled single choice; the list opens below on the popups surface.

export class Dropdown extends OmarchyElement {
  static props = { label: ["string", ""], value: ["string", ""], options: ["json", () => []], cursor: ["bool"], showLabel: ["bool"], hideLabel: ["bool"] };
  static css = `${LOOK_CSS}${popoverCss("popups")}
:host { display: inline-flex; flex-direction: column; gap: var(--omarchy-space-label-gap); width: var(--omarchy-space-dropdown-width); vertical-align: middle; }
:host(:not([hide-label])) { min-height: calc(var(--omarchy-space-control-height) + var(--omarchy-space-huge)); }
.label { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); font-weight: bold; color: var(--omarchy-text-dim); white-space: pre; }
.label:empty, :host([hide-label]) .label { display: none; }
.trigger {
  height: var(--omarchy-space-control-height); display: flex; align-items: center; outline: none; cursor: pointer;
  padding: 0 var(--omarchy-space-control-gap) 0 var(--omarchy-space-control-padding-x); user-select: none; -webkit-user-select: none;
}
.value { flex: 1; min-width: 0; font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre; overflow: hidden; text-overflow: ellipsis; margin-right: var(--omarchy-space-md); }
.chev { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-text-dim); }
.pop {
  border-width: var(--omarchy-control-normal-border-width);
  padding: var(--omarchy-space-hairline);
  display: flex; flex-direction: column; gap: var(--omarchy-space-label-gap);
  max-height: calc(8 * var(--omarchy-space-popup-row-height) + 7 * var(--omarchy-space-label-gap) + 2 * var(--omarchy-space-hairline) + 2px);
  overflow-y: auto; scrollbar-width: none;
}
.pop:not(:popover-open) { display: none; }
.opt { flex: none; height: var(--omarchy-space-popup-row-height); display: flex; align-items: center; padding: 0 var(--omarchy-space-control-padding-x);
  font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre; overflow: hidden; text-overflow: ellipsis; cursor: pointer; }
.opt.cur { background: var(--omarchy-control-hover-fill); color: var(--omarchy-control-hover-text, var(--omarchy-foreground)); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<span class="label"></span><div class="trigger box" tabindex="0" role="combobox" aria-haspopup="listbox" aria-expanded="false"><span class="value"></span><span class="chev">\u{F0140}</span></div><div class="pop" popover="manual" role="listbox"></div>`;
    const trigger = (this.trigger = this.$(".trigger"));
    const pop = (this.pop = this.$(".pop"));
    this._cur = -1;
    trackPointer(this, trigger, { focus: false });
    trigger.addEventListener("focus", () => ((this._focused = true), this.update()));
    trigger.addEventListener("blur", () => ((this._focused = false), this.update()));
    trigger.addEventListener("click", () => this.toggle());
    trigger.addEventListener("keydown", (e) => {
      if (isOpen(pop)) return this.listKey(e);
      if (["Enter", " ", "ArrowDown"].includes(e.key) || e.key === "j") {
        e.preventDefault();
        this.open();
      }
    });
    pop.addEventListener("pointermove", (e) => {
      const o = e.target.closest(".opt");
      if (o) this.setCursor(Number(o.dataset.i));
    });
    pop.addEventListener("mousedown", (e) => e.preventDefault());
    pop.addEventListener("click", (e) => {
      const o = e.target.closest(".opt");
      if (o) this.choose(Number(o.dataset.i));
    });
    this._outside = (e) => {
      if (!e.composedPath().includes(this)) this.close();
    };
  }
  get popupOpen() {
    return isOpen(this.pop);
  }
  focus(opts) {
    this.trigger ? this.trigger.focus(opts) : super.focus(opts);
  }
  open() {
    if (isOpen(this.pop)) return;
    const opts = this.options;
    this.pop.innerHTML = opts.map((o, i) => `<div class="opt" role="option" data-i="${i}">${esc(optionLabel(o))}</div>`).join("");
    this.pop.showPopover();
    place(this.pop, this.trigger, { gap: Number.parseFloat(getComputedStyle(this).getPropertyValue("--omarchy-space-xxs")) || 2, width: this.trigger.offsetWidth });
    this.setCursor(Math.max(0, opts.findIndex((o) => optionValue(o) === this.value)));
    this.trigger.setAttribute("aria-expanded", "true");
    document.addEventListener("pointerdown", this._outside, true);
    this.emit("open");
  }
  close() {
    if (!isOpen(this.pop)) return;
    this.pop.hidePopover();
    this.trigger.setAttribute("aria-expanded", "false");
    document.removeEventListener("pointerdown", this._outside, true);
    this.emit("close");
  }
  toggle() {
    isOpen(this.pop) ? this.close() : this.open();
  }
  setCursor(i) {
    this._cur = i;
    this.pop.querySelectorAll(".opt").forEach((o, j) => o.classList.toggle("cur", j === i));
    const el = this.pop.querySelector(".opt.cur");
    if (el) el.scrollIntoView({ block: "nearest" });
  }
  listKey(e) {
    const n = this.options.length;
    if (e.key === "Escape") this.close();
    else if (e.key === "ArrowDown" || e.key === "j") this.setCursor(Math.min(n - 1, this._cur + 1));
    else if (e.key === "ArrowUp" || e.key === "k") this.setCursor(Math.max(0, this._cur - 1));
    else if (e.key === "Enter" || e.key === " ") this.choose(this._cur);
    else if (e.key === "Tab") return this.close();
    else return;
    e.preventDefault();
    e.stopPropagation();
  }
  choose(i) {
    const o = this.options[i];
    if (o === undefined) return;
    this.value = optionValue(o);
    this.close();
    this.emit("change", { value: this.value });
  }
  update() {
    this.$(".label").textContent = this.label;
    const cur = this.options.find((o) => optionValue(o) === this.value);
    this.$(".value").textContent = cur === undefined ? this.value : optionLabel(cur);
    const box = this.trigger;
    const focus = !!this._focused;
    const hot = !!this._hot || this.cursor;
    box.dataset.fill = focus ? "focus" : hot ? "hover" : "normal";
    box.dataset.border = focus ? "focus" : hot ? "hover" : "normal";
    if (this.label) box.setAttribute("aria-label", this.label);
  }
}

// ---------------------------------------------------------------------------------------------
// Slider (Qt's ValueSlider / PanelSlider): thin track, foreground fill, a knob ringed in the surface
// color, optional notches. Drag or click; Left/Right, Home/End and the wheel step it.

export class Slider extends OmarchyElement {
  static focusable = true;
  static props = { value: ["number", 0], min: ["number", 0], max: ["number", 1], step: ["number", 0.05], integer: ["bool"], ticks: ["number", 0], cursor: ["bool"] };
  static css = `
:host {
  --knob: max(14px, round(calc(var(--omarchy-space-control-height) * 0.38), 1px));
  --track: max(4px, round(calc(var(--omarchy-space-control-height) * 0.11), 1px));
  --p: 0;
  display: inline-block; position: relative; vertical-align: middle;
  width: ${space(200)}; height: max(${space(22)}, calc(var(--knob) + var(--omarchy-space-md)));
  cursor: pointer; outline: none; touch-action: none; user-select: none; -webkit-user-select: none;
}
.track, .fill { position: absolute; top: calc(50% - var(--track) / 2); height: var(--track); border-radius: calc(var(--track) / 2); }
.track { left: calc(var(--knob) / 2); right: calc(var(--knob) / 2); background: var(--omarchy-control-selected-fill); }
.fill { left: calc(var(--knob) / 2); width: calc((100% - var(--knob)) * var(--p)); background: var(--omarchy-foreground); }
.tick { position: absolute; top: calc(50% - (var(--track) + ${space(4)}) / 2); height: calc(var(--track) + ${space(4)});
  width: ${space(2)}; border-radius: 1px; background: var(--omarchy-popups-background);
  left: calc(var(--knob) / 2 + (100% - var(--knob)) * var(--f) - ${space(2)} / 2); }
.knob {
  position: absolute; top: calc(50% - var(--knob) / 2); left: calc((100% - var(--knob)) * var(--p));
  width: var(--knob); height: var(--knob); border-radius: 50%;
  background: var(--omarchy-foreground); border: ${space(2)} solid var(--omarchy-popups-background);
  transition: transform 110ms cubic-bezier(0.33, 1, 0.68, 1);
}
.knob.hot { transform: scale(1.15); }
:host(:not(.dragging)) .knob, :host(:not(.dragging)) .fill { transition: transform 110ms cubic-bezier(0.33, 1, 0.68, 1), left 140ms cubic-bezier(0.33, 1, 0.68, 1), width 140ms cubic-bezier(0.33, 1, 0.68, 1); }
:host([data-omarchy-still]) .knob, :host([data-omarchy-still]) .fill { transition: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="track"></div><div class="fill"></div><div class="ticks"></div><div class="knob"></div>`;
    this.setAttribute("role", "slider");
    this._live = null;
    trackPointer(this);
    const fromX = (x) => {
      const r = this.getBoundingClientRect();
      const k = this.$(".knob").offsetWidth;
      let f = Math.max(0, Math.min(1, (x - r.left - k / 2) / Math.max(1, r.width - k)));
      if (getComputedStyle(this).direction === "rtl") f = 1 - f;
      let v = this.min + f * (this.max - this.min);
      if (this.integer) v = Math.round(v);
      return Math.max(this.min, Math.min(this.max, v));
    };
    this.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      this.setPointerCapture(e.pointerId);
      this.classList.add("dragging");
      this._live = fromX(e.clientX);
      this.update();
      this.emit("input", { value: this._live });
    });
    this.addEventListener("pointermove", (e) => {
      if (this._live === null) return;
      this._live = fromX(e.clientX);
      this.update();
      this.emit("input", { value: this._live });
    });
    const end = () => {
      if (this._live === null) return;
      const v = this._live;
      this._live = null;
      this.classList.remove("dragging");
      this.value = v;
      this.emit("change", { value: v });
    };
    this.addEventListener("pointerup", end);
    this.addEventListener("pointercancel", end);
    this.addEventListener("keydown", (e) => {
      const big = (this.max - this.min) / 10;
      const d = { ArrowRight: this.step, ArrowUp: this.step, ArrowLeft: -this.step, ArrowDown: -this.step, PageUp: big, PageDown: -big }[readingKey(e, true)];
      let v;
      if (d !== undefined) v = this.value + (getComputedStyle(this).direction === "rtl" && e.key.startsWith("Arrow") && /Left|Right/.test(e.key) ? -d : d);
      else if (e.key === "Home") v = this.min;
      else if (e.key === "End") v = this.max;
      else return;
      e.preventDefault();
      this.set(v);
    });
    this.addEventListener("wheel", (e) => {
      e.preventDefault();
      this.set(this.value + (e.deltaY < 0 ? this.step : -this.step));
    }, { passive: false });
  }
  set(v) {
    if (this.integer) v = Math.round(v);
    v = Math.max(this.min, Math.min(this.max, Number(v.toFixed(6))));
    if (v === this.value) return;
    this.value = v;
    this.emit("input", { value: v });
    this.emit("change", { value: v });
  }
  update() {
    const v = this._live === null || this._live === undefined ? this.value : this._live;
    const range = Math.max(0.0001, this.max - this.min);
    const p = Math.max(0, Math.min(1, (v - this.min) / range));
    this.style.setProperty("--p", String(p));
    const count = this.ticks > 2 ? this.ticks - 2 : 0;
    const ticks = this.$(".ticks");
    if (ticks.childElementCount !== count) {
      ticks.innerHTML = Array.from({ length: count }, (_, i) => `<div class="tick" style="--f: ${(i + 1) / (this.ticks - 1)}"></div>`).join("");
    }
    this.$(".knob").classList.toggle("hot", !!this._hot || !!this._focused || this.cursor || this._live !== null);
    this.setAttribute("aria-valuenow", String(v));
    this.setAttribute("aria-valuemin", String(this.min));
    this.setAttribute("aria-valuemax", String(this.max));
  }
}

export const elements = {
  "omarchy-button": Button,
  "omarchy-icon-button": IconButton,
  "omarchy-button-group": ButtonGroup,
  "omarchy-toggle-switch": ToggleSwitch,
  "omarchy-toggle": Toggle,
  "omarchy-text-field": TextField,
  "omarchy-number-field": NumberField,
  "omarchy-dropdown": Dropdown,
  "omarchy-slider": Slider,
};
