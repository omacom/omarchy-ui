// Surfaces and the words on them (spec/components.md, spec/components/surfaces.md): Card,
// SectionHeader, Separator, MenuRow, Tooltip, ConfirmDialog, Pane, Heading. As Qt draws them:
// qt/qml/Omarchy/Ui/{Card,PanelSectionHeader,PanelSeparator,PopupMenuRow,Tooltip,ConfirmDialog,Pane,Heading}.qml.

import { OmarchyElement, readingKey, surfaceBorder } from "./base.js";
import { LOOK_CSS, alpha, space } from "./looks.js";
import { isOpen, place } from "./popup.js";

// ---------------------------------------------------------------------------------------------
// SectionHeader: a caption, bold, textDim.

export class SectionHeader extends OmarchyElement {
  static props = { text: ["string", ""] };
  static css = `
:host { display: block; color: var(--omarchy-text-dim); font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); font-weight: bold;
  padding-top: round(up, calc(var(--omarchy-font-caption) * 0.15), 1px); white-space: pre; }`;
  setup() {
    this.shadowRoot.innerHTML = `<span id="t"></span><slot></slot>`;
    this.setAttribute("role", "heading");
    this.setAttribute("aria-level", "3");
  }
  update() {
    this.$("#t").textContent = this.text;
  }
}

// ---------------------------------------------------------------------------------------------
// Separator: 1 px of foreground at 12% (the track role).

export class Separator extends OmarchyElement {
  static props = { vertical: ["bool"] };
  static css = `
:host { display: block; height: 1px; min-height: 1px; background: var(--omarchy-track); align-self: stretch; flex: none; }
:host([vertical]) { width: 1px; height: auto; min-height: 0; }`;
  setup() {
    this.setAttribute("role", "separator");
  }
}

// ---------------------------------------------------------------------------------------------
// Card: the popups surface panels and flyouts are drawn on.

export class Card extends OmarchyElement {
  static props = { title: ["string", ""] };
  static css = `
:host {
  display: flex; flex-direction: column; gap: var(--omarchy-space-panel-gap);
  position: relative; box-sizing: border-box;
  background: var(--omarchy-popups-background); color: var(--omarchy-popups-text);
  ${surfaceBorder("popups")}
  border-radius: var(--omarchy-radius);
  padding: var(--omarchy-space-panel-padding);
}
omarchy-section-header:not([text]), omarchy-section-header[text=""] { display: none; }
.body { display: flex; flex-direction: column; align-items: stretch; gap: var(--omarchy-space-panel-gap); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="gradient-ring"></div><omarchy-section-header></omarchy-section-header><div class="body"><slot></slot></div>`;
  }
  update() {
    this.$("omarchy-section-header").setAttribute("text", this.title);
  }
}

// ---------------------------------------------------------------------------------------------
// MenuRow (Qt's PopupMenuRow): a list/menu row; accent check when checked, hover fill when highlighted.

export class MenuRow extends OmarchyElement {
  static props = { text: ["string", ""], checked: ["bool"], highlighted: ["bool"] };
  static css = `
:host {
  display: flex; align-items: center; justify-content: space-between; align-self: stretch;
  height: var(--omarchy-space-popup-row-height); padding: 0 var(--omarchy-space-control-padding-x);
  border-radius: var(--omarchy-radius); cursor: pointer; outline: none;
  color: var(--omarchy-popups-text); font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal);
}
:host(:hover), :host([highlighted]), :host(:focus-visible) { background: var(--omarchy-control-hover-fill); }
.t { white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.check { color: var(--omarchy-accent); }
:host(:not([checked])) .check { display: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<span class="t"></span><span class="check">✓</span>`;
    this.setAttribute("role", "menuitemcheckbox");
    if (!this.hasAttribute("tabindex")) this.tabIndex = -1;
    this.addEventListener("click", () => this.emit("trigger"));
    this.addEventListener("keydown", (e) => {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        this.click();
      }
    });
  }
  update() {
    this.$(".t").textContent = this.text;
    this.setAttribute("aria-checked", String(this.checked));
  }
}

// ---------------------------------------------------------------------------------------------
// Tooltip: the tooltip surface, bodySmall, with an optional shortcut (a KeyCombo). `held` draws it
// in place (a still, an onboarding hint); otherwise it shows over its parent element on hover or
// keyboard focus, after 400 ms, centered above it.

export class Tooltip extends OmarchyElement {
  static props = { text: ["string", ""], shortcut: ["string", ""], held: ["bool"] };
  static css = `
:host { display: none; }
:host([held]) { display: inline-flex; }
.tip {
  position: relative; box-sizing: border-box; display: inline-flex; align-items: center; gap: var(--omarchy-space-control-gap);
  background: var(--omarchy-tooltip-background); color: var(--omarchy-tooltip-text);
  ${surfaceBorder("tooltip")}
  border-radius: var(--omarchy-radius);
  padding: var(--omarchy-space-control-padding-y) var(--omarchy-space-control-padding-x);
  font-family: var(--omarchy-font-family); font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, normal); white-space: pre;
}
.tip[popover] { position: fixed; inset: auto; margin: 0; pointer-events: none; opacity: 1; transition: opacity var(--omarchy-motion-fast); }
.tip[popover]:not(:popover-open) { display: none; }
omarchy-key-combo:not([keys]), omarchy-key-combo[keys=""] { display: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="tip" role="tooltip"><div class="gradient-ring"></div><span class="t"></span><omarchy-key-combo></omarchy-key-combo></div>`;
    this._anchor = null;
    this._timer = 0;
    this._show = () => {
      if (this.held) return;
      clearTimeout(this._timer);
      this._timer = setTimeout(() => this.open(), 400);
    };
    this._hide = () => {
      clearTimeout(this._timer);
      this.close();
    };
  }
  connectedCallback() {
    super.connectedCallback();
    const a = this.parentElement || (this.getRootNode() && this.getRootNode().host);
    if (a && a !== this._anchor) {
      this._anchor = a;
      a.addEventListener("pointerenter", this._show);
      a.addEventListener("pointerleave", this._hide);
      a.addEventListener("focusin", this._show);
      a.addEventListener("focusout", this._hide);
      a.addEventListener("pointerdown", this._hide);
    }
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    const a = this._anchor;
    if (a) {
      a.removeEventListener("pointerenter", this._show);
      a.removeEventListener("pointerleave", this._hide);
      a.removeEventListener("focusin", this._show);
      a.removeEventListener("focusout", this._hide);
      a.removeEventListener("pointerdown", this._hide);
    }
    this._anchor = null;
  }
  open() {
    const tip = this.$(".tip");
    if (this.held || !this._anchor) return;
    this.style.display = "contents";
    tip.popover = "manual";
    tip.showPopover();
    place(tip, this._anchor, { side: "above", align: "center", gap: Number.parseFloat(getComputedStyle(this).getPropertyValue("--omarchy-space-sm")) || 4 });
  }
  close() {
    const tip = this.$(".tip");
    if (tip.popover && isOpen(tip)) tip.hidePopover();
    this.style.display = "";
  }
  update() {
    this.$(".t").textContent = this.text;
    const kc = this.$("omarchy-key-combo");
    if (this.shortcut) kc.setAttribute("keys", this.shortcut);
    else kc.removeAttribute("keys");
    if (this.held) {
      const tip = this.$(".tip");
      if (tip.popover) {
        if (isOpen(tip)) tip.hidePopover();
        tip.removeAttribute("popover");
      }
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Dialog and ConfirmDialog: a modal card on the scrim. ConfirmDialog is the Dialog with no title,
// the message at `title` size and a destructive Confirm (Qt's ConfirmDialog draws the same card).

export const DIALOG_CSS = `
:host { position: absolute; inset: 0; display: none; z-index: 10; outline: none; }
:host([open]) { display: block; }
.scrim { position: absolute; inset: 0; background: var(--omarchy-scrim); }
.card {
  position: absolute; left: 50%; top: 50%; transform: translate(-50%, -50%);
  width: min(calc(100% - ${space(32)}), ${space(370)});
  box-sizing: border-box; padding: ${space(18)};
  background: var(--omarchy-background); color: var(--omarchy-foreground);
  border: var(--omarchy-control-normal-border-top) solid var(--omarchy-menu-selected-text);
  border-radius: var(--omarchy-radius);
  display: flex; flex-direction: column;
}
.title { font-size: var(--omarchy-font-title); line-height: var(--omarchy-line-title, normal); font-weight: bold; }
.message { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); overflow-wrap: anywhere; white-space: pre-wrap; }
.title:empty, .message:empty { display: none; }
.lines { display: flex; flex-direction: column; gap: ${space(10)}; }
.buttons { display: flex; justify-content: flex-end; gap: ${space(10)}; margin-top: ${space(20)}; }
.btn {
  min-width: ${space(88)}; height: ${space(34)}; padding-inline: var(--omarchy-space-control-padding-x); box-sizing: border-box; display: flex; align-items: center; justify-content: center;
  border: var(--omarchy-control-normal-border-top) solid ${alpha("var(--omarchy-foreground)", 0.38)};
  border-radius: var(--omarchy-radius);
  background: transparent; color: var(--omarchy-foreground); cursor: pointer;
  font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); white-space: pre; user-select: none; -webkit-user-select: none;
}
.btn.sel { background: var(--omarchy-menu-selected-background); border-color: var(--omarchy-menu-selected-text); color: var(--omarchy-menu-selected-text); }
.btn.destructive { border-color: ${alpha("var(--omarchy-urgent)", 0.56)}; }
.btn.destructive.sel { background: ${alpha("var(--omarchy-urgent)", 0.22)}; border-color: var(--omarchy-urgent); color: var(--omarchy-urgent); }
`;

export class Dialog extends OmarchyElement {
  static props = {
    open: ["bool"],
    title: ["string", ""],
    message: ["string", ""],
    secondaryText: ["string", ""],
    primaryText: ["string", ""],
    destructive: ["bool"],
    selectedIndex: ["number", 1],
  };
  static css = DIALOG_CSS;
  setup() {
    this.shadowRoot.innerHTML = `<div class="scrim"></div><div class="card" role="dialog" aria-modal="true"><div class="lines"><div class="title"></div><div class="message"></div><slot></slot></div><div class="buttons"><div class="btn" data-i="0"></div><div class="btn" data-i="1"></div></div></div>`;
    if (!this.hasAttribute("tabindex")) this.tabIndex = -1;
    this.$(".scrim").addEventListener("click", () => this.reject());
    for (const b of this.shadowRoot.querySelectorAll(".btn")) {
      b.addEventListener("pointerenter", () => (this.selectedIndex = Number(b.dataset.i)));
      b.addEventListener("click", () => (b.dataset.i === "0" ? this.reject() : this.accept()));
    }
    this.addEventListener("keydown", (e) => {
      if (this.handleKey(e)) {
        e.preventDefault();
        e.stopPropagation();
      }
    });
  }
  /** The dialog's keys (Qt's handleKey): Escape rejects, Return takes the selected button,
   *  Left/Right/h/l/Tab move the selection. True when it used the key. */
  handleKey(e) {
    if (!this.open) return false;
    const k = readingKey(e, true);
    if (e.key === "Escape") this.reject();
    else if (k === "ArrowLeft" || k === "ArrowRight" || e.key === "Tab") this.selectedIndex = this.selectedIndex === 0 ? 1 : 0;
    else if (e.key === "Enter") this.selectedIndex === 0 ? this.reject() : this.accept();
    else return false;
    return true;
  }
  reject() {
    this.emit("reject");
    this.emit("cancel");
  }
  accept() {
    this.emit("accept");
    this.emit("confirm");
  }
  labels() {
    return [this.secondaryText || this.t("common.cancel", "Cancel"), this.primaryText || this.t("common.confirm", "Confirm")];
  }
  update() {
    this.$(".title").textContent = this.title;
    this.$(".message").textContent = this.message;
    const [a, b] = this.labels();
    const btns = this.shadowRoot.querySelectorAll(".btn");
    btns[0].textContent = a;
    btns[1].textContent = b;
    btns.forEach((el, i) => el.classList.toggle("sel", i === this.selectedIndex));
    btns[1].classList.toggle("destructive", this.destructive);
    if (this.open && !this._wasOpen) queueMicrotask(() => this.isConnected && this.open && !this.hasAttribute("data-no-autofocus") && this.focus({ preventScroll: true }));
    this._wasOpen = this.open;
  }
}

export class ConfirmDialog extends Dialog {
  static props = {
    open: ["bool"],
    message: ["string", ""],
    cancelText: ["string", ""],
    confirmText: ["string", ""],
    selectedIndex: ["number", 1],
  };
  static css = `${DIALOG_CSS}
.scrim { background: ${alpha("var(--omarchy-background)", 0.7)}; }
.message { font-size: var(--omarchy-font-title); line-height: var(--omarchy-line-title, normal); }
.buttons { margin-top: ${space(20)}; }
`;
  get destructive() {
    return true;
  }
  get title() {
    return "";
  }
  labels() {
    return [this.cancelText || this.t("common.cancel", "Cancel"), this.confirmText || this.t("common.confirm", "Confirm")];
  }
}

// ---------------------------------------------------------------------------------------------
// Heading: a bold title at a font token's size, an optional textDim subtitle, a trailing slot.

const LEVELS = {
  title: ["title", "body-small"],
  heading: ["heading", "body"],
  display: ["display", "subtitle"],
};

export class Heading extends OmarchyElement {
  static props = { text: ["string", ""], subtitle: ["string", ""], level: ["string", "title"], centered: ["bool"] };
  static css = `
:host { display: flex; align-items: center; gap: var(--omarchy-space-control-gap); min-width: 0; }
.lines { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: var(--omarchy-space-xxs); }
.t { font-size: var(--size-t); line-height: var(--line-t, normal); font-weight: bold; white-space: pre; overflow: hidden; text-overflow: ellipsis; color: var(--omarchy-foreground); }
.s { font-size: var(--size-s); line-height: var(--line-s, normal); white-space: pre; overflow: hidden; text-overflow: ellipsis; color: var(--omarchy-text-dim); }
.s:empty { display: none; }
:host([centered]) .t, :host([centered]) .s { text-align: center; }
.trailing { display: flex; align-items: center; gap: var(--omarchy-space-xxs); }
.trailing.empty { display: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="lines"><div class="t" part="title"></div><div class="s" part="subtitle"></div></div><div class="trailing"><slot></slot></div>`;
    this.setAttribute("role", "heading");
    const slot = this.$("slot");
    const sync = () => this.$(".trailing").classList.toggle("empty", !slot.assignedElements().length);
    slot.addEventListener("slotchange", sync);
    sync();
  }
  update() {
    const [t, s] = LEVELS[this.level] || LEVELS.title;
    this.style.setProperty("--size-t", `var(--omarchy-font-${t})`);
    this.style.setProperty("--size-s", `var(--omarchy-font-${s})`);
    this.style.setProperty("--line-t", `var(--omarchy-line-${t}, normal)`);
    this.style.setProperty("--line-s", `var(--omarchy-line-${s}, normal)`);
    this.$(".t").textContent = this.text;
    this.$(".s").textContent = this.subtitle;
    this.setAttribute("aria-level", this.level === "display" ? "1" : this.level === "heading" ? "2" : "3");
  }
}

// ---------------------------------------------------------------------------------------------
// Pane: a flat surface inside a window: padding, an optional Heading with actions (slot="actions"),
// children in a column; plain, divided (a hairline track rule on one side) or bordered.

export class Pane extends OmarchyElement {
  static props = { title: ["string", ""], subtitle: ["string", ""], divider: ["string", ""], bordered: ["bool"] };
  static css = `${LOOK_CSS}
:host { display: block; position: relative; box-sizing: border-box; }
.box {
  --rule: var(--omarchy-space-hairline);
  position: relative; display: flex; flex-direction: column; gap: var(--omarchy-space-panel-gap);
  padding: calc(var(--omarchy-space-panel-padding) + var(--rt-x, 0px)) calc(var(--omarchy-space-panel-padding) + var(--rr-x, 0px))
           calc(var(--omarchy-space-panel-padding) + var(--rb-x, 0px)) calc(var(--omarchy-space-panel-padding) + var(--rl-x, 0px));
  border-radius: 0; height: 100%;
}
:host([bordered]) .box { border-radius: var(--omarchy-radius); }
:host([divider="left"]) .box { --rl-x: var(--rule); }
:host([divider="right"]) .box { --rr-x: var(--rule); }
:host([divider="top"]) .box { --rt-x: var(--rule); }
:host([divider="bottom"]) .box { --rb-x: var(--rule); }
.rule { position: absolute; background: var(--omarchy-track); display: none; }
:host([divider="left"]) .rule { display: block; left: calc(-1 * var(--bl)); top: calc(-1 * var(--bt)); bottom: calc(-1 * var(--bb)); width: var(--rule); }
:host([divider="right"]) .rule { display: block; right: calc(-1 * var(--br)); top: calc(-1 * var(--bt)); bottom: calc(-1 * var(--bb)); width: var(--rule); }
:host([divider="top"]) .rule { display: block; top: calc(-1 * var(--bt)); left: calc(-1 * var(--bl)); right: calc(-1 * var(--br)); height: var(--rule); }
:host([divider="bottom"]) .rule { display: block; bottom: calc(-1 * var(--bb)); left: calc(-1 * var(--bl)); right: calc(-1 * var(--br)); height: var(--rule); }
omarchy-heading.none { display: none; }
.body { display: flex; flex-direction: column; align-items: stretch; gap: var(--omarchy-space-panel-gap); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="box"><div class="rule"></div><omarchy-heading><slot name="actions"></slot></omarchy-heading><div class="body"><slot></slot></div></div>`;
  }
  update() {
    const box = this.$(".box");
    box.dataset.fill = this.bordered ? "normal" : "none";
    box.dataset.border = this.bordered ? "normal" : "none";
    const h = this.$("omarchy-heading");
    h.setAttribute("text", this.title);
    h.setAttribute("subtitle", this.subtitle);
    h.classList.toggle("none", !this.title);
  }
}

export const elements = {
  "omarchy-section-header": SectionHeader,
  "omarchy-separator": Separator,
  "omarchy-card": Card,
  "omarchy-menu-row": MenuRow,
  "omarchy-tooltip": Tooltip,
  "omarchy-dialog": Dialog,
  "omarchy-confirm-dialog": ConfirmDialog,
  "omarchy-heading": Heading,
  "omarchy-pane": Pane,
};
