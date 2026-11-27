// Data and layout (spec/components/data.md): Table, Tree, SplitView, Sidebar, Calendar, drawn as
// qt/qml/Omarchy/Ui/{Table,Tree,SplitView,Sidebar,Calendar}.qml draw them.
//
// Shared rules: rows show the hover fill under the pointer, keyboard focus or the cursor flag, else
// the selected fill when selected (hover replaces selected); selected text is bold in
// controls.selected.text. The app owns the state: each element asks for changes with events
// (`activate`, `sort`, `toggle`, `pick`, `resize`) and, like Qt's, moves its own cursor.

import { OmarchyElement, esc } from "./base.js";
import { fitGlyph, space } from "./looks.js";

/** The row look every data element shares (Qt's Rectangle under each delegate). */
const ROW_CSS = `
.row { position: relative; box-sizing: border-box; height: var(--omarchy-space-popup-row-height); border-radius: var(--omarchy-radius); cursor: pointer; }
.row.sel { background: var(--omarchy-control-selected-fill); }
.row.cur { background: var(--omarchy-control-hover-fill); }
.row.sel .ink { color: var(--omarchy-control-selected-text); font-weight: bold; }
.cell, .label { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.glyph { color: var(--omarchy-text-dim); font-size: var(--omarchy-font-icon); line-height: var(--omarchy-line-icon, normal); white-space: pre; }
.row.sel .glyph { color: var(--omarchy-control-selected-text); }
`;

/** Shared focus/hover bookkeeping: the cursor shows with the cursor flag, focus or the pointer
 *  over the rows (Qt's `_showCursor`). */
function followFocus(el, rowsRoot) {
  el.addEventListener("focus", () => ((el._focus = true), el.paint()));
  el.addEventListener("blur", () => ((el._focus = false), el.paint()));
  rowsRoot.addEventListener("pointerleave", () => ((el._hovering = false), el.paint()));
}

const cursorShown = (el) => el.cursor || !!el._focus || !!el._hovering;

// ---------------------------------------------------------------------------------------------
// Table

export class Table extends OmarchyElement {
  static focusable = true;
  static props = {
    columns: ["json", () => []],
    rows: ["json", () => []],
    sortColumn: ["number", -1],
    sortAscending: ["bool"],
    selectedRow: ["number", -1],
    cursorRow: ["number", -1],
    cursor: ["bool"],
    separators: ["bool"],
  };
  // Qt's defaults are true for these two: an absent attribute reads true, "false" reads false.
  get sortAscending() {
    return this.getAttribute("sort-ascending") !== "false";
  }
  set sortAscending(v) {
    this.setAttribute("sort-ascending", v ? "" : "false");
  }
  get separators() {
    return this.getAttribute("separators") !== "false";
  }
  set separators(v) {
    this.setAttribute("separators", v ? "" : "false");
  }
  static css = `${ROW_CSS}
:host { display: flex; flex-direction: column; width: 400px; outline: none; user-select: none; -webkit-user-select: none; }
.head, .row { display: flex; flex: none; height: var(--omarchy-space-popup-row-height); }
.head { position: relative; }
.rule { position: absolute; left: 0; right: 0; bottom: 0; height: var(--omarchy-space-hairline); background: var(--omarchy-track); pointer-events: none; }
.row:last-child .rule, :host([separators="false"]) .row .rule { display: none; }
.col { flex: none; box-sizing: border-box; min-width: 0; display: flex; align-items: center; padding: 0 var(--omarchy-space-control-padding-x); overflow: hidden; }
.col.flex { width: var(--flexw); }
.col.right { justify-content: flex-end; }
.cell { flex: 1; min-width: 0; }
.col.right .cell { text-align: right; }
.hd { cursor: pointer; gap: var(--omarchy-space-xs); }
.hd.right { flex-direction: row-reverse; justify-content: flex-start; }
.title, .sort { color: var(--omarchy-text-dim); font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); white-space: pre; }
.title { font-weight: bold; min-width: 0; overflow: hidden; text-overflow: ellipsis; }
.sort { flex: none; }
.body { flex: 1; min-height: 0; overflow-y: auto; scrollbar-width: none; display: flex; flex-direction: column; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="head" role="row"></div><div class="body" role="rowgroup"></div>`;
    this.setAttribute("role", "grid");
    const body = this.$(".body");
    followFocus(this, body);
    this.$(".head").addEventListener("click", (e) => {
      const h = e.target.closest(".hd");
      if (h) this.emit("sort", { column: Number(h.dataset.i) });
    });
    body.addEventListener("pointerover", (e) => {
      const r = e.target.closest(".row");
      if (!r) return;
      this._hovering = true;
      this.cursorRow = Number(r.dataset.i);
    });
    body.addEventListener("click", (e) => {
      const r = e.target.closest(".row");
      if (!r) return;
      this.focus({ preventScroll: true });
      this.activateRow(Number(r.dataset.i));
    });
    this.addEventListener("keydown", (e) => {
      const n = this.rows.length;
      const page = Math.max(1, Math.floor(body.clientHeight / Math.max(1, this.rowHeight())));
      if (e.key === "ArrowUp") this.moveCursor(-1);
      else if (e.key === "ArrowDown") this.moveCursor(1);
      else if (e.key === "Home") this.cursorRow = n > 0 ? 0 : -1;
      else if (e.key === "End") this.cursorRow = n - 1;
      else if (e.key === "PageDown") this.moveCursor(page);
      else if (e.key === "PageUp") this.moveCursor(-page);
      else if (e.key === "Enter" || e.key === " ") this.activateRow(this.cursorRow);
      else return;
      e.preventDefault();
      this.reveal();
    });
  }
  rowHeight() {
    const r = this.$(".head");
    return r ? r.offsetHeight : 28;
  }
  moveCursor(delta) {
    const n = this.rows.length;
    if (!n) return;
    const from = this.cursorRow < 0 ? (delta > 0 ? -1 : n) : this.cursorRow;
    this.cursorRow = Math.max(0, Math.min(n - 1, from + delta));
  }
  reveal() {
    const r = this.shadowRoot.querySelector(`.row[data-i="${this.cursorRow}"]`);
    if (r) r.scrollIntoView({ block: "nearest" });
  }
  /** Select a row and say so (Qt's activate: sets selectedRow, emits rowActivated). */
  activateRow(row) {
    if (row < 0 || row >= this.rows.length) return;
    this.selectedRow = row;
    this.emit("activate", { row });
  }
  render() {
    const cols = this.columns;
    const fixed = cols.reduce((a, c) => a + (Number(c.width) > 0 ? Number(c.width) : 0), 0);
    const flex = cols.filter((c) => !(Number(c.width) > 0)).length;
    this.style.setProperty("--flexw", flex ? `max(0px, round(down, calc((100% - ${fixed}px) / ${flex}), 1px))` : "0px");
    const colAttrs = (c) => {
      const w = Number(c.width) > 0 ? `style="width: ${Number(c.width)}px"` : "";
      return `class="col${Number(c.width) > 0 ? "" : " flex"}${c.align === "right" ? " right" : ""}" ${w}`;
    };
    // The flex share is of the table's width: give the rows the host's width.
    this.$(".head").innerHTML =
      cols
        .map((c, i) => `<div ${colAttrs(c).replace('class="col', `data-i="${i}" role="columnheader" class="col hd`)}><span class="title">${esc(c.title || "")}</span><span class="sort"></span></div>`)
        .join("") + `<div class="rule"></div>`;
    this.$(".body").innerHTML = this.rows
      .map(
        (r, i) =>
          `<div class="row" role="row" data-i="${i}">${cols
            .map((c, j) => `<div ${colAttrs(c)} role="gridcell"><span class="cell ink">${esc(r[j] === undefined || r[j] === null ? "" : r[j])}</span></div>`)
            .join("")}<div class="rule"></div></div>`,
      )
      .join("");
    this._key = this._dataKey();
  }
  _dataKey() {
    return JSON.stringify([this.columns, this.rows]);
  }
  paint() {
    if (!this._built) return;
    const show = cursorShown(this);
    this.shadowRoot.querySelectorAll(".body .row").forEach((r, i) => {
      r.classList.toggle("sel", i === this.selectedRow);
      r.classList.toggle("cur", show && i === this.cursorRow);
      r.setAttribute("aria-selected", String(i === this.selectedRow));
    });
    this.shadowRoot.querySelectorAll(".hd").forEach((h, i) => {
      const s = h.querySelector(".sort");
      s.textContent = i === this.sortColumn ? (this.sortAscending ? "\u{F0143}" : "\u{F0140}") : "";
      s.style.display = i === this.sortColumn ? "" : "none";
      fitGlyph(s);
      h.setAttribute("aria-sort", i === this.sortColumn ? (this.sortAscending ? "ascending" : "descending") : "none");
    });
  }
  update() {
    if (this._key !== this._dataKey()) this.render();
    this.paint();
  }
}

// ---------------------------------------------------------------------------------------------
// Tree

export class Tree extends OmarchyElement {
  static focusable = true;
  static props = {
    nodes: ["json", () => []],
    expanded: ["json", () => []],
    selectedId: ["string", ""],
    cursorId: ["string", ""],
    cursor: ["bool"],
  };
  static css = `${ROW_CSS}
:host { display: block; width: 240px; outline: none; overflow-y: auto; scrollbar-width: none; user-select: none; -webkit-user-select: none; }
.row { display: block; }
.chev { position: absolute; top: 0; height: 100%; width: var(--omarchy-space-xxxl); display: flex; align-items: center; justify-content: center;
  left: calc(var(--omarchy-space-md) + var(--depth) * var(--omarchy-space-huge)); }
.chev span { color: var(--omarchy-text-dim); font-size: var(--omarchy-font-icon-small); line-height: var(--omarchy-line-icon-small, normal); white-space: pre; }
.g, .label { position: absolute; top: 0; height: 100%; display: flex; align-items: center; }
.g { left: calc(var(--omarchy-space-md) + var(--depth) * var(--omarchy-space-huge) + var(--omarchy-space-xxxl) + var(--omarchy-space-sm)); }
.label { --lx: calc(var(--omarchy-space-md) + var(--depth) * var(--omarchy-space-huge) + var(--omarchy-space-xxxl) + var(--omarchy-space-sm));
  left: var(--lx); width: max(0px, calc(100% - var(--lx) - var(--omarchy-space-md))); display: block; line-height: var(--omarchy-space-popup-row-height); }
.label span { display: block; overflow: hidden; text-overflow: ellipsis; }
.row.glyphed .label { --lx: calc(var(--omarchy-space-md) + var(--depth) * var(--omarchy-space-huge) + var(--omarchy-space-xxxl) + var(--omarchy-space-sm) + var(--omarchy-font-icon) + var(--omarchy-space-md)); }
`;
  setup() {
    this.setAttribute("role", "tree");
    followFocus(this, this);
    this.shadowRoot.addEventListener("pointerover", (e) => {
      const r = e.target.closest(".row");
      if (!r) return;
      this._hovering = true;
      this.cursorId = r.dataset.id;
    });
    this.shadowRoot.addEventListener("click", (e) => {
      const r = e.target.closest(".row");
      if (!r) return;
      const row = this._rows.find((x) => x.id === r.dataset.id);
      if (e.target.closest(".chev")) {
        if (row && row.branch) this.emit("toggle", { id: row.id });
        return;
      }
      this.focus({ preventScroll: true });
      this.emit("activate", { id: r.dataset.id });
    });
    this.shadowRoot.addEventListener("dblclick", (e) => {
      const r = e.target.closest(".row");
      if (!r || e.target.closest(".chev")) return;
      const row = this._rows.find((x) => x.id === r.dataset.id);
      if (row && row.branch) this.emit("toggle", { id: row.id });
    });
    this.addEventListener("keydown", (e) => {
      const at = this._rows.findIndex((r) => r.id === this.cursorId);
      const row = this._rows[at];
      if (e.key === "ArrowUp") this.moveCursor(-1);
      else if (e.key === "ArrowDown") this.moveCursor(1);
      else if (e.key === "ArrowRight") {
        if (row && row.branch && !row.open) this.emit("toggle", { id: row.id });
      } else if (e.key === "ArrowLeft") {
        if (row && row.branch && row.open) this.emit("toggle", { id: row.id });
      } else if (e.key === "Enter" || e.key === " ") {
        if (this.cursorId !== "") this.emit("activate", { id: this.cursorId });
      } else return;
      e.preventDefault();
    });
  }
  /** The rows shown: depth first, children only under expanded branches. */
  visibleRows() {
    const open = new Set((this.expanded || []).map(String));
    const out = [];
    const walk = (list, depth) => {
      for (const n of list || []) {
        const kids = n.children || [];
        const isOpen = kids.length > 0 && open.has(String(n.id));
        out.push({ id: String(n.id), label: String(n.label || ""), glyph: String(n.glyph || ""), depth, branch: kids.length > 0, open: isOpen });
        if (isOpen) walk(kids, depth + 1);
      }
    };
    walk(this.nodes, 0);
    return out;
  }
  moveCursor(delta) {
    const rows = this._rows;
    if (!rows.length) return;
    const at = rows.findIndex((r) => r.id === this.cursorId);
    const from = at < 0 ? (delta > 0 ? -1 : rows.length) : at;
    this.cursorId = rows[Math.max(0, Math.min(rows.length - 1, from + delta))].id;
    const el = this.shadowRoot.querySelector(`.row[data-id="${CSS.escape(this.cursorId)}"]`);
    if (el) el.scrollIntoView({ block: "nearest" });
  }
  update() {
    const rows = this.visibleRows();
    const key = JSON.stringify(rows);
    if (key !== this._key) {
      this._key = key;
      this._rows = rows;
      this.shadowRoot.innerHTML = rows
        .map(
          (r) =>
            `<div class="row${r.glyph ? " glyphed" : ""}" role="treeitem" style="--depth: ${r.depth}" data-id="${esc(r.id)}" aria-level="${r.depth + 1}"${r.branch ? ` aria-expanded="${r.open}"` : ""}>` +
            `<div class="chev">${r.branch ? `<span>${r.open ? "\u{F0140}" : "\u{F0142}"}</span>` : ""}</div>` +
            (r.glyph ? `<div class="g"><span class="glyph">${esc(r.glyph)}</span></div>` : "") +
            `<div class="label"><span class="ink">${esc(r.label)}</span></div></div>`,
        )
        .join("");
      this.shadowRoot.querySelectorAll(".chev span").forEach(fitGlyph);
    }
    this.paint();
  }
  paint() {
    if (!this._built || !this._rows) return;
    const show = cursorShown(this);
    this.shadowRoot.querySelectorAll(".row").forEach((r) => {
      const id = r.dataset.id;
      r.classList.toggle("sel", id === this.selectedId);
      r.classList.toggle("cur", show && id === this.cursorId);
      r.setAttribute("aria-selected", String(id === this.selectedId));
    });
  }
}

// ---------------------------------------------------------------------------------------------
// SplitView: the children are the panes, laid out in a row (or a column with `vertical`), `md`
// apart; a handle in each gap. A pane sizes itself with attributes named after Qt's attached
// SplitView properties: preferred-width / preferred-height, minimum-width, maximum-width (…-height),
// fill-width / fill-height (else the last visible pane fills). `hidden` collapses a pane and its
// handle. Dragging a handle resizes the sized pane beside it (the leading one first) and emits
// `resize` { index, size }.

export class SplitView extends OmarchyElement {
  static props = { vertical: ["bool"], cursor: ["bool"] };
  static css = `
:host { display: flex; position: relative; gap: var(--omarchy-space-md); box-sizing: border-box; min-width: 0; min-height: 0; }
:host([vertical]) { flex-direction: column; }
::slotted(*) { flex: none; min-width: 0; min-height: 0; box-sizing: border-box; }
::slotted([data-split-fill]) { flex: 1 1 0; }
.handle { position: absolute; z-index: 1; border-radius: var(--omarchy-radius-small); cursor: col-resize; touch-action: none; }
:host([vertical]) .handle { cursor: row-resize; }
.handle.hot { background: var(--omarchy-control-hover-fill); }
.handle::after { content: ""; position: absolute; background: var(--omarchy-track);
  left: round(down, calc((100% - var(--omarchy-space-hairline)) / 2), 1px); top: 0; bottom: 0; width: var(--omarchy-space-hairline); }
:host([vertical]) .handle::after { left: 0; right: 0; width: auto; top: round(down, calc((100% - var(--omarchy-space-hairline)) / 2), 1px); bottom: auto; height: var(--omarchy-space-hairline); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<slot></slot><div class="handles"></div>`;
    this._ro = new ResizeObserver(() => this.placeHandles());
    this._ro.observe(this);
    this._mo = new MutationObserver(() => this.update());
    // A pane hidden or shown, or its size attributes changed (children only matter; deeper changes are cheap).
    this._mo.observe(this, { childList: true, subtree: true, attributeFilter: ["hidden", "preferred-width", "preferred-height", "minimum-width", "maximum-width", "minimum-height", "maximum-height", "fill-width", "fill-height"] });
    this.$("slot").addEventListener("slotchange", () => this.update());
  }
  panes() {
    return [...this.children].filter((c) => !c.hidden && getComputedStyle(c).display !== "none");
  }
  dim() {
    return this.vertical ? ["height", "Height"] : ["width", "Width"];
  }
  limits(p) {
    const [, D] = this.dim();
    const n = (a, d) => {
      const v = Number(p.getAttribute(a));
      return p.hasAttribute(a) && Number.isFinite(v) ? v : d;
    };
    return { min: n(`minimum-${D.toLowerCase()}`, 0), max: n(`maximum-${D.toLowerCase()}`, Infinity), pref: n(`preferred-${D.toLowerCase()}`, NaN) };
  }
  update() {
    if (!this._built) return;
    const [d, D] = this.dim();
    const panes = this.panes();
    const fillAttr = `fill-${D.toLowerCase()}`;
    let fill = panes.find((p) => p.hasAttribute(fillAttr) && p.getAttribute(fillAttr) !== "false");
    if (!fill) fill = panes[panes.length - 1];
    for (const p of [...this.children]) {
      p.toggleAttribute("data-split-fill", p === fill);
      if (p === fill || !panes.includes(p)) {
        p.style.removeProperty(d);
        continue;
      }
      const { min, max, pref } = this.limits(p);
      const size = p._splitSize ?? (Number.isFinite(pref) ? pref : null);
      if (size !== null) p.style[d] = `${Math.max(min, Math.min(max, size))}px`;
    }
    this._fill = fill;
    this.placeHandles();
  }
  placeHandles() {
    const box = this.$(".handles");
    if (!box) return;
    const panes = this.panes();
    const n = Math.max(0, panes.length - 1);
    if (box.childElementCount !== n) {
      box.innerHTML = Array.from({ length: n }, (_, i) => `<div class="handle" role="separator" data-i="${i}"></div>`).join("");
      box.querySelectorAll(".handle").forEach((h) => this.wireHandle(h));
    }
    const me = this.getBoundingClientRect();
    const gap = parseFloat(getComputedStyle(this).columnGap) || 0;
    box.querySelectorAll(".handle").forEach((h, i) => {
      const r = panes[i].getBoundingClientRect();
      if (this.vertical) Object.assign(h.style, { left: "0px", right: "0px", width: "", top: `${r.bottom - me.top - this.clientTop}px`, height: `${gap}px` });
      else Object.assign(h.style, { top: "0px", bottom: "0px", height: "", left: `${r.right - me.left - this.clientLeft}px`, width: `${gap}px` });
      h.classList.toggle("hot", !!(this.cursor || h._hover || h._drag));
      h.setAttribute("aria-orientation", this.vertical ? "horizontal" : "vertical");
    });
  }
  wireHandle(h) {
    const repaint = () => h.classList.toggle("hot", this.cursor || !!h._hover || !!h._drag);
    h.addEventListener("pointerenter", () => ((h._hover = true), repaint()));
    h.addEventListener("pointerleave", () => ((h._hover = false), repaint()));
    h.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      const i = Number(h.dataset.i);
      const panes = this.panes();
      // The sized pane beside the handle: the leading one, unless it is the one that fills.
      const lead = panes[i] !== this._fill;
      const pane = lead ? panes[i] : panes[i + 1];
      if (!pane) return;
      const [d] = this.dim();
      const start = pane.getBoundingClientRect()[d];
      const at = this.vertical ? e.clientY : e.clientX;
      h.setPointerCapture(e.pointerId);
      h._drag = { pane, start, at, lead, index: panes.indexOf(pane) };
      repaint();
      e.preventDefault();
    });
    h.addEventListener("pointermove", (e) => {
      const g = h._drag;
      if (!g) return;
      const delta = (this.vertical ? e.clientY : e.clientX) - g.at;
      const { min, max } = this.limits(g.pane);
      const size = Math.round(Math.max(min, Math.min(max, g.start + (g.lead ? delta : -delta))));
      if (size === g.pane._splitSize) return;
      g.pane._splitSize = size;
      this.update();
      this.emit("resize", { index: g.index, size });
    });
    const end = () => {
      h._drag = null;
      repaint();
    };
    h.addEventListener("pointerup", end);
    h.addEventListener("pointercancel", end);
  }
  disconnectedCallback() {
    super.disconnectedCallback();
  }
}

// ---------------------------------------------------------------------------------------------
// Sidebar

export class Sidebar extends OmarchyElement {
  static focusable = true;
  static props = {
    sections: ["json", () => []],
    selectedId: ["string", ""],
    cursorId: ["string", ""],
    collapsed: ["bool"],
    cursor: ["bool"],
  };
  static css = `${ROW_CSS}
:host { display: block; box-sizing: border-box; width: ${space(200)}; padding: var(--omarchy-space-sm); outline: none; user-select: none; -webkit-user-select: none; }
:host([collapsed]) { width: calc(var(--omarchy-space-sm) * 2 + var(--omarchy-space-control-padding-x) * 2 + var(--omarchy-font-icon)); }
.col { display: flex; flex-direction: column; gap: var(--omarchy-space-md); }
.head { color: var(--omarchy-text-dim); font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); font-weight: bold; white-space: pre; overflow: hidden; text-overflow: ellipsis;
  padding: round(up, calc(var(--omarchy-font-caption) * 0.15), 1px) 0 var(--omarchy-space-xxs) var(--omarchy-space-control-padding-x); }
:host([collapsed]) .head, .head:empty { display: none; }
.row { display: block; }
.g { position: absolute; left: var(--omarchy-space-control-padding-x); top: 0; height: 100%; display: flex; align-items: center; }
.label { position: absolute; top: 0; left: calc(var(--omarchy-space-control-padding-x) + var(--omarchy-font-icon) + var(--omarchy-space-control-gap));
  right: var(--omarchy-space-control-padding-x); line-height: var(--omarchy-space-popup-row-height); }
:host([collapsed]) .label { display: none; }
`;
  setup() {
    this.setAttribute("role", "listbox");
    followFocus(this, this);
    this.shadowRoot.addEventListener("pointerover", (e) => {
      const r = e.target.closest(".row");
      if (!r) return;
      this._hovering = true;
      this.cursorId = r.dataset.id;
    });
    this.shadowRoot.addEventListener("click", (e) => {
      const r = e.target.closest(".row");
      if (!r) return;
      this.focus({ preventScroll: true });
      this.emit("activate", { id: r.dataset.id });
    });
    this.addEventListener("keydown", (e) => {
      if (e.key === "ArrowUp") this.moveCursor(-1);
      else if (e.key === "ArrowDown") this.moveCursor(1);
      else if (e.key === "Enter" || e.key === " ") {
        if (this.cursorId !== "") this.emit("activate", { id: this.cursorId });
      } else return;
      e.preventDefault();
    });
  }
  ids() {
    return this.sections.flatMap((s) => (s.items || []).map((i) => String(i.id)));
  }
  moveCursor(delta) {
    const ids = this.ids();
    if (!ids.length) return;
    const at = ids.indexOf(this.cursorId);
    const from = at < 0 ? (delta > 0 ? -1 : ids.length) : at;
    this.cursorId = ids[Math.max(0, Math.min(ids.length - 1, from + delta))];
  }
  update() {
    const key = JSON.stringify(this.sections);
    if (key !== this._key) {
      this._key = key;
      this.shadowRoot.innerHTML = `<div class="col">${this.sections
        .map(
          (s) =>
            `<div role="group"><div class="head">${esc(s.title || "")}</div>${(s.items || [])
              .map(
                (it) =>
                  `<div class="row" role="option" data-id="${esc(it.id)}" title="${esc(it.label || "")}"><div class="g"><span class="glyph">${esc(it.glyph || "")}</span></div><div class="label cell ink">${esc(it.label || "")}</div></div>`,
              )
              .join("")}</div>`,
        )
        .join("")}</div>`;
    }
    this.paint();
  }
  paint() {
    if (!this._built) return;
    const show = cursorShown(this);
    this.shadowRoot.querySelectorAll(".row").forEach((r) => {
      const id = r.dataset.id;
      r.classList.toggle("sel", id === this.selectedId);
      r.classList.toggle("cur", show && id === this.cursorId);
      r.setAttribute("aria-selected", String(id === this.selectedId));
    });
  }
}

// ---------------------------------------------------------------------------------------------
// Calendar: a month grid from Monday, six weeks, today ringed in accent, the selected day filled.

const MONTHS = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
const WEEKDAYS = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"];
const pad = (n) => (n < 10 ? "0" : "") + n;
const iso = (y, m, d) => `${y}-${pad(m)}-${pad(d)}`;
const isoOf = (d) => iso(d.getFullYear(), d.getMonth() + 1, d.getDate());
const dateOf = (s) => {
  const p = String(s).split("-");
  return new Date(Number(p[0]), Number(p[1]) - 1, Number(p[2]), 12);
};

export class Calendar extends OmarchyElement {
  static focusable = true;
  static props = {
    year: ["number", 2026],
    month: ["number", 1],
    today: ["string", ""],
    selected: ["string", ""],
    cursorDate: ["string", ""],
    cursor: ["bool"],
    monthNames: ["json", () => MONTHS],
    weekdayNames: ["json", () => WEEKDAYS],
  };
  static css = `
:host {
  --cw: ${space(36)}; --ch: var(--omarchy-space-control-height); --gap: var(--omarchy-space-xxs);
  display: inline-flex; flex-direction: column; gap: var(--omarchy-space-sm); width: calc(var(--cw) * 7 + var(--gap) * 6);
  outline: none; user-select: none; -webkit-user-select: none; vertical-align: top;
}
.head { position: relative; display: flex; align-items: center; justify-content: space-between; }
.head-slot { width: var(--cw); display: flex; align-items: center; justify-content: center; flex: none; }
.month { position: absolute; left: 0; right: 0; text-align: center; color: var(--omarchy-text-dim); white-space: pre;
  font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); pointer-events: none; }
.grid { display: grid; grid-template-columns: repeat(7, var(--cw)); gap: var(--gap); }
.wd { height: ${space(16)}; display: flex; align-items: center; justify-content: center; color: var(--omarchy-text-dim); font-weight: bold;
  font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); white-space: pre; }
.day { box-sizing: border-box; height: var(--ch); display: flex; align-items: center; justify-content: center; border-radius: var(--omarchy-radius); cursor: pointer;
  font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-foreground); }
.day.weekend { color: var(--omarchy-text-dim); }
.day.out { color: var(--omarchy-text-faint); }
.day.sel { background: var(--omarchy-control-selected-fill); font-weight: bold; }
.day.sel:not(.out) { color: var(--omarchy-control-selected-text); }
.day.cur { background: var(--omarchy-control-hover-fill); }
.day.today { border: var(--omarchy-space-hairline) solid var(--omarchy-accent); font-weight: bold; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="head"><span class="month"></span><div class="head-slot"><omarchy-icon-button class="prev" nofocus icon="\u{F0141}"></omarchy-icon-button></div><div class="head-slot"><omarchy-icon-button class="next" nofocus icon="\u{F0142}"></omarchy-icon-button></div></div><div class="grid" role="grid"></div>`;
    this.setAttribute("role", "group");
    const grid = this.$(".grid");
    followFocus(this, grid);
    this.$(".prev").addEventListener("click", () => this.stepMonth(-1));
    this.$(".next").addEventListener("click", () => this.stepMonth(1));
    this.shadowRoot.addEventListener("mousedown", (e) => {
      if (e.target.closest("omarchy-icon-button")) e.preventDefault();
    });
    grid.addEventListener("pointerover", (e) => {
      const c = e.target.closest(".day");
      if (!c) return;
      this._hovering = true;
      this.cursorDate = c.dataset.date;
    });
    grid.addEventListener("click", (e) => {
      const c = e.target.closest(".day");
      if (!c) return;
      this.focus({ preventScroll: true });
      this.pick(c.dataset.date);
    });
    this.addEventListener("keydown", (e) => {
      const d = { ArrowLeft: -1, ArrowRight: 1, ArrowUp: -7, ArrowDown: 7 }[e.key];
      if (d !== undefined) this.moveCursor(d);
      else if (e.key === "PageUp") this.stepMonth(-1);
      else if (e.key === "PageDown") this.stepMonth(1);
      else if (e.key === "Enter" || e.key === " ") {
        if (this.cursorDate !== "") this.pick(this.cursorDate);
      } else return;
      e.preventDefault();
    });
  }
  stepMonth(delta) {
    const m = this.month - 1 + delta;
    const year = this.year + Math.floor(m / 12);
    const month = (((m % 12) + 12) % 12) + 1;
    this.year = year;
    this.month = month;
    this.emit("month", { year, month });
  }
  moveCursor(days) {
    const from = this.cursorDate || this.selected || this.today || iso(this.year, this.month, 1);
    const d = dateOf(from);
    d.setDate(d.getDate() + days);
    this.cursorDate = isoOf(d);
    if (d.getFullYear() !== this.year || d.getMonth() + 1 !== this.month) {
      this.year = d.getFullYear();
      this.month = d.getMonth() + 1;
      this.emit("month", { year: this.year, month: this.month });
    }
  }
  /** Choose a day (Qt's pick: sets `selected`, emits picked). */
  pick(date) {
    this.selected = date;
    this.emit("pick", { date });
  }
  days() {
    const first = new Date(this.year, this.month - 1, 1, 12);
    const back = (first.getDay() + 6) % 7;
    return Array.from({ length: 42 }, (_, i) => {
      const d = new Date(this.year, this.month - 1, 1 - back + i, 12);
      return { date: isoOf(d), day: d.getDate(), inMonth: d.getMonth() === this.month - 1, weekend: i % 7 >= 5 };
    });
  }
  update() {
    const names = this.monthNames;
    this.$(".month").textContent = `${names[this.month - 1] || ""} ${this.year}`.toUpperCase();
    this.$(".prev").setAttribute("tooltip", this.t("clock.previousMonth", "Previous month"));
    this.$(".next").setAttribute("tooltip", this.t("clock.nextMonth", "Next month"));
    const key = `${this.year}-${this.month}-${JSON.stringify(this.weekdayNames)}`;
    if (key !== this._key) {
      this._key = key;
      this._days = this.days();
      this.$(".grid").innerHTML =
        this.weekdayNames.map((w) => `<div class="wd" role="columnheader">${esc(String(w).toUpperCase())}</div>`).join("") +
        this._days
          .map((c) => `<div class="day${c.inMonth ? "" : " out"}${c.weekend ? " weekend" : ""}" role="gridcell" data-date="${c.date}">${c.day}</div>`)
          .join("");
    }
    this.paint();
  }
  paint() {
    if (!this._built) return;
    const show = cursorShown(this);
    this.shadowRoot.querySelectorAll(".day").forEach((c) => {
      const d = c.dataset.date;
      c.classList.toggle("today", d === this.today);
      c.classList.toggle("sel", d === this.selected);
      c.classList.toggle("cur", show && d === this.cursorDate);
      c.setAttribute("aria-selected", String(d === this.selected));
    });
  }
}

export const elements = {
  "omarchy-table": Table,
  "omarchy-tree": Tree,
  "omarchy-split-view": SplitView,
  "omarchy-sidebar": Sidebar,
  "omarchy-calendar": Calendar,
};
