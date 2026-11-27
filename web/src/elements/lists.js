// Lists (spec/components/lists.md): ListRow, CursorModel, ScrollArea, KeyValue, Hero, Badge,
// StatusDot, Avatar, TileGrid. Drawn as qt/qml/Omarchy/Ui draws them: ListRow.qml (on
// CursorSurface), CursorModel.qml + PointerMoveGate.qml, ScrollArea.qml, KeyValue.qml, Hero.qml,
// Badge.qml, StatusDot.qml, Avatar.qml, TileGrid.qml.

import { OmarchyElement, esc } from "./base.js";
import { OG_CSS, alpha, inkCenter, ogFont, space, trackPointer } from "./looks.js";
import { omarchyApi } from "../runtime.js";

const TONES = ["accent", "success", "warning", "urgent", "muted"];
/** A tone's color as CSS: one of TONES, else `fallback`. */
const tone = (t, fallback) => (TONES.includes(t) ? `var(--omarchy-${t})` : fallback);

// A CursorSurface (Qt): the cursor look (hover fill and border) or the current look (selected fill
// and border), the border drawn inside the box as Qt's Rectangle draws it, so it never moves content.
const SURFACE_CSS = `
.surface { position: relative; border-radius: var(--omarchy-radius); background: transparent; transition: background-color 60ms; }
.surface::after { content: ""; position: absolute; inset: 0; pointer-events: none; border-radius: inherit;
  border-style: solid; border-width: 0; border-color: transparent; }
.surface.cursor { background: var(--omarchy-control-hover-fill); }
.surface.cursor::after { border-color: var(--omarchy-control-hover-border); border-width: var(--omarchy-control-hover-border-width); }
.surface.current:not(.cursor) { background: var(--omarchy-control-selected-fill); }
.surface.current:not(.cursor)::after { border-color: var(--omarchy-control-selected-border); border-width: var(--omarchy-control-selected-border-width); }
`;

// Count async loads (images, icon lookups) so a still render waits for them (gallery.js).
const pending = (d) => {
  window.__omarchyPending = Math.max(0, (window.__omarchyPending || 0) + d);
};

// ---------------------------------------------------------------------------------------------
// CursorModel: the keyboard cursor a panel walks (a section and a row within it), and the pointer
// gate that keeps a still pointer from stealing it. Not an element: an EventTarget.
//
//   const cursor = new CursorModel({ sections: [{ name: "rows", count: 9 }] });
//   list.addEventListener("keydown", (e) => cursor.handleKey(e));
//   row.addEventListener("pointermove", (e) => cursor.pointerMoved(e, "rows", i));
//   cursor.addEventListener("change", render);          // any change of the cursor
//   cursor.addEventListener("moved", (e) => …);         // { section, index, byKey }
//   activated { section, index }, deleterequested { section, index }, closerequested, texttyped { text }

export class CursorModel extends EventTarget {
  constructor({ sections = [{ name: "", count: 0 }], columns = 1, pageSize = 5 } = {}) {
    super();
    this._sections = sections.map((s) => ({ name: String(s.name), count: Number(s.count) || 0 }));
    this.focusSection = this._sections.length ? this._sections[0].name : "";
    this.selectedIndex = 0;
    this.cursorActive = false;
    this.columns = columns;
    this.pageSize = pageSize;
    this.threshold = 1;
    this._gate = { primed: false, initial: false, x: 0, y: 0 };
  }

  get sections() {
    return this._sections;
  }
  set sections(v) {
    this._sections = v.map((s) => ({ name: String(s.name), count: Number(s.count) || 0 }));
    this.settle();
    this._changed();
  }

  _emit(type, detail) {
    this.dispatchEvent(new CustomEvent(type, { detail }));
  }
  _changed() {
    this._emit("change", { section: this.focusSection, index: this.selectedIndex, active: this.cursorActive });
  }

  sectionIndex(name) {
    return this._sections.findIndex((s) => s.name === name);
  }
  countOf(name) {
    const i = this.sectionIndex(name);
    return i < 0 ? 0 : this._sections[i].count;
  }
  neighbour(from, dir) {
    for (let i = from + dir; i >= 0 && i < this._sections.length; i += dir) if (this._sections[i].count > 0) return i;
    return -1;
  }

  /** A row's cursor flag. */
  isAt(section, index) {
    return this.cursorActive && this.focusSection === section && this.selectedIndex === index;
  }

  set(section, index) {
    if (this.sectionIndex(section) < 0) return;
    this.focusSection = section;
    this.selectedIndex = index;
    this.cursorActive = true;
    this.settle();
    this._changed();
  }

  reset() {
    this.cursorActive = false;
    this._resetGate();
    this._changed();
  }

  setCount(section, count) {
    const i = this.sectionIndex(section);
    if (i < 0) return;
    this._sections = this._sections.map((s, j) => (j === i ? { name: s.name, count } : s));
    this.settle();
    this._changed();
  }

  settle() {
    const s = this.sectionIndex(this.focusSection);
    if (s < 0 || this._sections[s].count === 0) {
      let n = this.neighbour(Math.max(0, s), 1);
      if (n < 0) n = this.neighbour(Math.max(0, s), -1);
      if (n < 0 && s < 0 && this._sections.length > 0) n = 0;
      if (n >= 0) {
        this.focusSection = this._sections[n].name;
        this.selectedIndex = 0;
      }
    }
    this.selectedIndex = Math.max(0, Math.min(this.selectedIndex, this.countOf(this.focusSection) - 1));
  }

  _wake() {
    this._resetGate();
    if (this.cursorActive) return true;
    this.cursorActive = true;
    this.settle();
    return false;
  }

  _announce(byKey) {
    this._changed();
    this._emit("moved", { section: this.focusSection, index: this.selectedIndex, byKey });
  }

  move(dx, dy) {
    if (!this._wake()) return this._announce(true);
    const cols = this.columns;
    let s = this.sectionIndex(this.focusSection);
    let count = this.countOf(this.focusSection);
    let index = this.selectedIndex;
    if (dx !== 0 && cols > 1) {
      const col = (index % cols) + dx;
      if (col >= 0 && col < cols && index + dx >= 0 && index + dx < count) index += dx;
    }
    for (let step = 0; step < Math.abs(dy); step++) {
      const next = index + cols * (dy > 0 ? 1 : -1);
      if (next >= 0 && next < count) {
        index = next;
        continue;
      }
      const n = this.neighbour(s, dy > 0 ? 1 : -1);
      if (n >= 0) {
        const c = this._sections[n].count;
        if (dy > 0) index = Math.min(index % cols, c - 1);
        else index = Math.min(Math.floor((c - 1) / cols) * cols + (index % cols), c - 1);
        s = n;
        count = c;
      } else if (dy > 0 && cols > 1 && Math.floor(index / cols) < Math.floor((count - 1) / cols)) {
        index = count - 1;
      }
    }
    this.focusSection = this._sections[s].name;
    this.selectedIndex = index;
    this._announce(true);
  }

  moveSection(delta) {
    if (!this._wake()) return this._announce(true);
    const n = this.neighbour(this.sectionIndex(this.focusSection), delta > 0 ? 1 : -1);
    if (n < 0) return;
    this.focusSection = this._sections[n].name;
    this.selectedIndex = 0;
    this._announce(true);
  }

  page(delta) {
    if (!this._wake()) return this._announce(true);
    const last = this.countOf(this.focusSection) - 1;
    this.selectedIndex = Math.max(0, Math.min(last, this.selectedIndex + Math.max(1, this.pageSize) * this.columns * (delta > 0 ? 1 : -1)));
    this._announce(true);
  }

  home() {
    this.cursorActive = true;
    this._resetGate();
    const n = this.neighbour(-1, 1);
    if (n >= 0) {
      this.focusSection = this._sections[n].name;
      this.selectedIndex = 0;
    }
    this._announce(true);
  }

  end() {
    this.cursorActive = true;
    this._resetGate();
    const n = this.neighbour(this._sections.length, -1);
    if (n >= 0) {
      this.focusSection = this._sections[n].name;
      this.selectedIndex = this._sections[n].count - 1;
    }
    this._announce(true);
  }

  /** Type-ahead text a key carries, or "": one character (or a surrogate pair), not a space or a
   *  control, with no Ctrl, Alt or Super. */
  printable(e) {
    if (e.ctrlKey || e.altKey || e.metaKey) return "";
    const t = e.key || "";
    if (t.length < 1 || t.length > 2) return "";
    if (t.length === 2 && !(t.charCodeAt(0) >= 0xd800 && t.charCodeAt(0) < 0xdc00)) return "";
    if (/^[\s\x00-\x1f\x7f]$/.test(t)) return "";
    return t;
  }

  /** The panel keys (spec/components/lists.md, Keys). True, and the event's default prevented,
   *  when the key was the cursor's. */
  handleKey(e) {
    const plain = !(e.ctrlKey || e.altKey || e.metaKey);
    const k = e.key;
    const t = plain ? k : "";
    let done = true;
    if (k === "ArrowDown" || t === "j") this.move(0, 1);
    else if (k === "ArrowUp" || t === "k") this.move(0, -1);
    else if (k === "ArrowRight" || t === "l") this.move(1, 0);
    else if (k === "ArrowLeft" || t === "h") this.move(-1, 0);
    else if (k === "PageDown") this.page(1);
    else if (k === "PageUp") this.page(-1);
    else if (k === "Home") this.home();
    else if (k === "End") this.end();
    else if (k === "Tab") this.moveSection(e.shiftKey ? -1 : 1);
    else if (k === "Enter" || k === " ") this._emit("activated", { section: this.focusSection, index: this.selectedIndex });
    else if (k === "Escape") this._emit("closerequested", {});
    else if (k === "Delete" || t === "x" || t === "X") this._emit("deleterequested", { section: this.focusSection, index: this.selectedIndex });
    else {
      const typed = this.printable(e);
      if (typed) this._emit("texttyped", { text: typed });
      else done = false;
    }
    if (done && e.preventDefault) e.preventDefault();
    return done;
  }

  // The pointer gate (PointerMoveGate.qml): a sample moves the cursor only when the pointer
  // travelled more than `threshold` px since the last accepted one; keyboard moves reset it and the
  // next sample only primes it. Points are viewport coordinates, so content scrolling under a still
  // pointer is no movement.
  _resetGate() {
    this._gate = { primed: false, initial: false, x: 0, y: 0 };
  }
  allowInitialSample() {
    this._resetGate();
    this._gate.initial = true;
  }
  _gateMoved(x, y) {
    const g = this._gate;
    const first = !g.primed;
    const moved = !first ? Math.abs(x - g.x) > this.threshold || Math.abs(y - g.y) > this.threshold : g.initial;
    if (first || moved) {
      g.x = x;
      g.y = y;
    }
    g.primed = true;
    g.initial = false;
    return moved;
  }

  /** A pointer sample over a row (a PointerEvent, or { clientX, clientY }): moves the cursor there
   *  when the pointer really moved. True when the cursor is on that row afterwards. */
  pointerMoved(e, section, index) {
    if (!e) {
      this._resetGate();
      return false;
    }
    if (!this._gateMoved(e.clientX, e.clientY)) return false;
    if (this.isAt(section, index)) return true;
    this.set(section, index);
    this._announce(false);
    return true;
  }
}

// ---------------------------------------------------------------------------------------------
// ListRow: leading slot · title / subtitle / status · trailing (value or any item) · end slot (a
// glyph that stays, or the action revealed in its place).

export class ListRow extends OmarchyElement {
  static props = {
    glyph: ["string", ""],
    glyphColor: ["string", null],
    title: ["string", ""],
    subtitle: ["string", ""],
    status: ["string", ""],
    statusTone: ["string", ""],
    value: ["string", ""],
    trailingGlyph: ["string", ""],
    trailingTone: ["string", ""],
    actionGlyph: ["string", ""],
    actionUrgent: ["bool"],
    actionCursor: ["bool"],
    actionTip: ["string", ""],
    cursor: ["bool"],
    current: ["bool"],
    disabled: ["bool"],
  };
  static css = `${SURFACE_CSS}${OG_CSS}
:host { display: block; cursor: pointer; user-select: none; -webkit-user-select: none; }
:host([disabled]) { cursor: default; pointer-events: none; }
.surface { display: flex; flex-direction: column; padding: calc(var(--omarchy-space-row-padding-x) / 2) var(--omarchy-space-control-padding-x); }
.main { display: flex; align-items: center; min-width: 0; }
.lead { flex: none; width: calc(var(--omarchy-font-icon-large) + ${space(10)}); display: flex; align-items: center; }
.lead.none { display: none; }
.lead-box { width: var(--omarchy-font-icon-large); display: flex; justify-content: center; align-items: center; }
.lead-og {
  width: var(--omarchy-font-icon-large);
  height: max(var(--omarchy-font-icon-large), round(up, calc(var(--omarchy-font-title) * 1.35), 1px));
  color: var(--glyph-color, var(--omarchy-foreground));
  ${ogFont("title")}
}
.lead-og.none { display: none; }
.info { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: ${space(1)}; }
.title { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); font-weight: bold; color: var(--omarchy-foreground); }
.sub, .status { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); color: var(--omarchy-text-dim); }
.title, .sub, .status { white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.sub:empty, .status:empty { display: none; }
.trail { flex: none; display: flex; align-items: center; gap: ${space(8)}; margin-left: ${space(8)}; }
.trail.none { display: none; }
.value { font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, normal); color: var(--omarchy-text-dim); white-space: pre; }
.value:empty { display: none; }
::slotted([slot="trailing"]) { flex: none; }
.slot { position: relative; flex: none; width: ${space(22)}; height: ${space(22)}; border-radius: var(--omarchy-radius-small); ${ogFont("subtitle")} }
.slot.none { display: none; }
.slot.has-action { cursor: pointer; }
.slot.cur { background: ${alpha("var(--ink)", "var(--omarchy-control-hover-fill-alpha)")}; }
.slot.cur::after { content: ""; position: absolute; inset: 0; border-radius: inherit; border: var(--omarchy-control-hover-border-top) solid ${alpha("var(--ink)", "var(--omarchy-control-hover-border-alpha)")}; }
.end-glyph {
  position: absolute; left: 50%; white-space: pre;
  font-size: var(--f); line-height: var(--l, normal);
  top: round(calc((100% - var(--l, 1em)) / 2), 1px);
}
.end-glyph.hidden { display: none; }
.act { color: var(--omarchy-text-dim); }
.act.urgent { color: var(--omarchy-urgent); }
.expansion { margin: ${space(4)} 0 0; }
.expansion.none { display: none; }
:host([disabled]) .title, :host([disabled]) .sub, :host([disabled]) .status, :host([disabled]) .value,
:host([disabled]) .glyph, :host([disabled]) .tg { color: var(--omarchy-text-faint); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="surface" part="surface">
  <div class="main">
    <div class="lead"><div class="lead-box"><div class="og lead-og"><span class="glyph"></span></div><slot name="leading"></slot></div></div>
    <div class="info"><div class="title"></div><div class="sub"></div><div class="status"></div></div>
    <div class="trail"><span class="value"></span><slot name="trailing"></slot>
      <div class="slot"><span class="end-glyph act"></span><span class="end-glyph tg"></span></div></div>
  </div>
  <div class="expansion"><slot name="expansion"></slot></div>
</div>`;
    this.setAttribute("role", "option");
    trackPointer(this, this, { focus: false });
    for (const name of ["leading", "trailing", "expansion"]) this.$(`slot[name=${name}]`).addEventListener("slotchange", () => this.update());
    this.addEventListener("pointermove", (e) => {
      if (!this.disabled) this.emit("pointermoved", { clientX: e.clientX, clientY: e.clientY, event: e });
    });
    this.addEventListener("click", (e) => {
      if (this.disabled) {
        e.stopImmediatePropagation();
        e.preventDefault();
      }
    }, true);
    this.$(".slot").addEventListener("click", (e) => {
      if (this.disabled || !this.actionGlyph) return;
      e.stopPropagation();
      this.emit("action");
    });
  }
  update() {
    const has = (name) => this.$(`slot[name=${name}]`).assignedElements().length > 0;
    const leading = has("leading");
    const trailing = has("trailing");
    const expansion = has("expansion");
    const surface = this.$(".surface");
    surface.classList.toggle("cursor", this.cursor && !this.disabled);
    surface.classList.toggle("current", this.current && !this.disabled);

    const leadOg = this.$(".lead-og");
    const glyph = this.$(".glyph");
    const showLeadGlyph = !leading && !!this.glyph;
    glyph.textContent = showLeadGlyph ? this.glyph : "";
    leadOg.classList.toggle("none", !showLeadGlyph);
    if (this.glyphColor) this.style.setProperty("--glyph-color", this.glyphColor);
    else this.style.removeProperty("--glyph-color");
    if (showLeadGlyph) inkCenter(glyph);
    this.$(".lead").classList.toggle("none", !leading && !this.glyph);

    this.$(".title").textContent = this.title;
    this.$(".sub").textContent = this.subtitle;
    const status = this.$(".status");
    status.textContent = this.status;
    status.style.color = this.disabled ? "" : tone(this.statusTone, "var(--omarchy-text-dim)");

    const value = this.$(".value");
    value.textContent = trailing ? "" : this.value;

    const actionShown = !!this.actionGlyph && !this.disabled && (this.cursor || this.actionCursor || !!this._hot);
    const slot = this.$(".slot");
    const slotShown = !!this.actionGlyph || !!this.trailingGlyph;
    slot.classList.toggle("none", !slotShown);
    slot.classList.toggle("has-action", !!this.actionGlyph && !this.disabled);
    slot.classList.toggle("cur", this.actionCursor && !this.disabled);
    slot.style.setProperty("--ink", this.actionUrgent ? "var(--omarchy-urgent)" : "var(--omarchy-foreground)");
    const act = this.$(".act");
    act.textContent = this.actionGlyph;
    act.classList.toggle("urgent", this.actionUrgent);
    act.classList.toggle("hidden", !actionShown);
    const tg = this.$(".tg");
    tg.textContent = this.trailingGlyph;
    tg.style.color = this.disabled ? "" : tone(this.trailingTone, "var(--omarchy-text-dim)");
    tg.classList.toggle("hidden", !this.trailingGlyph || actionShown);
    if (actionShown) inkCenter(act);
    if (this.trailingGlyph && !actionShown) inkCenter(tg);
    slot.title = this.actionTip && actionShown ? this.actionTip : "";

    this.$(".trail").classList.toggle("none", !(value.textContent || trailing || slotShown));
    this.$(".expansion").classList.toggle("none", !expansion);
    this.$(".main").style.alignSelf = "";
    // With an expansion the main line sits at the top padding; without, the row is the main line.
    surface.style.justifyContent = expansion ? "flex-start" : "center";
    this.setAttribute("aria-disabled", String(this.disabled));
    this.setAttribute("aria-selected", String(this.current));
    if (this.title) this.setAttribute("aria-label", this.title);
  }
}

// ---------------------------------------------------------------------------------------------
// ScrollArea: a vertical scroll view capped at max-height, Omarchy's thin bar over its right edge
// while the content overflows.

export class ScrollArea extends OmarchyElement {
  static props = { maxHeight: ["number", Infinity], spacing: ["string", "0"] };
  static css = `
:host { display: block; position: relative; }
.view { overflow-y: auto; overflow-x: hidden; scrollbar-width: none; overscroll-behavior: contain; }
.view::-webkit-scrollbar { display: none; }
.col { display: flex; flex-direction: column; align-items: stretch; gap: var(--gap, 0px); }
.bar { position: absolute; top: 0; right: 0; bottom: 0; width: var(--omarchy-space-scrollbar-thickness);
  background: var(--omarchy-track); border-radius: var(--omarchy-radius-small); display: none; touch-action: none; }
.bar.on { display: block; }
.thumb { position: absolute; left: 0; right: 0; border-radius: var(--omarchy-radius-small); background: var(--omarchy-control-normal-border); }
.bar:hover .thumb, .bar.drag .thumb { background: var(--omarchy-text-dim); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="view" part="view"><div class="col" part="content"><slot></slot></div></div><div class="bar"><div class="thumb"></div></div>`;
    this.view = this.$(".view");
    const sync = () => this._sync();
    this.view.addEventListener("scroll", sync, { passive: true });
    this._ro = new ResizeObserver(sync);
    this._ro.observe(this.view);
    this._ro.observe(this.$(".col"));
    const bar = this.$(".bar");
    let grab = null;
    bar.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      const thumb = this.$(".thumb").getBoundingClientRect();
      const b = bar.getBoundingClientRect();
      // On the thumb: drag from where it was grabbed. On the groove: center the thumb there first.
      const offset = e.clientY >= thumb.top && e.clientY <= thumb.bottom ? e.clientY - thumb.top : thumb.height / 2;
      grab = { offset };
      bar.setPointerCapture(e.pointerId);
      bar.classList.add("drag");
      this._dragTo(e.clientY - b.top - offset);
    });
    bar.addEventListener("pointermove", (e) => {
      if (!grab) return;
      this._dragTo(e.clientY - bar.getBoundingClientRect().top - grab.offset);
    });
    const end = () => {
      grab = null;
      bar.classList.remove("drag");
    };
    bar.addEventListener("pointerup", end);
    bar.addEventListener("pointercancel", end);
  }
  _dragTo(thumbTop) {
    const v = this.view;
    const h = v.clientHeight;
    const th = this._thumbH || 0;
    const room = Math.max(1, h - th);
    v.scrollTop = (Math.max(0, Math.min(room, thumbTop)) / room) * Math.max(0, v.scrollHeight - h);
  }
  _sync() {
    const v = this.view;
    if (!v) return;
    const h = v.clientHeight;
    const content = this.$(".col").offsetHeight;
    const over = content > h + 0.5;
    this.$(".bar").classList.toggle("on", over);
    if (!over) return;
    // Qt's ScrollBar: size = the visible share, at least space(24) long; the position scales into
    // the room the longer thumb leaves.
    const size = h / content;
    const minPx = Number.parseFloat(getComputedStyle(this).getPropertyValue("--omarchy-space-scale") || "1") * 24;
    const minSize = Math.max(1, Math.round(minPx)) / Math.max(1, h);
    const vis = Math.max(size, minSize);
    const pos = content > 0 ? v.scrollTop / content : 0;
    const vpos = size < minSize ? (pos / Math.max(1e-6, 1 - size)) * (1 - minSize) : pos;
    this._thumbH = vis * h;
    const thumb = this.$(".thumb");
    thumb.style.top = `${vpos * h}px`;
    thumb.style.height = `${vis * h}px`;
  }
  /** Whether the content is taller than the area. */
  get overflowing() {
    return this.view ? this.$(".col").offsetHeight > this.view.clientHeight + 0.5 : false;
  }
  /** Scroll the least needed to show content from `y` (px from the content's top) to `y + h`. */
  ensureVisibleRange(y, h) {
    const v = this.view;
    const top = v.scrollTop;
    const bottom = top + v.clientHeight;
    const maxY = Math.max(0, v.scrollHeight - v.clientHeight);
    if (y < top) v.scrollTop = Math.max(0, y);
    else if (y + h > bottom) v.scrollTop = Math.min(maxY, y + h - v.clientHeight);
  }
  /** Scroll the least needed to show `el` (a child, or anything inside one). */
  ensureVisible(el) {
    if (!el || !this.view) return;
    const r = el.getBoundingClientRect();
    const c = this.$(".col").getBoundingClientRect();
    this.ensureVisibleRange(r.top - c.top, r.height);
  }
  update() {
    const mh = this.maxHeight;
    this.view.style.maxHeight = Number.isFinite(mh) ? `${mh}px` : "";
    const sp = String(this.spacing).trim();
    this.style.setProperty("--gap", /^-?\d+(\.\d+)?$/.test(sp) ? `${sp}px` : sp);
    this._sync();
  }
}

// ---------------------------------------------------------------------------------------------
// KeyValue: dim labels beside their values, `pair-columns` pairs to a line.

export class KeyValue extends OmarchyElement {
  static props = { pairs: ["json", () => []], pairColumns: ["number", 1] };
  static css = `
:host { display: inline-grid; justify-content: start; column-gap: ${space(20)}; row-gap: var(--omarchy-space-label-gap);
  font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, normal); }
.l { color: var(--omarchy-text-dim); white-space: pre; }
.v { color: var(--omarchy-foreground); white-space: pre; }
`;
  update() {
    this.style.gridTemplateColumns = `repeat(${Math.max(1, this.pairColumns) * 2}, auto)`;
    this.shadowRoot.innerHTML = (this.pairs || [])
      .map((p) => `<span class="l">${esc(p.label)}</span><span class="v" style="color: ${tone(p.tone === "muted" ? "" : p.tone, "var(--omarchy-foreground)")}">${esc(p.value)}</span>`)
      .join("");
  }
}

// ---------------------------------------------------------------------------------------------
// Hero: a panel's head: a big glyph, a bold title, an uppercased status line, a trailing control
// (slot="trailing").

export class Hero extends OmarchyElement {
  static props = { glyph: ["string", ""], title: ["string", ""], status: ["string", ""], error: ["bool"], glyphOpacity: ["number", 1] };
  static css = `${OG_CSS}
:host { display: flex; align-items: center; min-width: 0; }
.glyph-slot {
  width: var(--omarchy-font-display);
  height: max(var(--omarchy-font-display-large), round(up, calc(var(--omarchy-font-display) * 1.35), 1px));
  margin-right: ${space(14)};
  color: var(--omarchy-foreground);
  ${ogFont("display")}
}
.glyph-slot.none { display: none; }
.labels { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: ${space(2)}; }
.title { font-size: var(--omarchy-font-title); line-height: var(--omarchy-line-title, normal); font-weight: bold; }
.status { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); font-weight: bold; letter-spacing: 1.2px;
  text-transform: uppercase; color: var(--omarchy-text-dim); }
:host([error]) .status { color: var(--omarchy-urgent); }
.title, .status { white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.status:empty { display: none; }
.trailing { flex: none; display: flex; align-items: center; margin-left: ${space(12)}; }
.trailing.none { display: none; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="og glyph-slot"><span class="glyph"></span></div><div class="labels"><div class="title"></div><div class="status"></div></div><div class="trailing"><slot name="trailing"></slot><slot></slot></div>`;
    for (const s of this.shadowRoot.querySelectorAll("slot")) s.addEventListener("slotchange", () => this.update());
  }
  update() {
    const slot = this.$(".glyph-slot");
    const g = this.$(".glyph");
    g.textContent = this.glyph;
    slot.classList.toggle("none", !this.glyph);
    slot.style.opacity = String(this.glyphOpacity);
    if (this.glyph) inkCenter(g);
    this.$(".title").textContent = this.title;
    this.$(".status").textContent = this.status;
    const any = [...this.shadowRoot.querySelectorAll("slot")].some((s) => s.assignedElements().length);
    this.$(".trailing").classList.toggle("none", !any);
  }
}

// ---------------------------------------------------------------------------------------------
// Badge: a count or short label in a small pill.

export class Badge extends OmarchyElement {
  static props = { text: ["string", ""], tone: ["string", ""], filled: ["bool"] };
  static css = `
:host {
  --line: var(--omarchy-control-normal-border-top);
  display: inline-flex; align-items: center; justify-content: center; position: relative; vertical-align: middle; box-sizing: border-box;
  padding: calc(${space(1)} + var(--line)) calc(${space(6)} + var(--line));
  min-width: calc(var(--omarchy-line-caption, 13px) + 2 * (${space(1)} + var(--line)));
  border-radius: var(--omarchy-radius-small);
  font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); font-weight: bold; white-space: pre;
  background: ${alpha("var(--tone)", "var(--omarchy-control-selected-fill-alpha)")}; color: var(--tone);
}
:host([data-neutral]) { background: var(--omarchy-control-normal-fill); }
:host([data-neutral])::after { content: ""; position: absolute; inset: 0; border-radius: inherit; pointer-events: none;
  border-style: solid; border-color: var(--omarchy-control-normal-border); border-width: var(--omarchy-control-normal-border-width); }
:host([filled]) { background: var(--tone); color: var(--omarchy-background); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<span></span>`;
  }
  update() {
    this.shadowRoot.querySelector("span").textContent = this.text;
    const t = this.tone === "muted" ? "" : this.tone;
    this.style.setProperty("--tone", tone(t, "var(--omarchy-foreground)"));
    this.toggleAttribute("data-neutral", !t && !this.filled);
  }
}

// ---------------------------------------------------------------------------------------------
// StatusDot: a dot ringed in the surface color. Always a circle.

export class StatusDot extends OmarchyElement {
  static props = { tone: ["string", "success"], size: ["number", 0], ring: ["number", -1], ground: ["string", null] };
  static css = `
:host {
  --size: ${space(8)}; --ring: max(1px, ${space(2)});
  display: inline-block; flex: none; vertical-align: middle; box-sizing: border-box;
  width: calc(var(--size) + 2 * var(--ring)); height: calc(var(--size) + 2 * var(--ring));
  border-radius: 50%; background: var(--ground, var(--omarchy-popups-background)); padding: var(--ring);
}
:host([ring="0"]) { background: transparent; }
.dot { width: var(--size); height: var(--size); border-radius: 50%; background: var(--tone); }
`;
  setup() {
    this.shadowRoot.innerHTML = `<div class="dot"></div>`;
    this.setAttribute("role", "img");
  }
  update() {
    this.style.setProperty("--tone", tone(this.tone, "var(--omarchy-foreground)"));
    if (this.size > 0) this.style.setProperty("--size", `${this.size}px`);
    if (this.ring >= 0) this.style.setProperty("--ring", `${this.ring}px`);
    if (this.ground) this.style.setProperty("--ground", this.ground);
    else this.style.removeProperty("--ground");
    this.setAttribute("aria-label", this.tone);
  }
}

// ---------------------------------------------------------------------------------------------
// Avatar: a picture, else an icon by name, else a glyph, else the name's initial; an optional
// docked StatusDot.

const AVATAR_SIZES = { small: "icon-large", medium: "display-large" };

export class Avatar extends OmarchyElement {
  static props = {
    name: ["string", ""],
    source: ["string", ""],
    iconName: ["string", ""],
    glyph: ["string", ""],
    size: ["string", "medium"],
    pixelSize: ["number", 0],
    round: ["bool"],
    status: ["string", ""],
    ground: ["string", null],
  };
  static css = `
:host { display: inline-block; position: relative; flex: none; vertical-align: middle; width: var(--px); height: var(--px);
  --corner: min(var(--omarchy-radius), calc(var(--px) / 2)); }
:host([round]) { --corner: calc(var(--px) / 2); }
.pic, .icon, .tile { position: absolute; inset: 0; width: 100%; height: 100%; display: none; }
.pic { object-fit: cover; border-radius: var(--corner); }
.icon { object-fit: contain; }
.tile { border-radius: var(--corner); background: var(--omarchy-control-selected-fill); align-items: center; justify-content: center; }
.mark { font-size: round(calc(var(--px) * 0.5), 1px); line-height: normal; white-space: pre; color: var(--omarchy-foreground); }
.mark.bold { font-weight: bold; }
.mark.og-glyph { position: absolute; left: 50%; top: 50%; transform: translateY(-50%); }
.show { display: block; }
.tile.show { display: flex; }
omarchy-status-dot { position: absolute; display: none; --size: max(${space(6)}, round(calc(var(--px) * 0.28), 1px));
  right: calc(-1 * var(--ring, max(1px, ${space(2)}))); bottom: calc(-1 * var(--ring, max(1px, ${space(2)}))); }
omarchy-status-dot.show { display: inline-block; }
`;
  setup() {
    this.shadowRoot.innerHTML = `<img class="pic" alt=""><img class="icon" alt=""><div class="tile"><span class="mark"></span></div><omarchy-status-dot></omarchy-status-dot>`;
    this.setAttribute("role", "img");
    this._pic = { src: "", state: "none" };
    this._icon = { key: "", url: null, state: "none" };
    const pic = this.$(".pic");
    pic.addEventListener("load", () => this._picDone("ok"));
    pic.addEventListener("error", () => this._picDone("failed"));
    const icon = this.$(".icon");
    icon.addEventListener("load", () => this._iconDone("ok"));
    icon.addEventListener("error", () => this._iconDone("failed"));
  }
  _picDone(state) {
    if (this._pic.state === "loading") pending(-1);
    this._pic.state = state;
    this.update();
  }
  _iconDone(state) {
    if (this._icon.state === "loading") pending(-1);
    this._icon.state = state;
    this.update();
  }
  /** The avatar's size in px: pixel-size, else small / medium / large from the font tokens. */
  get pixels() {
    if (this.pixelSize > 0) return this.pixelSize;
    const f = (omarchyApi().tokens || {}).font || {};
    if (this.size === "small") return f.iconLarge || 18;
    if (this.size === "large") return (f.display || 24) * 2;
    return f.displayLarge || 28;
  }
  get initial() {
    const text = String(this.name || "").trim();
    if (!text) return "";
    const code = text.charCodeAt(0);
    return (code >= 0xd800 && code < 0xdc00 ? text.slice(0, 2) : text.charAt(0)).toUpperCase();
  }
  _lookupIcon(px) {
    const api = omarchyApi();
    const devicePx = Math.ceil(px * (window.devicePixelRatio || 1));
    const key = this.iconName ? `${api.iconTheme}\n${devicePx}\n${this.iconName}` : "";
    if (key === this._icon.key) return;
    if (this._icon.state === "loading" || this._icon.state === "lookup") pending(-1);
    this._icon = { key, url: null, state: key ? "lookup" : "none" };
    if (!key) return;
    pending(1);
    api
      .icon(this.iconName, devicePx)
      .catch(() => null)
      .then((url) => {
        if (this._icon.key !== key) return;
        pending(-1);
        this._icon.url = url;
        this._icon.state = url ? "loading" : "failed";
        if (url) pending(1);
        const img = this.$(".icon");
        if (url) img.src = url;
        else img.removeAttribute("src");
        this.update();
      });
  }
  update() {
    const px = this.pixels;
    this.style.setProperty(
      "--px",
      this.pixelSize > 0 ? `${px}px` : this.size === "large" ? "calc(var(--omarchy-font-display) * 2)" : `var(--omarchy-font-${AVATAR_SIZES[this.size] || AVATAR_SIZES.medium})`,
    );

    const pic = this.$(".pic");
    if (this.source !== this._pic.src) {
      if (this._pic.state === "loading") pending(-1);
      this._pic = { src: this.source, state: this.source ? "loading" : "none" };
      if (this.source) {
        pending(1);
        pic.src = this.source;
      } else pic.removeAttribute("src");
    }
    this._lookupIcon(px);

    const pictured = this._pic.state === "ok";
    const iconed = !pictured && this._icon.state === "ok";
    pic.classList.toggle("show", pictured);
    this.$(".icon").classList.toggle("show", iconed);
    this.$(".tile").classList.toggle("show", !pictured && !iconed);
    const mark = this.$(".mark");
    mark.textContent = this.glyph || this.initial;
    mark.classList.toggle("bold", !this.glyph);
    mark.classList.toggle("og-glyph", !!this.glyph);
    if (this.glyph) inkCenter(mark);
    else mark.style.marginLeft = "";

    const dot = this.$("omarchy-status-dot");
    dot.classList.toggle("show", !!this.status);
    if (this.status) dot.setAttribute("tone", this.status);
    if (this.ground) dot.setAttribute("ground", this.ground);
    else dot.removeAttribute("ground");
    this.setAttribute("aria-label", this.name);
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    // A load in flight no longer holds a still render.
    if (this._pic && this._pic.state === "loading") {
      pending(-1);
      this._pic.state = "none";
      this._pic.src = "";
    }
    if (this._icon && (this._icon.state === "loading" || this._icon.state === "lookup")) {
      pending(-1);
      this._icon = { key: "", url: null, state: "none" };
    }
  }
}

// ---------------------------------------------------------------------------------------------
// TileGrid: tiles of a glyph (or a medium Avatar) above a label of up to two lines.

export class TileGrid extends OmarchyElement {
  static props = { tiles: ["json", () => []], columns: ["number", 4], cellWidth: ["number", 0], cursorIndex: ["number", -1], currentIndex: ["number", -1] };
  static css = `${SURFACE_CSS}${OG_CSS}
:host { --cell: ${space(88)}; --icon-h: max(var(--omarchy-line-display, 32px), var(--omarchy-font-display-large)); display: inline-grid; grid-auto-rows: auto; vertical-align: top; }
.tile {
  width: var(--cell);
  height: calc(2 * var(--omarchy-space-md) + var(--icon-h) + var(--omarchy-space-md) + 2 * var(--omarchy-line-body-small, 15px));
  box-sizing: border-box; padding: 0 var(--omarchy-space-md); cursor: pointer; user-select: none; -webkit-user-select: none;
  display: flex; flex-direction: column; align-items: center; justify-content: center; gap: var(--omarchy-space-md);
}
.icon { width: 100%; height: var(--icon-h); flex: none; color: var(--omarchy-foreground); ${ogFont("display")} }
.icon.av { display: flex; align-items: center; justify-content: center; }
.label { box-sizing: border-box; width: 100%; text-align: center; color: var(--omarchy-foreground);
  font-size: var(--omarchy-font-body-small); line-height: var(--omarchy-line-body-small, 15px);
  max-height: calc(2 * var(--omarchy-line-body-small, 15px)); overflow: hidden; white-space: normal; overflow-wrap: break-word;
  display: -webkit-box; -webkit-box-orient: vertical; -webkit-line-clamp: 2; }
`;
  setup() {
    this.setAttribute("role", "grid");
    this.shadowRoot.addEventListener("click", (e) => {
      const t = e.target.closest(".tile");
      if (t) this.emit("activate", { index: Number(t.dataset.index) });
    });
    this.shadowRoot.addEventListener("pointermove", (e) => {
      const t = e.target.closest(".tile");
      if (t) this.emit("pointermoved", { index: Number(t.dataset.index), clientX: e.clientX, clientY: e.clientY, event: e });
    });
  }
  update() {
    this.style.gridTemplateColumns = `repeat(${Math.max(1, this.columns)}, var(--cell))`;
    if (this.cellWidth > 0) this.style.setProperty("--cell", `${this.cellWidth}px`);
    const tiles = this.tiles || [];
    const key = JSON.stringify(tiles);
    if (this._rendered !== key) {
      this._rendered = key;
      this.shadowRoot.innerHTML = tiles
        .map((t, i) => {
          const avatared = t.source !== undefined || t.name !== undefined;
          const icon = avatared
            ? `<div class="icon av"><omarchy-avatar name="${esc(t.name || t.label || "")}" source="${esc(t.source || "")}" glyph="${esc(t.glyph || "")}"></omarchy-avatar></div>`
            : `<div class="icon og"><span class="g">${esc(t.glyph || "")}</span></div>`;
          return `<div class="tile surface" role="gridcell" data-index="${i}">${icon}<div class="label">${esc(t.label || "")}</div></div>`;
        })
        .join("");
    }
    this.shadowRoot.querySelectorAll(".tile").forEach((el, i) => {
      el.classList.toggle("cursor", i === this.cursorIndex);
      el.classList.toggle("current", i === this.currentIndex);
      el.setAttribute("aria-selected", String(i === this.currentIndex));
    });
    this.shadowRoot.querySelectorAll(".g").forEach((g) => inkCenter(g));
  }
}

export const elements = {
  "omarchy-list-row": ListRow,
  "omarchy-scroll-area": ScrollArea,
  "omarchy-key-value": KeyValue,
  "omarchy-hero": Hero,
  "omarchy-badge": Badge,
  "omarchy-status-dot": StatusDot,
  "omarchy-avatar": Avatar,
  "omarchy-tile-grid": TileGrid,
};

