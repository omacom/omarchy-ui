// qt/gallery/sections/pickers.qml
export default {
  title: "Pickers",
  html: `
<div class="row" style="gap: var(--omarchy-space-xxl); align-items: flex-start">
  <omarchy-multi-select label="Bar modules" options='["Clock","Battery","Network","Volume","Bluetooth"]' values='["Clock","Battery","Network","Volume"]'></omarchy-multi-select>
  <div style="height: 280px">
    <omarchy-multi-select id="pickers-open" label="Notify for" options='["Calendar","Mail","Messages","Updates","Battery"]' values='["Mail","Updates"]'></omarchy-multi-select>
  </div>
</div>
<div class="row" style="gap: var(--omarchy-space-control-gap)">
  <omarchy-color-well swatch="var(--omarchy-accent)"></omarchy-color-well>
  <omarchy-color-well swatch="var(--omarchy-success)" cursor></omarchy-color-well>
  <omarchy-color-well swatch="var(--omarchy-urgent)"></omarchy-color-well>
</div>
<omarchy-swatch-strip current-index="2" colors='["var(--omarchy-accent)","var(--omarchy-urgent)","var(--omarchy-warning)","var(--omarchy-success)","var(--omarchy-foreground)","var(--omarchy-text-dim)","var(--omarchy-text-faint)","var(--omarchy-muted)"]'></omarchy-swatch-strip>
<div style="position: relative; width: 420px; height: 240px">
  <omarchy-card title="Displays" style="position: absolute; inset: 0">
    <div class="col fill">
      <omarchy-menu-row text="Built-in display" checked></omarchy-menu-row>
      <omarchy-menu-row text="External monitor"></omarchy-menu-row>
      <omarchy-menu-row text="Projector"></omarchy-menu-row>
    </div>
  </omarchy-card>
  <omarchy-dialog open data-no-autofocus title="Discard changes?" message="Your edits to Tokyo Night will be lost." secondary-text="Keep editing" primary-text="Discard" destructive></omarchy-dialog>
</div>`,
  async setup(body) {
    // Held open, as Qt's gallery holds it: once the page is laid out, which the popup clamps to.
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
    body.querySelector("#pickers-open").open();
  },
};
