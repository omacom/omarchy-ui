// Feedback: Meter, ProgressBar, Spinner, EmptyState, Banner, Toast (spec/components/feedback.md).

const fired = async (t, tag, type) => (await t.events()).filter((e) => e[0] === tag && e[1] === type).length;

export const tests = {
  async meter_fills_clamps_and_turns_urgent_when_alarming(t) {
    await t.mount(`<omarchy-meter id="m" style="width: 200px" value="0.5" markers="[0.5, 0.8]"></omarchy-meter>`);
    const fill = () => t.eval(`__q("#m >>> .fill").getBoundingClientRect().width`);
    t.assert.equal(await fill(), 100);
    t.assert.equal(await t.eval(`__q("#m >>> .ticks").childElementCount`), 2);
    await t.eval(`__q("#m").value = 1.7`);
    await t.frame();
    t.assert.equal(await fill(), 200, "clamped to 1");
    const urgent = await t.eval(`(() => { const d = document.createElement("div"); d.style.color = "var(--omarchy-urgent)"; document.body.append(d); const c = getComputedStyle(d).color; d.remove(); return c; })()`);
    await t.eval(`__q("#m").alarming = true`);
    await t.frame();
    t.assert.equal(await t.eval(`getComputedStyle(__q("#m >>> .fill")).backgroundColor`), urgent);
    t.assert.equal(await t.eval(`__q("#m").getAttribute("aria-valuenow")`), "100");
  },

  async progress_bar_is_accent_and_holds_still_mid_track_under_reduced_motion(t) {
    await t.mount(`<omarchy-progress-bar id="p" style="width: 400px" indeterminate></omarchy-progress-bar>`);
    const seg = await t.eval(`(() => { const s = __q("#p >>> .segment").getBoundingClientRect(); const p = __q("#p").getBoundingClientRect(); return [s.x - p.x, s.width]; })()`);
    t.assert.deepEqual(seg, [150, 100], "a quarter of the track, at 3/8");
    t.assert.equal(await t.eval(`__q("#p >>> .segment").getAnimations().length`), 0, "no sweep under reduced motion");
  },

  async spinner_box_is_its_line_height_and_still_under_reduced_motion(t) {
    await t.mount(`<omarchy-spinner id="s"></omarchy-spinner><omarchy-spinner id="p" pulse size="icon-large"></omarchy-spinner>`);
    const s = await t.box("#s");
    t.assert.equal(s.width, s.height, "square");
    t.assert.ok(s.height >= 14, `the line height of icon (${s.height})`);
    t.assert.equal(await t.eval(`__q("#s >>> .spin").getAnimations().length`), 0);
    t.assert.equal(await t.eval(`getComputedStyle(__q("#p >>> .spin")).opacity`), "0.5", "a still pulse rests at half");
  },

  async empty_state_action_runs_on_click_and_keys(t) {
    await t.mount(`<omarchy-empty-state id="e" glyph="\u{f05aa}" title="No networks found" hint="Turn on Wi-Fi" action-text="Scan"></omarchy-empty-state>`);
    await t.click("#e >>> omarchy-button");
    await t.eval(`__q("#e >>> omarchy-button").focus()`);
    await t.key("Enter");
    t.assert.equal(await fired(t, "omarchy-empty-state", "action"), 2);
    await t.eval(`__q("#e").actionText = ""`);
    await t.frame();
    t.assert.equal(await t.eval(`getComputedStyle(__q("#e >>> omarchy-button")).display`), "none", "no action, no button");
  },

  async banner_action_and_dismiss(t) {
    await t.mount(`<omarchy-banner id="b" style="width: 500px" tone="warning" text="Battery is low" action-text="Settings" dismissible></omarchy-banner>`);
    t.assert.equal(await t.eval(`__q("#b").getAttribute("role")`), "alert");
    await t.click("#b >>> omarchy-button");
    t.assert.equal(await fired(t, "omarchy-banner", "action"), 1);
    await t.click("#b >>> .dismiss");
    t.assert.equal(await fired(t, "omarchy-banner", "dismiss"), 1);
    // Keyboard: Tab reaches the action, then the dismiss; Space and Return press them.
    await t.eval(`document.activeElement && document.activeElement.blur()`);
    await t.eval(`__q("#b >>> omarchy-button").focus()`);
    await t.key(" ");
    await t.key("Tab");
    await t.key("Enter");
    t.assert.equal(await fired(t, "omarchy-banner", "action"), 2);
    t.assert.equal(await fired(t, "omarchy-banner", "dismiss"), 2);
    t.assert.equal(await t.eval(`__q("#b >>> .glyph").textContent`), "\u{f002a}", "the tone's glyph");
  },

  async banner_trailing_padding_and_tone_glyphs(t) {
    await t.mount(`<omarchy-banner id="a" text="x"></omarchy-banner><omarchy-banner id="b" text="x" dismissible tone="error"></omarchy-banner><omarchy-banner id="c" tone="neutral" glyph="*" text="x"></omarchy-banner>`);
    const pad = (s) => t.eval(`getComputedStyle(__q("${s}")).paddingRight`);
    t.assert.equal(await pad("#a"), "10px", "controlPaddingX without trailing parts");
    t.assert.equal(await pad("#b"), "4px", "sm with a dismiss");
    t.assert.equal(await t.eval(`__q("#b >>> .glyph").textContent`), "\u{f015a}");
    t.assert.equal(await t.eval(`__q("#c >>> .glyph").textContent`), "*", "glyph replaces the tone's");
    t.assert.equal(await t.eval(`getComputedStyle(__q("#a")).borderTopColor === getComputedStyle(__q("#c")).borderTopColor`), true, "neutral draws as info");
  },

  async toast_action_countdown_and_cursor(t) {
    await t.mount(`<omarchy-toast id="t" glyph="\u{f012c}" title="Export finished" text="demo.mp4" action-text="Open" countdown="0.5"></omarchy-toast>`);
    await t.click("#t >>> omarchy-button");
    t.assert.equal(await fired(t, "omarchy-toast", "action"), 1);
    const line = await t.eval(`(() => { const c = __q("#t >>> .countdown").getBoundingClientRect(); const h = __q("#t").getBoundingClientRect(); return [Math.round(c.width), Math.round(h.width)]; })()`);
    t.assert.equal(line[0], Math.round((line[1] - 4) * 0.5), "countdown × the inner width");
    await t.eval(`__q("#t").countdown = -1; __q("#t").cursor = true`);
    await t.frame();
    t.assert.equal(await t.eval(`getComputedStyle(__q("#t >>> .countdown")).display`), "none", "below 0 hides the line");
    t.assert.equal(await t.eval(`getComputedStyle(__q("#t >>> .tint")).display`), "block", "the cursor tints it");
  },
};
