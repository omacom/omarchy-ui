// Swatches, state chips and the type scale (qt/gallery/sections/tokens.qml).
const swatches = ["foreground", "background", "accent", "urgent", "muted"];
const states = [
  ["normal", "normal"],
  ["hover-cursor", "hover"],
  ["focus", "focus"],
  ["selected", "selected"],
];
const sizes = ["displayLarge", "display", "heading", "title", "subtitle", "body", "bodySmall", "caption"];
const kebab = (s) => s.replace(/[A-Z]/g, (c) => "-" + c.toLowerCase());

export default {
  title: "Tokens",
  html: (omarchy) => `
<style>
  .tk-flow { display: flex; flex-wrap: wrap; align-items: flex-start; gap: var(--omarchy-space-lg); }
  .tk-swatch { display: flex; flex-direction: column; align-items: flex-start; gap: var(--omarchy-space-xs); }
  .tk-swatch > div { width: 64px; height: 32px; box-sizing: border-box; border: 1px solid var(--omarchy-control-normal-border); border-radius: var(--omarchy-radius-small); background-clip: padding-box; }
  .tk-swatch > span { font-size: var(--omarchy-font-caption); line-height: var(--omarchy-line-caption, normal); }
  .tk-chip { width: 120px; height: var(--omarchy-space-control-height); box-sizing: border-box; display: flex; align-items: center; justify-content: center;
    border-style: solid; border-radius: var(--omarchy-radius); background-clip: padding-box; font-size: var(--omarchy-font-body); }
  .tk-scale { display: flex; flex-direction: column; align-items: flex-start; gap: var(--omarchy-space-xs); }
</style>
<div class="tk-flow fill">
  ${swatches.map((s) => `<div class="tk-swatch"><div style="background-color: var(--omarchy-${s})"></div><span class="text">${s}</span></div>`).join("")}
</div>
<div class="tk-flow fill">
  ${states
    .map(
      ([label, s]) =>
        `<div class="tk-chip text" style="background-color: var(--omarchy-control-${s}-fill); border-color: var(--omarchy-control-${s}-border); border-width: var(--omarchy-control-${s}-border-width);${s === "selected" ? " font-weight: bold; color: var(--omarchy-control-selected-text);" : ""}">${label}</div>`,
    )
    .join("")}
</div>
<div class="tk-scale">
  ${sizes.map((s) => `<span class="text" style="font-size: var(--omarchy-font-${kebab(s)}); line-height: var(--omarchy-line-${kebab(s)}, normal)">${s} ${omarchy.tokens ? omarchy.tokens.font[s] : ""}</span>`).join("")}
</div>`,
};
