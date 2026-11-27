// Navigation (spec/components/navigation.md): Menu, MenuItem, MenuSection, MenuSeparator,
// ContextMenu, Keycap, KeyCombo, ShortcutSheet, Toolbar, ToolbarSeparator, Tabs, IconRail. Drawn as
// qt/qml/Omarchy/Ui draws them (Menu.qml, MenuItem.qml, Keycap.qml, Toolbar.qml, Tabs.qml,
// IconRail.qml, …): the same metrics, states, keys and strings.

import { OmarchyElement, esc, readingKey, surfaceBorder } from "./base.js";
import { LOOK_CSS, space } from "./looks.js";
import { isOpen } from "./popup.js";

// ---------------------------------------------------------------------------------------------
// Glyphs centered by ink (Qt's OpticalGlyph): horizontally on the outline's middle, vertically on
// the line box rounded to whole points. A `.og` box holds one `<span>`; inkCenter(span) moves it.

const OG_CSS = `
.og { position: relative; flex: none; }
.og > span {
  position: absolute; left: 50%; white-space: pre;
  font-size: var(--f); line-height: var(--l, normal);
  top: round(calc((100% - var(--l, 1em)) / 2), 1px);
}`;

/** `--f` / `--l` for a font token (`icon`, `caption`, `icon-large`, …). */
const ogFont = (token) => `--f: var(--omarchy-font-${token}); --l: var(--omarchy-line-${token}, normal);`;

const inkCanvas = typeof document !== "undefined" ? document.createElement("canvas").getContext("2d") : null;
const inkCache = new Map();

/** Shift `span` (left: 50%) so its ink, not its advance, is centered (Qt OpticalGlyph). */
function inkCenter(span) {
  if (!inkCanvas || !span.isConnected) return;
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
    inkCanvas.font = `${cs.fontWeight} ${size * 100}px ${cs.fontFamily}`;
    const m = inkCanvas.measureText(text);
    off = (m.actualBoundingBoxRight - m.actualBoundingBoxLeft) / 200;
    inkCache.set(key, off);
  }
  span.style.marginLeft = `${-off}px`;
}

/** ⌈advance⌉ of `text` in `el`'s font at `token` size (Qt's TextMetrics.advanceWidth). */
function advance(el, text, token) {
  if (!inkCanvas || !el.isConnected) return 0;
  const cs = getComputedStyle(el);
  const size = parseFloat(cs.getPropertyValue(`--omarchy-font-${token}`)) || 10;
  inkCanvas.font = `${size}px ${cs.fontFamily}`;
  return Math.ceil(inkCanvas.measureText(text).width - 1e-6);
}

const px = (el, name) => parseFloat(getComputedStyle(el).getPropertyValue(name)) || 0;

// ---------------------------------------------------------------------------------------------
// Keycap and KeyCombo

const KEY_NAMES = {
  super: "Super", meta: "Super", win: "Super", cmd: "Super", logo: "Super",
  ctrl: "Ctrl", control: "Ctrl",
  alt: "Alt", option: "Alt",
  shift: "⇧",
  enter: "⏎", return: "⏎",
  backspace: "⌫",
  esc: "Esc", escape: "Esc",
  tab: "Tab", space: "Space",
  up: "↑", down: "↓", left: "←", right: "→",
  pageup: "PgUp", pagedown: "PgDn",
  delete: "Del", del: "Del",
  home: "Home", end: "End",
};

/** A key name as a Keycap draws it ("ctrl" → "Ctrl", "enter" → "⏎", "k" → "K"). */
export function keyLabel(k) {
  const s = String(k).trim();
  const n = KEY_NAMES[s.toLowerCase()];
  if (n !== undefined) return n;
  return s.length === 1 ? s.toUpperCase() : s;
}

/** "Ctrl+Shift+S" → ["Ctrl", "Shift", "S"]; a lone "+" is the plus key ("Ctrl++"). */
export function splitCombo(combo) {
  const parts = String(combo).split("+");
  const out = [];
  for (let i = 0; i < parts.length; ++i) {
    if (parts[i] !== "") out.push(parts[i].trim());
    else if (i + 1 < parts.length && parts[i + 1] === "") {
      out.push("+");
      ++i;
    }
  }
  return out;
}

export class Keycap extends OmarchyElement {
  static props = { key: ["string", ""], foreground: ["string", null] };
  static css = `${LOOK_CSS}${OG_CSS}
:host { display: inline-block; vertical-align: middle; position: relative; flex: none;
  height: ${space(18)}; min-width: ${space(18)}; color: var(--ink, var(--omarchy-foreground)); }
.box { position: absolute; inset: 0; border-radius: var(--omarchy-radius-small); transition: none; }
.og { position: absolute; inset: 0; ${ogFont("caption")} }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="box" data-fill="normal" data-border="normal"></div><div class="og"><span></span></div>`;
  }
  get label() {
    return keyLabel(this.key);
  }
  update() {
    const span = this.$(".og > span");
    span.textContent = this.label;
    if (this.foreground) this.style.setProperty("--ink", this.foreground);
    else this.style.removeProperty("--ink");
    const box = this.$(".box");
    box.classList.toggle("ink", !!this.foreground);
    const cs = getComputedStyle(box);
    const borders = (parseFloat(cs.borderLeftWidth) || 0) + (parseFloat(cs.borderRightWidth) || 0);
    const w = advance(this, this.label, "caption") + 2 * px(this, "--omarchy-space-sm") + borders;
    this.style.width = `${Math.max(this.offsetHeight || 18, w)}px`;
    inkCenter(span);
  }
}

export class KeyCombo extends OmarchyElement {
  static props = { keys: ["string", ""] };
  static css = `:host { display: inline-flex; align-items: flex-start; gap: var(--omarchy-space-xxs); vertical-align: middle; flex: none; }`;
  update() {
    const keys = splitCombo(this.keys);
    if (this._rendered !== this.keys) {
      this._rendered = this.keys;
      this.shadowRoot.innerHTML = keys.map((k) => `<omarchy-keycap key="${esc(k)}"></omarchy-keycap>`).join("");
    }
    this.setAttribute("aria-label", keys.join("+"));
  }
}

// ---------------------------------------------------------------------------------------------
// ShortcutSheet: groups filling `columns` columns, left to right in turn.

export class ShortcutSheet extends OmarchyElement {
  static props = { groups: ["json", () => []], columns: ["number", 2] };
  static css = `
:host { display: flex; align-items: flex-start; gap: calc(var(--omarchy-space-panel-gap) * 2); }
.column { display: flex; flex-direction: column; align-items: flex-start; gap: var(--omarchy-space-panel-gap); }
.group { display: flex; flex-direction: column; align-items: flex-start; gap: var(--omarchy-space-md); }
.grid { display: grid; grid-template-columns: max-content max-content; column-gap: var(--omarchy-space-xxl); row-gap: var(--omarchy-space-md); align-items: center; justify-items: start; }
.desc { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-foreground); white-space: pre; }
`;
  update() {
    const groups = this.groups || [];
    const cols = Math.max(1, Math.min(this.columns, groups.length));
    let html = "";
    for (let c = 0; c < cols; c++) {
      html += `<div class="column">`;
      groups.forEach((g, i) => {
        if (i % this.columns !== c) return;
        html += `<div class="group"><omarchy-section-header text="${esc(g.title || "")}"></omarchy-section-header><div class="grid">`;
        for (const [combo, desc] of g.keys || []) {
          html += `<omarchy-key-combo keys="${esc(combo)}"></omarchy-key-combo><span class="desc">${esc(desc)}</span>`;
        }
        html += `</div></div>`;
      });
      html += `</div>`;
    }
    if (html !== this._html) {
      this._html = html;
      this.shadowRoot.innerHTML = html;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Menu, MenuItem, MenuSeparator, MenuSection, ContextMenu

export class MenuItem extends OmarchyElement {
  static props = {
    text: ["string", ""],
    icon: ["string", ""],
    shortcut: ["string", ""],
    checkable: ["bool"],
    checked: ["bool"],
    exclusive: ["bool"],
    destructive: ["bool"],
    disabled: ["bool"],
    hasSubmenu: ["bool"],
    cursor: ["bool"],
  };
  static css = `${OG_CSS}
:host { display: block; outline: none; }
:host([hidden]) { display: none; }
.row {
  position: relative; display: flex; align-items: center;
  height: var(--omarchy-space-popup-row-height); padding: 0 var(--omarchy-space-control-padding-x);
  border-radius: var(--omarchy-radius-small); cursor: pointer; color: var(--ink);
  --ink: var(--omarchy-menu-text);
  transition: background-color var(--omarchy-motion-fast);
}
.row.hot { background: var(--omarchy-menu-selected-background); --ink: var(--omarchy-menu-selected-text); }
.row.down { background: var(--omarchy-control-pressed-fill); --ink: var(--omarchy-menu-selected-text); }
:host([destructive]) .row { --ink: var(--omarchy-urgent); }
:host([disabled]) .row { --ink: var(--omarchy-text-faint); cursor: default; }
.icon { width: var(--omarchy-font-icon); height: 100%; margin-right: var(--omarchy-space-control-gap); ${ogFont("icon")} }
:host(:not([reserve-icon])) .icon { display: none; }
.label { flex: none; font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre; }
.space { flex: 1; min-width: 0; }
.hint { flex: none; margin-left: var(--omarchy-space-huge); font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, normal); white-space: pre; color: var(--omarchy-text-dim); }
:host([disabled]) .hint { color: var(--omarchy-text-faint); }
.hint:empty { display: none; }
.mark { width: var(--omarchy-font-icon); height: 100%; margin-left: var(--omarchy-space-control-gap); color: var(--omarchy-text-dim); ${ogFont("icon-small")} }
.mark.check { color: var(--omarchy-accent); ${ogFont("body")} }
.mark.dot { color: var(--omarchy-accent); ${ogFont("caption")} }
:host([disabled]) .mark { color: var(--omarchy-text-faint); }
:host(:not([reserve-mark])) .mark { display: none; }
.sub { display: contents; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="row" part="row"><div class="og icon"><span></span></div><span class="label" part="label"></span><span class="space"></span><span class="hint" part="hint"></span><div class="og mark"><span></span></div></div><div class="sub"><slot></slot></div>`;
    this.setAttribute("role", "menuitem");
    const row = this.$(".row");
    row.addEventListener("pointerenter", () => {
      this._hover = true;
      const m = this.menu;
      if (m && !this.disabled) m.currentIndex = m.rows.indexOf(this);
      this.update();
    });
    row.addEventListener("pointerleave", () => {
      this._hover = false;
      this._down = false;
      this.update();
    });
    row.addEventListener("pointerdown", (e) => {
      if (e.button !== 0 || this.disabled) return;
      e.preventDefault(); // keep the keys on the menu
      this._down = true;
      this.update();
    });
    row.addEventListener("pointerup", () => {
      this._down = false;
      this.update();
    });
    row.addEventListener("click", (e) => {
      e.stopPropagation();
      this.activate();
    });
  }
  /** The Menu this row is in. */
  get menu() {
    const p = this.parentElement;
    return p && p instanceof Menu ? p : null;
  }
  /** Its submenu, an <omarchy-menu> child. */
  get submenu() {
    return [...this.children].find((c) => c instanceof Menu) || null;
  }
  get isSubmenuRow() {
    return this.hasSubmenu || !!this.submenu;
  }
  get highlighted() {
    const m = this.menu;
    return !!m && m.currentItem === this;
  }
  get hot() {
    return !this.disabled && (!!this._hover || this.cursor || this.highlighted);
  }
  /** Run the row (Qt's activate()): a submenu row opens its submenu; a checkable row flips. */
  activate() {
    if (this.disabled) return;
    const m = this.menu;
    if (this.isSubmenuRow) {
      if (m) m.openSubmenu(this);
      return;
    }
    if (this.checkable) {
      if (this.exclusive) {
        if (m) for (const r of m.rows) if (r !== this && r.exclusive) r.checked = false;
        this.checked = true;
      } else this.checked = !this.checked;
    }
    this.emit("trigger", { text: this.text, checked: this.checked });
    if (m) m.itemTriggered(this);
  }
  update() {
    const m = this.menu;
    const reserveIcon = m ? m.hasIcons : !!this.icon;
    const reserveMark = m ? m.hasMarks : this.checkable || this.isSubmenuRow;
    this.toggleAttribute("reserve-icon", reserveIcon);
    this.toggleAttribute("reserve-mark", reserveMark);
    const iconSpan = this.$(".icon > span");
    iconSpan.textContent = this.icon;
    this.$(".label").textContent = this.text;
    this.$(".hint").textContent = this.shortcut;
    const markBox = this.$(".mark");
    const mark = this.checkable && this.checked;
    markBox.className = `og mark${mark ? (this.exclusive ? " dot" : " check") : ""}`;
    const markSpan = markBox.firstElementChild;
    markSpan.textContent = mark ? (this.exclusive ? "●" : "✓") : this.isSubmenuRow ? "\u{F054}" : "";
    const row = this.$(".row");
    row.classList.toggle("hot", this.hot);
    row.classList.toggle("down", !!this._down && !this.disabled);
    this.setAttribute("role", this.checkable ? (this.exclusive ? "menuitemradio" : "menuitemcheckbox") : "menuitem");
    if (this.checkable) this.setAttribute("aria-checked", String(this.checked));
    else this.removeAttribute("aria-checked");
    this.setAttribute("aria-disabled", String(this.disabled));
    if (this.isSubmenuRow) this.setAttribute("aria-haspopup", "menu");
    if (iconSpan.isConnected) {
      inkCenter(iconSpan);
      inkCenter(markSpan);
    }
    if (m && !this._inRefresh) m._rowsChanged();
  }
}

export class MenuSeparator extends OmarchyElement {
  static css = `
:host { display: block; position: relative; height: calc(var(--omarchy-space-sm) * 2 + 1px); }
:host::after { content: ""; position: absolute; left: 0; right: 0; top: var(--omarchy-space-sm); height: 1px; background: var(--omarchy-track); }`;
  setup() {
    this.setAttribute("role", "separator");
  }
}

export class MenuSection extends OmarchyElement {
  static props = { text: ["string", ""] };
  static css = `
/* A flex row, not a line box: the header sits at its own height, with no strut of the menu's font
   under it (whose rounded ascent and descent differ between engines). */
:host { display: flex; padding: var(--omarchy-space-sm) var(--omarchy-space-control-padding-x) var(--omarchy-space-xxs); }`;
  setup() {
    this.shadowRoot.innerHTML = `<omarchy-section-header></omarchy-section-header>`;
    this.setAttribute("role", "presentation");
  }
  update() {
    this.$("omarchy-section-header").setAttribute("text", this.text);
  }
}

export class Menu extends OmarchyElement {
  static props = { held: ["bool"], minimumWidth: ["number", 0], currentIndex: ["number", -1] };
  static css = `
:host {
  box-sizing: border-box; display: inline-block; vertical-align: top; outline: none; position: relative;
  background: var(--omarchy-menu-background); color: var(--omarchy-menu-text);
  ${surfaceBorder("menu")}
  border-radius: var(--omarchy-radius);
  padding: var(--omarchy-space-sm);
  font-family: var(--omarchy-font-family);
}
:host([popover]) { position: fixed; inset: auto; margin: 0; overflow: visible; }
:host([popover]:not(:popover-open)) { display: none; }
.column { display: flex; flex-direction: column; min-width: var(--min-width, ${space(160)}); width: max-content; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="gradient-ring"></div><div class="column" role="none"><slot></slot></div>`;
    this.setAttribute("role", "menu");
    this.parentMenu = null;
    this._opened = false;
    this.$("slot").addEventListener("slotchange", () => this._rowsChanged());
    this.addEventListener("keydown", (e) => {
      if (e.target !== this) return;
      if (this.handleKey(e)) {
        e.preventDefault();
        e.stopPropagation();
      }
    });
    this.addEventListener("pointerleave", () => {
      // The keyboard cursor stays where the pointer left it, as Qt's (currentIndex set on enter).
    });
    this._outside = (e) => {
      const path = e.composedPath();
      for (let m = this; m; m = m._openSub || null) if (path.includes(m)) return;
      this.close();
    };
  }
  /** The visible MenuItems, in order. */
  get rows() {
    return [...this.children].filter((c) => c instanceof MenuItem && !c.hidden);
  }
  get currentItem() {
    const r = this.rows;
    return this.currentIndex >= 0 && this.currentIndex < r.length ? r[this.currentIndex] : null;
  }
  get hasIcons() {
    return this.rows.some((r) => r.icon !== "");
  }
  get hasMarks() {
    return this.rows.some((r) => r.checkable || r.isSubmenuRow);
  }
  /** Open, or drawn open in place (`held`). */
  get opened() {
    return this.held ? true : isOpen(this);
  }
  _rowsChanged() {
    if (this._refreshQueued) return;
    this._refreshQueued = true;
    queueMicrotask(() => {
      this._refreshQueued = false;
      for (const r of this.rows) {
        r._inRefresh = true;
        r.update();
        r._inRefresh = false;
      }
    });
  }
  /** Open below `trigger` (an element, or the element whose id the `trigger` attribute names), `xs` away. */
  open(trigger) {
    const t = trigger || this.trigger || (this.getAttribute("trigger") && document.getElementById(this.getAttribute("trigger")));
    this.show();
    if (t) {
      const r = t.getBoundingClientRect();
      this.place(r.left, r.bottom + px(this, "--omarchy-space-xs"));
    }
  }
  /** Open at a point in the viewport, kept inside it. */
  popup(x, y) {
    this.show();
    this.place(x, y);
  }
  place(x, y) {
    const w = this.offsetWidth;
    const h = this.offsetHeight;
    const vw = document.documentElement.clientWidth;
    const vh = document.documentElement.clientHeight;
    this.style.left = `${Math.round(Math.max(0, Math.min(x, vw - w)))}px`;
    this.style.top = `${Math.round(Math.max(0, Math.min(y, vh - h)))}px`;
  }
  show() {
    this.currentIndex = -1;
    if (!this.held) {
      if (!this.hasAttribute("popover")) this.popover = "manual";
      if (!isOpen(this)) this.showPopover();
      if (!this.parentMenu) document.addEventListener("pointerdown", this._outside, true);
    }
    if (!this.hasAttribute("tabindex")) this.tabIndex = -1;
    this.focus({ preventScroll: true });
    this.update();
  }
  close() {
    this.closeSubmenus();
    if (this.held || !isOpen(this)) return;
    this.hidePopover();
    this.currentIndex = -1;
    document.removeEventListener("pointerdown", this._outside, true);
    this.emit("close");
  }
  closeSubmenus() {
    for (const r of this.rows) {
      const s = r.submenu;
      if (s && s.opened && !s.held) s.close();
    }
    this._openSub = null;
  }
  closeAll() {
    let m = this;
    while (m.parentMenu) m = m.parentMenu;
    if (!m.held) m.close();
    else {
      m.closeSubmenus();
      m.focus({ preventScroll: true });
    }
  }
  move(step) {
    const rows = this.rows;
    const n = rows.length;
    if (!n) return;
    let i = this.currentIndex < 0 ? (step > 0 ? -1 : n) : this.currentIndex;
    for (let tries = 0; tries < n; ++tries) {
      i = (i + step + n) % n;
      if (!rows[i].disabled) {
        this.currentIndex = i;
        return;
      }
    }
  }
  jump(toEnd) {
    this.currentIndex = -1;
    this.move(toEnd ? -1 : 1);
  }
  openSubmenu(item) {
    const sub = item && item.submenu;
    if (!sub) return;
    this.closeSubmenus();
    this.currentIndex = this.rows.indexOf(item);
    sub.parentMenu = this;
    this._openSub = sub;
    sub.show();
    const r = item.getBoundingClientRect();
    const right = px(this, "--omarchy-space-sm") + (parseFloat(getComputedStyle(this).borderRightWidth) || 0);
    const subTop = px(sub, "--omarchy-space-sm") + (parseFloat(getComputedStyle(sub).borderTopWidth) || 0);
    sub.place(r.right + right, r.top - subTop);
    sub.move(1);
  }
  itemTriggered(item) {
    let m = this;
    while (m.parentMenu) m = m.parentMenu;
    m.emit("trigger", { item, text: item.text, checked: item.checked });
    this.closeAll();
  }
  /** The menu's keys (Qt's handleKey), for a panel's own key catcher too. True when it used it. */
  handleKey(e) {
    const k = e.key;
    if (k === "ArrowDown" || k === "j") this.move(1);
    else if (k === "ArrowUp" || k === "k") this.move(-1);
    else if (k === "Home") this.jump(false);
    else if (k === "End") this.jump(true);
    else if (k === "Enter" || k === " ") {
      if (this.currentItem) this.currentItem.activate();
    } else if (k === "ArrowRight" || k === "l") {
      if (this.currentItem && this.currentItem.isSubmenuRow) this.openSubmenu(this.currentItem);
    } else if (k === "ArrowLeft" || k === "h" || k === "Escape") {
      if (this.parentMenu) {
        const back = this.parentMenu;
        this.close();
        back._openSub = null;
        back.focus({ preventScroll: true });
      } else if (k === "Escape") this.close();
    } else return false;
    return true;
  }
  update() {
    if (this.held && this.hasAttribute("popover")) {
      if (isOpen(this)) this.hidePopover();
      this.removeAttribute("popover");
    }
    if (this.held && !this.hasAttribute("tabindex")) this.tabIndex = 0;
    if (this.minimumWidth > 0) this.style.setProperty("--min-width", `${this.minimumWidth}px`);
    else this.style.removeProperty("--min-width");
    if (this._lastIndex !== this.currentIndex) {
      this._lastIndex = this.currentIndex;
      this._rowsChanged();
    }
  }
}

/** A Menu that opens at the pointer on a right-click in its `target` (an element, or the `for` id). */
export class ContextMenu extends Menu {
  static props = { ...Menu.props, for: ["string", ""] };
  connectedCallback() {
    super.connectedCallback();
    this._bind();
  }
  get target() {
    return this._target || (this.for ? document.getElementById(this.for) : null);
  }
  set target(el) {
    this._target = el;
    this._bind();
  }
  _bind() {
    const t = this.target;
    if (t === this._bound) return;
    if (this._bound) this._bound.removeEventListener("contextmenu", this._onContext);
    this._onContext ||= (e) => {
      e.preventDefault();
      this.popup(e.clientX, e.clientY);
    };
    if (t) t.addEventListener("contextmenu", this._onContext);
    this._bound = t;
  }
}

// ---------------------------------------------------------------------------------------------
// Toolbar and ToolbarSeparator

export class Toolbar extends OmarchyElement {
  static props = { overflow: ["bool"], overflowCursor: ["bool"], divided: ["string", "true"] };
  static css = `
:host { display: block; position: relative; box-sizing: border-box; padding: var(--omarchy-space-sm); }
:host(:not([divided="false"])) { padding-bottom: calc(var(--omarchy-space-sm) + 1px); }
.bar { display: grid; grid-template-columns: minmax(0, 1fr) auto minmax(0, 1fr); align-items: center; }
.slot { display: flex; align-items: center; gap: var(--omarchy-space-xxs); }
.leading { justify-self: start; }
.end { justify-self: end; }
.rule { position: absolute; left: 0; right: 0; bottom: 0; height: 1px; background: var(--omarchy-track); }
:host([divided="false"]) .rule { display: none; }
::slotted(*) { flex: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="bar"><div class="slot leading"><slot></slot></div><div class="slot center"><slot name="center"></slot></div><div class="slot end"><slot name="trailing"></slot><omarchy-icon-button class="more" icon="\u{F141}"></omarchy-icon-button></div></div><div class="rule"></div>`;
    this.setAttribute("role", "toolbar");
    this.$(".more").addEventListener("click", (e) => {
      e.stopPropagation();
      this.emit("overflow");
    });
  }
  update() {
    const more = this.$(".more");
    more.hidden = !this.overflow;
    more.toggleAttribute("cursor", this.overflowCursor);
  }
}

export class ToolbarSeparator extends OmarchyElement {
  static css = `
:host { display: block; position: relative; flex: none; width: calc(var(--omarchy-space-sm) * 2 + 1px); height: ${space(16)}; }
:host::after { content: ""; position: absolute; top: 0; bottom: 0; left: var(--omarchy-space-sm); width: 1px; background: var(--omarchy-track); }`;
  setup() {
    this.setAttribute("role", "separator");
    this.setAttribute("aria-orientation", "vertical");
  }
}

// ---------------------------------------------------------------------------------------------
// Tabs and IconRail: a choice of pages; the selection moves with the keys.

/** The shared behavior: currentIndex / cursorIndex, `select(i)` (clamped; emits `activate` when it
 *  changes), click to choose, keys from `keymap`. */
class Chooser extends OmarchyElement {
  static focusable = true;
  static props = { model: ["json", () => []], currentIndex: ["number", 0], cursorIndex: ["number", -1] };
  get count() {
    return (this.model || []).length;
  }
  select(i) {
    const n = this.count;
    if (!n) return;
    i = Math.max(0, Math.min(n - 1, i));
    if (i === this.currentIndex) return;
    this.currentIndex = i;
    this.emit("activate", { index: i });
  }
  wire(keymap) {
    this.setAttribute("role", "tablist");
    this.addEventListener("keydown", (e) => {
      const step = keymap(e);
      if (step === undefined) return;
      e.preventDefault();
      if (step === "home") this.select(0);
      else if (step === "end") this.select(this.count - 1);
      else this.select(this.currentIndex + step);
    });
    this.addEventListener("focus", () => this.update());
    this.addEventListener("blur", () => this.update());
    this.shadowRoot.addEventListener("click", (e) => {
      const it = e.target.closest("[data-i]");
      if (!it) return;
      this.focus({ preventScroll: true });
      this.select(Number(it.dataset.i));
    });
    const press = (on) => (e) => {
      const it = e.target.closest && e.target.closest("[data-i]");
      if (it) it.classList.toggle("down", on && e.button === 0);
    };
    this.shadowRoot.addEventListener("pointerdown", press(true));
    this.shadowRoot.addEventListener("pointerup", press(false));
    this.shadowRoot.addEventListener("pointerout", press(false));
  }
  /** Whether item `i` shows the cursor look (pointer, cursorIndex, or focus on the selection). */
  hotAt(i) {
    return i === this.cursorIndex || (this.matches(":focus") && i === this.currentIndex);
  }
}

export class Tabs extends Chooser {
  static css = `
:host { display: block; position: relative; outline: none; }
.row { position: relative; display: inline-flex; gap: var(--omarchy-space-xxs); }
.tab {
  flex: none; padding: var(--omarchy-space-control-padding-y) var(--omarchy-space-control-padding-x);
  border-radius: var(--omarchy-radius-small) var(--omarchy-radius-small) 0 0; cursor: pointer; user-select: none; -webkit-user-select: none;
  font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre;
  color: var(--omarchy-text-dim); transition: background-color var(--omarchy-motion-fast);
}
.tab:hover, .tab.hot { background: var(--omarchy-control-hover-fill); color: var(--omarchy-foreground); }
.tab.down { background: var(--omarchy-control-pressed-fill); }
.tab.sel { font-weight: bold; color: var(--omarchy-control-selected-text); }
.rule { position: absolute; left: 0; right: 0; bottom: 0; height: 1px; background: var(--omarchy-track); }
.bar { position: absolute; bottom: 0; height: var(--omarchy-space-xxs); background: var(--omarchy-accent);
  transition: left var(--omarchy-motion-press) var(--omarchy-ease-out), width var(--omarchy-motion-press) var(--omarchy-ease-out); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="rule"></div><div class="row"></div><div class="bar"></div>`;
    this.wire((e) => {
      const k = readingKey(e, true);
      if (k === "ArrowLeft") return -1;
      if (k === "ArrowRight") return 1;
      if (e.key === "Home") return "home";
      if (e.key === "End") return "end";
      return undefined;
    });
  }
  update() {
    const row = this.$(".row");
    const labels = (this.model || []).map(String);
    if (this._rendered !== JSON.stringify(labels)) {
      this._rendered = JSON.stringify(labels);
      row.innerHTML = labels.map((l, i) => `<div class="tab" role="tab" data-i="${i}">${esc(l)}</div>`).join("");
    }
    const tabs = [...row.children];
    tabs.forEach((t, i) => {
      t.classList.toggle("sel", i === this.currentIndex);
      t.classList.toggle("hot", this.hotAt(i));
      t.setAttribute("aria-selected", String(i === this.currentIndex));
    });
    const bar = this.$(".bar");
    const cur = tabs[this.currentIndex];
    bar.hidden = !cur;
    if (cur) {
      bar.style.left = `${cur.offsetLeft}px`;
      bar.style.width = `${cur.offsetWidth}px`;
    }
  }
}

export class IconRail extends Chooser {
  static css = `${OG_CSS}
:host {
  --item: calc(var(--omarchy-space-control-height) + 2 * var(--omarchy-space-sm));
  display: inline-block; position: relative; box-sizing: border-box; outline: none; vertical-align: top;
  width: calc(var(--item) + 2 * var(--omarchy-space-sm) + 1px); padding: var(--omarchy-space-sm);
}
.column { display: flex; flex-direction: column; gap: var(--omarchy-space-sm); }
.item { width: var(--item); height: var(--item); border-radius: var(--omarchy-radius); cursor: pointer;
  color: var(--omarchy-text-dim); transition: background-color var(--omarchy-motion-fast); ${ogFont("icon-large")} }
.item:hover, .item.hot { background: var(--omarchy-control-hover-fill); color: var(--omarchy-foreground); }
.item.down { background: var(--omarchy-control-pressed-fill); }
.item.sel { color: var(--omarchy-accent); }
.rule { position: absolute; right: 0; top: 0; bottom: 0; width: 1px; background: var(--omarchy-track); }
.bar { position: absolute; left: 0; width: var(--omarchy-space-xxs); height: var(--omarchy-font-icon-large);
  border-radius: min(1px, var(--omarchy-radius)); background: var(--omarchy-accent);
  transition: top var(--omarchy-motion-press) var(--omarchy-ease-out); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="rule"></div><div class="column"></div><div class="bar"></div>`;
    this.wire((e) => {
      const k = readingKey(e, true);
      if (k === "ArrowUp") return -1;
      if (k === "ArrowDown") return 1;
      if (e.key === "Home") return "home";
      if (e.key === "End") return "end";
      return undefined;
    });
  }
  get itemSize() {
    return px(this, "--omarchy-space-control-height") + 2 * px(this, "--omarchy-space-sm");
  }
  update() {
    const col = this.$(".column");
    const items = (this.model || []).map((m) => (m && typeof m === "object" ? m : { icon: String(m), label: String(m) }));
    const key = JSON.stringify(items);
    if (this._rendered !== key) {
      this._rendered = key;
      col.innerHTML = items
        .map((m, i) => `<div class="item og" role="tab" data-i="${i}" title="" aria-label="${esc(m.label || "")}"><span>${esc(m.icon || "")}</span></div>`)
        .join("");
    }
    const els = [...col.children];
    els.forEach((el, i) => {
      el.classList.toggle("sel", i === this.currentIndex);
      el.classList.toggle("hot", this.hotAt(i));
      el.setAttribute("aria-selected", String(i === this.currentIndex));
      inkCenter(el.firstElementChild);
    });
    const bar = this.$(".bar");
    const cur = els[this.currentIndex];
    bar.hidden = !cur;
    if (cur) {
      const h = px(this, "--omarchy-font-icon-large");
      bar.style.top = `${cur.offsetTop + Math.round((cur.offsetHeight - h) / 2)}px`;
    }
  }
}

export const elements = {
  "omarchy-keycap": Keycap,
  "omarchy-key-combo": KeyCombo,
  "omarchy-shortcut-sheet": ShortcutSheet,
  "omarchy-menu-item": MenuItem,
  "omarchy-menu-separator": MenuSeparator,
  "omarchy-menu-section": MenuSection,
  "omarchy-menu": Menu,
  "omarchy-context-menu": ContextMenu,
  "omarchy-toolbar": Toolbar,
  "omarchy-toolbar-separator": ToolbarSeparator,
  "omarchy-tabs": Tabs,
  "omarchy-icon-rail": IconRail,
};
