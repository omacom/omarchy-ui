// Forms and pickers (spec/components/forms.md), drawn as qt/qml/Omarchy/Ui draws them.

import { OmarchyElement, esc, readingKey } from "./base.js";
import { LOOK_CSS, alpha, clickOnKeys, fitGlyph, space, trackPointer } from "./looks.js";
import { FIELD_CSS } from "./controls.js";
import { omarchyApi } from "../runtime.js";

// ---------------------------------------------------------------------------------------------
// SettingRow: words on the left, any control (the children) on the right.

export class SettingRow extends OmarchyElement {
  static props = { label: ["string", ""], description: ["string", ""], cursor: ["bool"], disabled: ["bool"] };
  static css = `
:host {
  display: flex; align-items: center; gap: var(--omarchy-space-xxl); box-sizing: border-box;
  width: ${space(240)};
  min-height: calc(var(--omarchy-space-control-height) + 2 * var(--omarchy-space-md));
  padding: 0 var(--omarchy-space-row-padding-x);
  border-radius: var(--omarchy-radius);
}
:host([cursor]) { background: var(--omarchy-control-hover-fill); }
.words { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: var(--omarchy-space-xxs); padding: var(--omarchy-space-md) 0; }
.label { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.desc { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); color: var(--omarchy-text-dim); overflow-wrap: anywhere; }
.desc:empty { display: none; }
:host([disabled]) .label, :host([disabled]) .desc { color: var(--omarchy-text-faint); }
.control { display: flex; align-items: center; gap: var(--omarchy-space-md); flex: none; padding: var(--omarchy-space-xxs) 0; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="words"><div class="label" part="label"></div><div class="desc" part="description"></div></div><div class="control"><slot></slot></div>`;
  }
  update() {
    this.$(".label").textContent = this.label;
    this.$(".desc").textContent = this.description;
  }
}

// ---------------------------------------------------------------------------------------------
// Checkbox and Radio: a box (a circle for Radio), then a label and an optional description. The
// element flips `checked` itself on a click, Space or Return and emits `change` (Qt's toggled());
// a Radio in a RadioGroup leaves choosing to the group.

const CHECK_CSS = `${LOOK_CSS}
:host {
  --box: ${space(16)};
  display: inline-flex; align-items: flex-start; gap: var(--omarchy-space-row-gap);
  vertical-align: top; cursor: pointer; outline: none; user-select: none; -webkit-user-select: none;
}
:host([disabled]) { cursor: default; pointer-events: none; }
.box {
  flex: none; position: relative; width: var(--box); height: var(--box);
  margin-top: max(0px, round(calc((var(--omarchy-line-body, 16px) - var(--box)) / 2), 1px));
  border-radius: var(--omarchy-radius-small);
  display: flex; align-items: center; justify-content: center;
}
:host([disabled]) .box { opacity: 0.5; }
:host([exclusive]) .box { border-radius: 50%; }
.check {
  display: none; color: var(--omarchy-accent); font-weight: bold;
  font-size: round(calc(var(--box) * 0.85), 1px); line-height: 1;
}
.dot { display: none; width: round(calc(var(--box) / 2), 1px); height: round(calc(var(--box) / 2), 1px); border-radius: 50%; background: var(--omarchy-accent); }
:host([checked]:not([exclusive])) .check { display: block; }
:host([checked][exclusive]) .dot { display: block; }
.words { min-width: 0; display: flex; flex-direction: column; gap: var(--omarchy-space-xxs);
  margin-top: max(0px, round(calc((var(--box) - var(--omarchy-line-body, 16px)) / 2), 1px)); }
.label { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.desc { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); color: var(--omarchy-text-dim); overflow-wrap: anywhere; }
.desc:empty { display: none; }
:host([disabled]) .label, :host([disabled]) .desc { color: var(--omarchy-text-faint); }
`;

export class Checkbox extends OmarchyElement {
  static focusable = true;
  static props = { label: ["string", ""], description: ["string", ""], checked: ["bool"], exclusive: ["bool"], cursor: ["bool"], disabled: ["bool"] };
  static css = CHECK_CSS;
  setup() {
    this.shadowRoot.innerHTML = `<div class="box" part="box"><span class="check">\u2713</span><span class="dot"></span></div><div class="words"><div class="label" part="label"></div><div class="desc" part="description"></div></div>`;
    trackPointer(this);
    // Any focus shows the cursor look, as Qt's activeFocus does.
    this.addEventListener("focus", () => ((this._focused = true), this.update()));
    clickOnKeys(this);
    this.addEventListener("click", () => {
      if (this.disabled) return;
      if (this._inGroup) {
        this.emit("toggle");
        return;
      }
      this.checked = !this.checked;
      this.emit("change", { checked: this.checked });
    });
  }
  update() {
    this.$(".label").textContent = this.label;
    this.$(".desc").textContent = this.description;
    const hot = !this.disabled && (!!this._hot || this.cursor || !!this._focused);
    const box = this.$(".box");
    box.dataset.fill = hot ? "hover" : this.checked ? "selected" : "normal";
    box.dataset.border = hot ? "hover" : this.checked ? "selected" : "normal";
    this.setAttribute("role", this.exclusive ? "radio" : "checkbox");
    this.setAttribute("aria-checked", String(this.checked));
    this.setAttribute("aria-disabled", String(this.disabled));
    if (this.disabled) this.removeAttribute("tabindex");
    else if (!this._inGroup && !this.hasAttribute("tabindex")) this.tabIndex = 0;
  }
}

export class Radio extends Checkbox {
  connectedCallback() {
    if (!this.hasAttribute("exclusive")) this.setAttribute("exclusive", "");
    super.connectedCallback();
  }
}

// ---------------------------------------------------------------------------------------------
// RadioGroup: Radios lg apart, one chosen. Up/Down/Left/Right and j/k move the choice.

export class RadioGroup extends OmarchyElement {
  static focusable = true;
  static props = { options: ["json", () => []], value: ["string", ""], cursorIndex: ["number", -1] };
  static css = `
:host { display: inline-flex; flex-direction: column; align-items: stretch; gap: var(--omarchy-space-lg); outline: none; vertical-align: top; }
`;
  setup() {
    this.setAttribute("role", "radiogroup");
    this.addEventListener("focus", () => ((this._focused = true), this.update()));
    this.addEventListener("blur", () => ((this._focused = false), this.update()));
    this.addEventListener("keydown", (e) => {
      const k = readingKey(e, true);
      const i = this.indexOfValue(this.value);
      if (k === "ArrowDown" || k === "ArrowRight") this.choose(Math.min(this.options.length - 1, i + 1));
      else if (k === "ArrowUp" || k === "ArrowLeft") this.choose(Math.max(0, i - 1));
      else return;
      e.preventDefault();
    });
    this.shadowRoot.addEventListener("toggle", (e) => {
      const r = e.composedPath().find((n) => n.tagName === "OMARCHY-RADIO");
      if (r) this.choose(Number(r.dataset.i));
      e.stopPropagation();
    });
    this.shadowRoot.addEventListener("mousedown", (e) => e.preventDefault());
  }
  indexOfValue(v) {
    return this.options.findIndex((o) => (o && typeof o === "object" ? String(o.value) : String(o)) === v);
  }
  choose(i) {
    const o = this.options[i];
    if (o === undefined) return;
    const v = o && typeof o === "object" ? String(o.value) : String(o);
    if (v === this.value) return;
    this.value = v;
    this.emit("change", { value: v });
  }
  update() {
    const opts = this.options;
    const key = JSON.stringify(opts);
    if (this._rendered !== key) {
      this._rendered = key;
      this.shadowRoot.innerHTML = opts
        .map((o, i) => {
          const obj = o && typeof o === "object";
          const label = obj ? String(o.label ?? o.value) : String(o);
          const desc = obj && o.description ? String(o.description) : "";
          return `<omarchy-radio data-i="${i}" tabindex="-1" label="${esc(label)}" description="${esc(desc)}"></omarchy-radio>`;
        })
        .join("");
      for (const r of this.shadowRoot.querySelectorAll("omarchy-radio")) r._inGroup = true;
    }
    const cur = this.indexOfValue(this.value);
    this.shadowRoot.querySelectorAll("omarchy-radio").forEach((r, i) => {
      r.toggleAttribute("checked", i === cur);
      r.toggleAttribute("cursor", this.cursorIndex === i || (!!this._focused && i === cur));
    });
  }
}

// ---------------------------------------------------------------------------------------------
// PasswordField: a TextField for secrets: a lock before the text, dots, an optional reveal eye, and
// an error under it (polkit's error border and text; the shake and flash run on a new error, never
// under reduced motion).

const reduced = () => {
  const t = omarchyApi().tokens;
  return (t && t.reduceMotion) || (typeof matchMedia === "function" && matchMedia("(prefers-reduced-motion: reduce)").matches);
};

export class PasswordField extends OmarchyElement {
  static delegatesFocus = true;
  static props = { text: ["string", ""], placeholder: ["string", ""], revealable: ["bool"], revealed: ["bool"], error: ["string", ""], cursor: ["bool"], disabled: ["bool"] };
  static css = `
:host { display: inline-flex; flex-direction: column; gap: var(--omarchy-space-label-gap); width: ${space(240)}; vertical-align: top; }
.wrap { position: relative; }
omarchy-text-field { display: block; width: 100%; }
.glyph { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-text-dim); flex: none; }
.eye { cursor: pointer; }
.eye:hover { color: var(--omarchy-foreground); }
:host(:not([revealable])) .eye { display: none; }
:host([error]:not([error=""])) .lock { color: var(--omarchy-polkit-text-error); }
:host([error]:not([error=""])) omarchy-text-field::part(box) {
  border-color: var(--omarchy-polkit-border-error);
  border-width: max(1px, var(--omarchy-control-normal-border-top));
}
.flash { position: absolute; inset: 0; pointer-events: none; border-radius: var(--omarchy-radius); opacity: 0;
  background: ${alpha("var(--omarchy-polkit-text-error)", "var(--omarchy-control-selected-fill-alpha)")}; }
.message { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); color: var(--omarchy-polkit-text-error); overflow-wrap: anywhere; }
.message:empty { display: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="wrap"><omarchy-text-field password><span class="glyph lock" slot="start">\u{F023}</span><span class="glyph eye" slot="end" role="button"></span></omarchy-text-field><div class="flash"></div></div><div class="message" role="alert"></div>`;
    const field = (this.field = this.$("omarchy-text-field"));
    field.addEventListener("input", () => {
      this._typing = true;
      this.text = field.value;
      this._typing = false;
    });
    // The field's own `accept` ({ value }) crosses out of this shadow root as ours.
    const eye = this.$(".eye");
    eye.addEventListener("mousedown", (e) => e.preventDefault());
    eye.addEventListener("click", (e) => {
      e.stopPropagation();
      this.revealed = !this.revealed;
    });
    this._lastError = this.error;
  }
  get value() {
    return this.text;
  }
  set value(v) {
    this.text = v;
  }
  /** Replay the error animation (polkit's shake and an urgent flash). */
  shake() {
    if (reduced() || !this.isConnected) return;
    const s = parseFloat(getComputedStyle(this).getPropertyValue("--omarchy-space-scale")) || 1;
    const d = Math.max(1, Math.round(8 * s));
    this.$(".wrap").animate(
      [{ transform: "translateX(0)" }, { transform: `translateX(${-d}px)`, offset: 35 / 140 }, { transform: `translateX(${d}px)`, offset: 85 / 140 }, { transform: "translateX(0)" }],
      { duration: 140 },
    );
    this.$(".flash").animate([{ opacity: 1 }, { opacity: 0 }], { duration: 400, easing: "cubic-bezier(0.25, 0.46, 0.45, 0.94)" });
  }
  update() {
    const field = this.field;
    if (!this._typing && field.value !== this.text) field.value = this.text;
    field.placeholder = this.placeholder;
    field.password = !this.revealed;
    field.cursor = this.cursor;
    field.disabled = this.disabled;
    field.setAttribute("aria-label", this.getAttribute("aria-label") || this.placeholder || "Password");
    const eye = this.$(".eye");
    eye.textContent = this.revealed ? "\u{F0209}" : "\u{F0208}";
    eye.setAttribute("aria-label", this.revealed ? "Hide" : "Show");
    fitGlyph(this.$(".lock"));
    fitGlyph(eye);
    this.$(".message").textContent = this.error;
    if (this.error && this.error !== this._lastError) this.shake();
    this._lastError = this.error;
  }
}

// ---------------------------------------------------------------------------------------------
// TextArea: TextField's chrome around wrapped text, growing from minRows to maxRows lines, then a
// thin scrollbar.

export class TextArea extends OmarchyElement {
  static delegatesFocus = true;
  static props = { text: ["string", ""], placeholder: ["string", ""], minRows: ["number", 3], maxRows: ["number", 6], cursor: ["bool"], disabled: ["bool"] };
  static css = `${FIELD_CSS}
:host { display: inline-block; width: ${space(240)}; vertical-align: top; }
.box { display: block; padding: 0; cursor: text; }
textarea {
  display: block; width: 100%; resize: none; field-sizing: content; overflow-y: auto; white-space: pre-wrap; overflow-wrap: break-word;
  padding: var(--omarchy-space-input-padding-y) var(--omarchy-space-control-padding-x);
  min-height: calc(var(--rows-min) * var(--omarchy-line-body, 16px) + 2 * var(--omarchy-space-input-padding-y));
  max-height: calc(var(--rows-max) * var(--omarchy-line-body, 16px) + 2 * var(--omarchy-space-input-padding-y));
  line-height: var(--omarchy-line-body, normal);
  scrollbar-width: thin; scrollbar-color: var(--omarchy-text-faint) transparent;
}
textarea::-webkit-scrollbar { width: var(--omarchy-space-scrollbar-thickness); }
textarea::-webkit-scrollbar-thumb { background: var(--omarchy-text-faint); border-radius: var(--omarchy-radius-small); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="box" part="box"><textarea part="input" spellcheck="false"></textarea></div>`;
    const area = (this.area = this.$("textarea"));
    trackPointer(this, this, { focus: false });
    area.addEventListener("focus", () => ((this._focused = true), this.update()));
    area.addEventListener("blur", () => ((this._focused = false), this.update()));
    area.addEventListener("input", () => {
      this._typing = true;
      this.text = area.value;
      this._typing = false;
    });
    area.addEventListener("change", () => this.emit("change", { value: area.value }));
  }
  get value() {
    return this.text;
  }
  set value(v) {
    this.text = v;
  }
  update() {
    const area = this.area;
    if (!this._typing && area.value !== this.text) area.value = this.text;
    area.placeholder = this.placeholder;
    area.disabled = this.disabled;
    this.style.setProperty("--rows-min", String(this.minRows));
    this.style.setProperty("--rows-max", String(Math.max(this.minRows, this.maxRows)));
    const box = this.$(".box");
    const focus = !!this._focused;
    const hot = !!this._hot || this.cursor;
    box.dataset.fill = focus ? "focus" : hot ? "hover" : "normal";
    box.dataset.border = focus ? "focus" : hot ? "hover" : "normal";
  }
}

export const elements = {
  "omarchy-setting-row": SettingRow,
  "omarchy-checkbox": Checkbox,
  "omarchy-radio": Radio,
  "omarchy-radio-group": RadioGroup,
  "omarchy-password-field": PasswordField,
  "omarchy-text-area": TextArea,
};
