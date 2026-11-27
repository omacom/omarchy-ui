// SearchList (spec/components/composed.md): the type-ahead overlay: a filter line, the matching
// rows under a keyboard cursor, an optional detail pane, and an empty state. Drawn as
// qt/qml/Omarchy/Ui/SearchList.qml draws it, on ListRow, CursorModel, ScrollArea and EmptyState.
// Filtering is the caller's: it shows exactly the `items` it is given.

import { OmarchyElement, esc, surfaceBorder } from "./base.js";
import { space } from "./looks.js";
import { CursorModel } from "./lists.js";

export class SearchList extends OmarchyElement {
  static delegatesFocus = true;
  static props = {
    query: ["string", ""],
    placeholder: ["string", ""],
    items: ["json", () => []],
    showDetail: ["bool"],
    detailWidth: ["number", 0],
    emptyGlyph: ["string", "\u{F0349}"],
    emptyText: ["string", ""],
    noMatchesText: ["string", ""],
  };
  static css = `
:host { display: block; position: relative; width: ${space(480)}; height: ${space(360)}; }
.card {
  position: absolute; inset: 0; box-sizing: border-box; display: flex; flex-direction: column;
  background: var(--omarchy-menu-background); color: var(--omarchy-menu-text);
  ${surfaceBorder("menu")}
  border-radius: var(--omarchy-radius);
  padding: var(--omarchy-space-panel-padding);
}
.header { flex: none; position: relative; height: max(${space(34)}, calc(var(--omarchy-font-heading) + 2 * var(--omarchy-space-control-padding-y))); display: flex; align-items: center; }
input {
  all: unset; box-sizing: border-box; width: 100%;
  font-family: var(--omarchy-font-family); font-size: var(--omarchy-font-heading); line-height: var(--omarchy-line-heading, normal);
  color: var(--omarchy-menu-text); caret-color: transparent;
}
input::placeholder { color: var(--omarchy-text-faint); opacity: 1; }
input::selection { background: transparent; }
.body { flex: 1; min-height: 0; position: relative; display: flex; margin-top: var(--omarchy-space-md); }
omarchy-scroll-area { flex: none; height: 100%; }
omarchy-scroll-area::part(view) { max-height: 100%; }
.rule { flex: none; width: 1px; background: var(--omarchy-track); margin: 0 var(--omarchy-space-xxl); }
.detail { flex: 1; min-width: 0; overflow: hidden; display: flex; flex-direction: column; gap: var(--omarchy-space-sm); padding-top: calc(var(--omarchy-space-row-padding-x) / 2); }
.detail .t { font-size: var(--omarchy-font-title); line-height: var(--omarchy-line-title, normal); font-weight: bold; color: var(--omarchy-menu-text);
  white-space: pre; overflow: hidden; text-overflow: ellipsis; }
.detail .d { font-size: var(--omarchy-font-body); line-height: var(--omarchy-line-body, normal); color: var(--omarchy-text-dim); overflow-wrap: anywhere; }
.hide { display: none !important; }
omarchy-empty-state { position: absolute; left: 0; right: 0; top: 50%; transform: translateY(-50%); }
::slotted([slot="detail"]) { display: block; }
`;

  setup() {
    this.shadowRoot.innerHTML = `<div class="card"><div class="gradient-ring"></div>
  <div class="header"><input part="filter" spellcheck="false" autocomplete="off" aria-autocomplete="list"></div>
  <div class="body">
    <omarchy-scroll-area role="listbox"></omarchy-scroll-area>
    <div class="rule"></div>
    <div class="detail"><div class="t"></div><div class="d"></div><slot name="detail"></slot></div>
    <omarchy-empty-state size="overlay"></omarchy-empty-state>
  </div>
</div>`;
    const input = (this.input = this.$("input"));
    const scroll = (this.scroll = this.$("omarchy-scroll-area"));
    scroll.setAttribute("spacing", "var(--omarchy-space-xxs)");
    const cursor = (this.cursor = new CursorModel({ sections: [{ name: "results", count: 0 }] }));
    cursor.cursorActive = true;
    cursor.addEventListener("change", () => this.renderCursor());
    cursor.addEventListener("moved", (e) => {
      this.renderCursor();
      if (e.detail.byKey) scroll.ensureVisible(this.rows()[e.detail.index]);
    });
    cursor.addEventListener("activated", (e) => this.items.length && this.emit("activate", { index: e.detail.index, item: this.items[e.detail.index] }));
    cursor.addEventListener("deleterequested", (e) => this.items.length && this.emit("delete", { index: e.detail.index, item: this.items[e.detail.index] }));

    // The filter line holds focus; the list sees every key first (Qt's keyHandler).
    input.addEventListener("keydown", (e) => {
      if (e.altKey || e.metaKey) {
        e.preventDefault();
        return;
      }
      if (e.key === "Escape") {
        e.preventDefault();
        if (this.query !== "") this.setQuery("");
        else this.emit("close");
        return;
      }
      if (e.key === " " || cursor.printable(e) !== "" || e.key === "Backspace") return; // text
      if (["ArrowLeft", "ArrowRight"].includes(e.key)) {
        e.preventDefault(); // the line only grows at its end
        return;
      }
      if (["ArrowUp", "ArrowDown", "PageUp", "PageDown", "Home", "End", "Enter", "Delete", "Tab"].includes(e.key)) {
        if (e.key === "Tab") return;
        e.preventDefault();
        cursor.handleKey(e);
      }
    });
    input.addEventListener("input", () => this.setQuery(input.value));
    // The caret stays at the end; no selection.
    const toEnd = () => {
      const n = input.value.length;
      if (input.selectionStart !== n || input.selectionEnd !== n) input.setSelectionRange(n, n);
    };
    input.addEventListener("select", toEnd);
    input.addEventListener("click", toEnd);
    document.addEventListener("selectionchange", () => this.shadowRoot.activeElement === input && toEnd());
    // Inner elements' own events stay inside.
    for (const type of ["change", "input"]) input.addEventListener(type, (e) => e.stopPropagation());

    scroll.addEventListener("pointermove", (e) => {
      const row = e.target.closest && e.target.closest("omarchy-list-row");
      if (row) cursor.pointerMoved(e, "results", Number(row.dataset.index));
    });
    scroll.addEventListener("click", (e) => {
      const row = e.target.closest && e.target.closest("omarchy-list-row");
      if (!row) return;
      const i = Number(row.dataset.index);
      cursor.set("results", i);
      this.renderCursor();
      this.emit("activate", { index: i, item: this.items[i] });
    });
    scroll.addEventListener("mousedown", (e) => {
      e.preventDefault();
      input.focus();
    });
  }

  /** The plain case-insensitive substring test, for callers with nothing better. */
  matches(text, query) {
    return SearchList.matches(text, query);
  }
  static matches(text, query) {
    return String(text || "").toLowerCase().includes(String(query || "").toLowerCase());
  }

  get cursorIndex() {
    return this.cursor && this.cursor.cursorActive && this.items.length > 0 ? this.cursor.selectedIndex : -1;
  }
  get currentItem() {
    const i = this.cursorIndex;
    return i >= 0 && i < this.items.length ? this.items[i] : null;
  }
  focus(opts) {
    this.input ? this.input.focus(opts) : super.focus(opts);
  }

  setQuery(q) {
    if (q === this.query) return;
    this.query = q;
    this.emit("query", { query: q });
  }

  rows() {
    return [...this.scroll.querySelectorAll("omarchy-list-row")];
  }

  renderCursor() {
    const idx = this.cursorIndex;
    this.rows().forEach((r, i) => (r.cursor = i === idx));
    const item = this.currentItem;
    this.$(".detail .t").textContent = item ? item.title || "" : "";
    this.$(".detail .d").textContent = item ? item.detail || "" : "";
  }

  update() {
    const input = this.input;
    if (input.value !== this.query) input.value = this.query;
    input.placeholder = this.placeholder || this.t("common.search", "Search...");
    if (this.query !== this._lastQuery) {
      this._lastQuery = this.query;
      this.cursor.selectedIndex = 0; // a new query starts at the first match
    }
    const items = this.items || [];
    const key = JSON.stringify(items);
    if (this._rendered !== key) {
      this._rendered = key;
      this.scroll.innerHTML = items
        .map(
          (it, i) =>
            `<omarchy-list-row role="option" data-index="${i}" glyph="${esc(it.glyph || "")}" title="${esc(it.title || "")}" subtitle="${esc(it.subtitle || "")}" value="${esc(it.value || "")}"></omarchy-list-row>`,
        )
        .join("");
      this.cursor.setCount("results", items.length);
      this.cursor.settle();
    }
    const has = items.length > 0;
    const detail = this.showDetail && has;
    this.$(".rule").classList.toggle("hide", !detail);
    this.$(".detail").classList.toggle("hide", !detail);
    this.scroll.classList.toggle("hide", !has);
    // The list's width: the body less the pane, its gaps and the rule (Qt's _listWidth).
    const body = this.$(".body");
    const bw = body.clientWidth;
    const gap = parseFloat(getComputedStyle(this).getPropertyValue("--omarchy-space-xxl")) || 12;
    const pane = this.detailWidth > 0 ? this.detailWidth : Math.floor(bw * 0.45);
    this.scroll.style.width = detail ? `${bw - pane - gap * 2 - 1}px` : "100%";
    this.scroll.style.maxWidth = detail ? "" : "100%";
    const empty = this.$("omarchy-empty-state");
    empty.classList.toggle("hide", has);
    empty.setAttribute("glyph", this.emptyGlyph);
    const noMatches = this.noMatchesText || this.t("common.noMatchesFor", "No matches for “%1”");
    empty.setAttribute("title", this.query !== "" ? noMatches.replace(/%1/g, this.query) : this.emptyText);
    this.renderCursor();
    if (!this._sized) {
      this._sized = true;
      new ResizeObserver(() => this.update()).observe(body);
    }
  }
}

export const elements = { "omarchy-search-list": SearchList };
