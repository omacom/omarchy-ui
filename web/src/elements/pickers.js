// Pickers (spec/components/forms.md): MultiSelect, ColorWell, SwatchStrip, drawn as
// qt/qml/Omarchy/Ui/{MultiSelect,ColorWell,SwatchStrip}.qml draw them.

import { OmarchyElement, esc, readingKey } from "./base.js";
import { LOOK_CSS, alpha, clickOnKeys, space, trackPointer } from "./looks.js";
import { isOpen, place, popoverCss } from "./popup.js";

const px = (el, name, fallback) => Number.parseFloat(getComputedStyle(el).getPropertyValue(name)) || fallback;

function normalize(options) {
  const out = [];
  const seen = new Set();
  for (const o of Array.isArray(options) ? options : []) {
    const n =
      o && typeof o === "object"
        ? { value: String(o.value), label: String(o.label ?? o.value), description: o.description ? String(o.description) : "" }
        : { value: String(o), label: String(o), description: "" };
    if (!n.value || seen.has(n.value)) continue;
    seen.add(n.value);
    out.push(n);
  }
  return out;
}

// ---------------------------------------------------------------------------------------------
// MultiSelect: the shell's searchable multi-choice dropdown. The popup (top layer) holds a search
// field, a rule and a checkbox row per option.

export class MultiSelect extends OmarchyElement {
  static props = {
    label: ["string", ""],
    values: ["json", () => []],
    options: ["json", () => []],
    placeholder: ["string", ""],
    emptyText: ["string", ""],
    noSelectionText: ["string", ""],
    triggerLabel: ["string", ""],
    hideLabel: ["bool"],
    cursor: ["bool"],
    refreshable: ["bool"],
    loadingOptions: ["bool"],
    optionsError: ["string", ""],
  };
  static css = `${LOOK_CSS}${popoverCss("popups")}
:host { display: inline-flex; flex-direction: column; gap: var(--omarchy-space-label-gap); width: var(--omarchy-space-searchable-dropdown-width); vertical-align: top; }
.label { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); font-weight: bold; color: var(--omarchy-text-dim); white-space: pre; }
.label:empty, :host([hide-label]) .label { display: none; }
.trigger {
  height: var(--omarchy-space-control-height); display: flex; align-items: center; outline: none; cursor: pointer;
  padding: 0 var(--omarchy-space-control-gap) 0 var(--omarchy-space-control-padding-x); user-select: none; -webkit-user-select: none;
  color: var(--omarchy-popups-text);
}
.value { flex: 1; min-width: 0; font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre; overflow: hidden; text-overflow: ellipsis; margin-right: var(--omarchy-space-md); }
.value.none { color: var(--omarchy-text-dim); }
.chev { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-text-dim); }
.pop {
  border-width: var(--omarchy-control-normal-border-width);
  padding: var(--omarchy-space-hairline);
  overflow: hidden;
}
.pop:not(:popover-open) { display: none; }
.head { height: calc(var(--omarchy-space-popup-row-height) + var(--omarchy-space-control-padding-x)); padding: var(--omarchy-space-md); display: flex; gap: var(--omarchy-space-row-gap); }
.head omarchy-text-field { flex: 1; min-width: 0; height: 100%; }
.head omarchy-text-field::part(box) { height: 100%; padding-top: 0; padding-bottom: 0; }
.refresh { flex: none; aspect-ratio: 1; height: 100%; display: none; align-items: center; justify-content: center; cursor: pointer;
  font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); }
:host([refreshable]) .refresh { display: flex; }
.rule { height: 1px; background: ${alpha("var(--omarchy-popups-text)", 0.1)}; }
.list { position: relative; overflow-y: auto; scrollbar-width: none; display: flex; flex-direction: column; gap: var(--omarchy-space-label-gap); outline: none; }
.row {
  flex: none; display: flex; align-items: center; gap: var(--omarchy-space-row-gap);
  min-height: var(--omarchy-space-popup-row-height); padding: calc(var(--omarchy-space-row-padding-x) / 2) var(--omarchy-space-control-padding-x);
  border-radius: var(--omarchy-radius-small);
  cursor: pointer;
}
.row.cur { background: var(--omarchy-control-hover-fill); }
.row .box { flex: none; width: ${space(16)}; height: ${space(16)}; border-radius: var(--omarchy-radius-small);
  display: flex; align-items: center; justify-content: center; }
.row .check { display: none; color: var(--omarchy-control-selected-text); font-weight: bold; font-size: round(calc(${space(16)} * 0.85), 1px); line-height: 1; }
.row.on .check { display: block; }
.words { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: var(--omarchy-space-xxs); }
.words .l { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.row.cur .l { color: var(--omarchy-control-hover-text, var(--omarchy-popups-text)); }
.words .d { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); color: var(--omarchy-text-dim); white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.words .d:empty { display: none; }
.empty { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; color: var(--omarchy-text-faint);
  font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<span class="label"></span><div class="trigger box" tabindex="0" role="combobox" aria-haspopup="listbox" aria-expanded="false"><span class="value"></span><span class="chev">\u{F0140}</span></div><div class="pop" popover="manual"><div class="head"><omarchy-text-field></omarchy-text-field><div class="refresh box" data-fill="normal" data-border="normal"></div></div><div class="rule"></div><div class="list" tabindex="-1" role="listbox" aria-multiselectable="true"></div></div>`;
    const trigger = (this.trigger = this.$(".trigger"));
    const pop = (this.pop = this.$(".pop"));
    const search = (this.search = this.$("omarchy-text-field"));
    const list = (this.list = this.$(".list"));
    this._cur = -1;
    this._filtered = [];
    trackPointer(this, trigger, { focus: false });
    trigger.addEventListener("focus", () => ((this._focused = true), this.update()));
    trigger.addEventListener("blur", () => ((this._focused = false), this.update()));
    trigger.addEventListener("click", () => this.toggle());
    trigger.addEventListener("keydown", (e) => {
      if (["Enter", " ", "ArrowDown", "j"].includes(e.key)) this.toggle();
      else if (e.key === "Escape" && this.popupOpen) this.close();
      else return;
      e.preventDefault();
    });
    search.addEventListener("input", (e) => {
      e.stopPropagation();
      this.filter();
      this.setCursor(this._filtered.length ? 0 : -1);
    });
    search.addEventListener("change", (e) => e.stopPropagation());
    search.addEventListener("accept", (e) => e.stopPropagation());
    search.addEventListener("keydown", (e) => {
      if (e.key === "Escape") this.close();
      else if (e.key === "ArrowDown") {
        if (this._filtered.length) {
          this.setCursor(0);
          list.focus();
        }
      } else if (e.key === "Enter") {
        if (this._filtered.length) {
          this.setCursor(0);
          this.toggleCurrent();
        }
      } else return;
      e.preventDefault();
      e.stopPropagation();
    });
    list.addEventListener("keydown", (e) => {
      const k = readingKey(e, true);
      const n = this._filtered.length;
      if (e.key === "Escape") this.close();
      else if (k === "ArrowDown") {
        if (this._cur < n - 1) this.setCursor(this._cur + 1);
      } else if (k === "ArrowUp") {
        if (this._cur <= 0) search.focus();
        else this.setCursor(this._cur - 1);
      } else if (e.key === "Enter" || e.key === " ") this.toggleCurrent();
      else return;
      e.preventDefault();
      e.stopPropagation();
    });
    list.addEventListener("pointermove", (e) => {
      const r = e.target.closest(".row");
      if (r) this.setCursor(Number(r.dataset.i));
    });
    list.addEventListener("mousedown", (e) => e.preventDefault());
    list.addEventListener("click", (e) => {
      const r = e.target.closest(".row");
      if (r) this.toggleValue(this._filtered[Number(r.dataset.i)].value);
    });
    this.$(".refresh").addEventListener("click", () => {
      if (!this.loadingOptions) this.emit("refresh");
    });
    this._outside = (e) => {
      if (!e.composedPath().includes(this)) this.close();
    };
  }

  get popupOpen() {
    return !!this.pop && isOpen(this.pop);
  }
  focus(opts) {
    this.trigger ? this.trigger.focus(opts) : super.focus(opts);
  }

  resolved() {
    return normalize(this.options);
  }
  filter() {
    const q = (this.search.value || "").toLowerCase();
    const all = this.resolved();
    this._filtered = q
      ? all.filter((o) => o.label.toLowerCase().includes(q) || o.description.toLowerCase().includes(q) || o.value.toLowerCase().includes(q))
      : all;
    this.renderRows();
  }
  renderRows() {
    const set = new Set((this.values || []).map(String));
    const list = this.list;
    list.innerHTML =
      this._filtered
        .map(
          (o, i) =>
            `<div class="row${set.has(o.value) ? " on" : ""}${i === this._cur ? " cur" : ""}" role="option" aria-selected="${set.has(o.value)}" data-i="${i}"><div class="box" data-fill="${set.has(o.value) ? "selected" : "none"}" data-border="${set.has(o.value) ? "selected" : "normal"}"><span class="check">✓</span></div><div class="words"><div class="l">${esc(o.label)}</div><div class="d">${esc(o.description)}</div></div></div>`,
        )
        .join("") +
      (this._filtered.length
        ? ""
        : `<div class="empty">${esc(this.loadingOptions ? this.t("common.loading", "Loading…") : this.optionsError || this.emptyText || this.t("common.noOptions", "No options"))}</div>`);
  }
  setCursor(i) {
    this._cur = i;
    this.list.querySelectorAll(".row").forEach((r, j) => r.classList.toggle("cur", j === i));
    const el = this.list.querySelector(".row.cur");
    if (el) el.scrollIntoView({ block: "nearest" });
  }
  toggleCurrent() {
    const o = this._filtered[this._cur];
    if (o) this.toggleValue(o.value);
  }
  toggleValue(v) {
    const arr = (this.values || []).map(String);
    const i = arr.indexOf(v);
    if (i === -1) arr.push(v);
    else arr.splice(i, 1);
    this.values = arr;
    const cur = this._cur;
    this.renderRows();
    this.setCursor(cur);
    this.emit("change", { values: arr });
  }

  /** Open the list, placed below the trigger and clamped to the room below it, as Qt's popup. */
  open() {
    if (this.popupOpen) return;
    const pop = this.pop;
    this.search.value = "";
    this._cur = -1;
    this.filter();
    pop.showPopover();
    const gap = px(this, "--omarchy-space-xxs", 2);
    const row = px(this, "--omarchy-space-popup-row-height", 28);
    const lgap = px(this, "--omarchy-space-label-gap", 4);
    const scale = px(this, "--omarchy-space-scale", 1);
    const pcs = getComputedStyle(pop);
    const padY = (parseFloat(pcs.paddingTop) || 0) + (parseFloat(pcs.paddingBottom) || 0)
      + (parseFloat(pcs.borderTopWidth) || 0) + (parseFloat(pcs.borderBottomWidth) || 0);
    const head = this.$(".head").offsetHeight;
    const chrome = head + 1 + padY;
    const rows = [...this.list.querySelectorAll(".row")];
    const content = rows.reduce((h, r) => h + r.offsetHeight, 0) + Math.max(0, rows.length - 1) * lgap;
    const t = this.trigger.getBoundingClientRect();
    const below = Math.max(0, document.documentElement.clientHeight - (t.bottom + gap) - Math.max(1, Math.round(12 * scale)));
    const height = Math.min(below, content + chrome, row * 6 + 5 * lgap + chrome);
    pop.style.height = `${height}px`;
    this.list.style.height = `${Math.max(0, height - chrome)}px`;
    place(pop, this.trigger, { gap, width: this.trigger.offsetWidth });
    this.trigger.setAttribute("aria-expanded", "true");
    document.addEventListener("pointerdown", this._outside, true);
    this.search.focus({ preventScroll: true });
    this.emit("open");
  }
  close() {
    if (!this.popupOpen) return;
    const hadFocus = this.shadowRoot.activeElement && this.shadowRoot.activeElement !== this.trigger;
    this.pop.hidePopover();
    this.search.value = "";
    this.trigger.setAttribute("aria-expanded", "false");
    document.removeEventListener("pointerdown", this._outside, true);
    if (hadFocus) this.trigger.focus({ preventScroll: true });
    this.emit("close");
  }
  toggle() {
    this.popupOpen ? this.close() : this.open();
  }

  selectionLabel() {
    const arr = (this.values || []).map(String);
    if (!arr.length) return "";
    const byValue = new Map(this.resolved().map((o) => [o.value, o.label]));
    const labels = arr.map((v) => byValue.get(v) || v);
    if (labels.length <= 3) return labels.join(", ");
    return this.t("multiSelect.selectedCount", "%1 selected", arr.length.toLocaleString(this.lang || undefined));
  }

  update() {
    this.$(".label").textContent = this.label;
    const sel = this.selectionLabel();
    const value = this.$(".value");
    value.textContent = sel || this.triggerLabel || this.noSelectionText || this.t("common.noneSelected", "None selected");
    value.classList.toggle("none", !sel);
    this.search.placeholder = this.placeholder || this.t("common.search", "Search...");
    this.$(".refresh").textContent = this.loadingOptions ? "\u{F0996}" : "\u{F0450}";
    const box = this.trigger;
    const focus = !!this._focused;
    const hot = !!this._hot || this.cursor;
    box.dataset.fill = focus ? "focus" : hot ? "hover" : "normal";
    box.dataset.border = focus ? "focus" : hot ? "hover" : "normal";
    if (this.label) box.setAttribute("aria-label", this.label);
    if (this.popupOpen && !this._rendering) {
      this._rendering = true;
      this.filter();
      this.setCursor(this._cur);
      this._rendering = false;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// ColorWell: a bordered well with the color inset; a cursor ring outside it (sm away).

export class ColorWell extends OmarchyElement {
  static focusable = true;
  static props = { swatch: ["string", "var(--omarchy-accent)"], cursor: ["bool"] };
  static css = `${LOOK_CSS}
:host {
  --ww: ${space(44)}; --wh: ${space(26)}; --pad: var(--omarchy-space-sm);
  display: inline-block; position: relative; vertical-align: middle; flex: none;
  width: calc(var(--ww) + 2 * var(--pad)); height: calc(var(--wh) + 2 * var(--pad));
  cursor: pointer; outline: none;
}
.ring { position: absolute; inset: 0; display: none; border-radius: calc(min(var(--omarchy-radius), calc(var(--omarchy-radius-small) + var(--pad))) * var(--omarchy-rounded)); }
.ring.on { display: block; }
.well { position: absolute; left: var(--pad); top: var(--pad); width: var(--ww); height: var(--wh); border-radius: var(--omarchy-radius-small);
  padding: var(--omarchy-space-xxs); transition: transform var(--omarchy-motion-press) cubic-bezier(0.33, 1, 0.68, 1); }
.well.down { transform: scale(0.97); }
.color { width: 100%; height: 100%; border-radius: max(0px, calc(var(--omarchy-radius-small) - var(--omarchy-control-normal-border-top) - var(--omarchy-space-xxs))); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="ring box" data-border="hover"></div><div class="well box" data-fill="normal" data-border="normal"><div class="color"></div></div>`;
    this.setAttribute("role", "button");
    trackPointer(this);
    this.addEventListener("focus", () => ((this._focused = true), this.update()));
    clickOnKeys(this);
  }
  update() {
    this.$(".color").style.background = this.swatch;
    this.$(".ring").classList.toggle("on", !!this._hot || this.cursor || !!this._focused);
    this.$(".well").classList.toggle("down", !!this._pressed);
    if (!this.hasAttribute("aria-label")) this.setAttribute("aria-label", this.swatch);
  }
}

// ---------------------------------------------------------------------------------------------
// SwatchStrip: color chips xxs apart; a ring around the chosen one (accent) or under the cursor.

export class SwatchStrip extends OmarchyElement {
  static focusable = true;
  static props = { colors: ["json", () => []], currentIndex: ["number", -1], cursorIndex: ["number", -1] };
  static css = `
:host {
  --chip: ${space(22)}; --ring: max(1px, ${space(2)}); --gap: var(--omarchy-space-xxs);
  display: inline-flex; gap: var(--omarchy-space-xxs); vertical-align: middle; outline: none;
}
.cell { position: relative; width: calc(var(--chip) + 2 * (var(--gap) + var(--ring))); aspect-ratio: 1; cursor: pointer; flex: none; }
.ring { position: absolute; inset: 0; display: none; border: var(--ring) solid ${alpha("var(--omarchy-foreground)", "var(--omarchy-control-hover-border-alpha)")};
  border-radius: calc((var(--omarchy-radius-small) + var(--gap) + var(--ring)) * var(--omarchy-rounded)); }
.cell.hot .ring { display: block; border-color: var(--omarchy-control-hover-border); }
.cell.chosen .ring { display: block; border-color: var(--omarchy-accent); }
.chip { position: absolute; left: calc(var(--gap) + var(--ring)); top: calc(var(--gap) + var(--ring)); width: var(--chip); height: var(--chip);
  border: 1px solid var(--omarchy-track); border-radius: var(--omarchy-radius-small); box-sizing: border-box; background-clip: padding-box; }
`;
  setup() {
    this.setAttribute("role", "listbox");
    this._hover = -1;
    this.addEventListener("keydown", (e) => {
      const k = readingKey(e, true);
      const from = this.cursorIndex >= 0 ? this.cursorIndex : Math.max(0, this.currentIndex);
      if (k === "ArrowRight") this.cursorIndex = Math.min(this.colors.length - 1, from + 1);
      else if (k === "ArrowLeft") this.cursorIndex = Math.max(0, from - 1);
      else if (e.key === " " || e.key === "Enter") this.pick(from);
      else return;
      e.preventDefault();
    });
    this.addEventListener("blur", () => (this.cursorIndex = -1));
    this.shadowRoot.addEventListener("click", (e) => {
      const c = e.target.closest(".cell");
      if (c) this.pick(Number(c.dataset.i));
    });
    this.shadowRoot.addEventListener("pointerover", (e) => {
      const c = e.target.closest(".cell");
      this._hover = c ? Number(c.dataset.i) : -1;
      this.update();
    });
    this.shadowRoot.addEventListener("pointerleave", () => ((this._hover = -1), this.update()), true);
  }
  pick(i) {
    if (i < 0 || i >= this.colors.length) return;
    this.currentIndex = i;
    this.emit("pick", { index: i, color: this.colors[i] });
  }
  update() {
    const colors = this.colors;
    const key = JSON.stringify(colors);
    if (this._rendered !== key) {
      this._rendered = key;
      this.shadowRoot.innerHTML = colors
        .map((c, i) => `<div class="cell" role="option" data-i="${i}"><div class="ring"></div><div class="chip"></div></div>`)
        .join("");
      this.shadowRoot.querySelectorAll(".chip").forEach((el, i) => (el.style.backgroundColor = colors[i]));
    }
    this.shadowRoot.querySelectorAll(".cell").forEach((el, i) => {
      el.classList.toggle("chosen", i === this.currentIndex);
      el.classList.toggle("hot", i === this.cursorIndex || i === this._hover);
      el.setAttribute("aria-selected", String(i === this.currentIndex));
    });
  }
}

export const elements = {
  "omarchy-multi-select": MultiSelect,
  "omarchy-color-well": ColorWell,
  "omarchy-swatch-strip": SwatchStrip,
};
