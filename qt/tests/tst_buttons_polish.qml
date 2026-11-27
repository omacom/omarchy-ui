import QtQuick
import QtTest
import Omarchy.Theme as Theme
import Omarchy.Ui as O

TestCase {
  id: testCase
  name: "VisualPolish"
  width: 600
  height: 400
  visible: true
  when: windowShown

  Component {
    id: textButtonComponent
    O.Button { text: "Bordered"; bordered: true }
  }

  Component {
    id: iconButtonLabelComponent
    O.Button { text: "Icon"; iconText: ""; bordered: true }
  }

  Component {
    id: actionButtonComponent
    O.PanelActionButton { iconText: ""; bordered: true }
  }

  Component {
    id: sliderComponent
    O.PanelSlider { width: 200; value: 0.5; tickCount: 5 }
  }

  Component {
    id: tileGridComponent
    O.TileGrid {
      columns: 2
      tiles: [
        { glyph: "\uf0ac", label: "Browser" },
        { glyph: "\uf03d", label: "Screen Recorder" }
      ]
    }
  }

  Component {
    id: heroWifiComponent
    O.Hero { width: 320; glyph: "\uf0928"; title: "Wi-Fi"; status: "Connected" }
  }

  Component {
    id: heroBtComponent
    O.Hero { width: 320; glyph: "\uf00b2"; title: "Bluetooth"; status: "Off" }
  }

  Component {
    id: bannerActionComponent
    O.Banner { width: 420; text: "Update available"; actionText: "Update"; dismissible: true }
  }

  Component {
    id: bannerPlainComponent
    O.Banner { width: 420; text: "Backup finished"; dismissible: true }
  }

  Component {
    id: numberFieldComponent
    O.NumberField { label: "Gaps"; value: 10 }
  }

  Component {
    id: dropdownComponent
    O.Dropdown { label: "Theme"; value: "a"; options: ["a", "b"] }
  }

  Component {
    id: calendarWideComponent
    O.Calendar { width: 400; year: 2026; month: 11 }
  }

  function test_button_with_icon_matches_text_only_height() {
    var plain = createTemporaryObject(textButtonComponent, testCase)
    var withIcon = createTemporaryObject(iconButtonLabelComponent, testCase)
    verify(plain && withIcon)
    compare(withIcon.implicitHeight, plain.implicitHeight,
            "an icon + label button must match text-only button height")
  }

  function test_icon_button_caps_radius_and_centers_glyph_optically() {
    var btn = createTemporaryObject(actionButtonComponent, testCase)
    verify(btn)
    compare(btn.radius, Math.min(4, Theme.Style.cornerRadius))
    var glyph = null
    for (var i = 0; i < btn.children.length; i++) {
      if (btn.children[i].text === "") { glyph = btn.children[i]; break }
    }
    verify(glyph, "icon glyph text exists")
    verify(glyph._hasInk, "icon glyph has measurable ink bounds")
    var inkCenterX = glyph.x + glyph._ink.x + glyph._ink.width / 2
    verify(Math.abs(inkCenterX - btn.width / 2) <= 1.0,
           "glyph ink X center (" + inkCenterX + ") should sit near button center (" + (btn.width / 2) + ")")
    compare(glyph.y, Math.round((btn.height - glyph.height) / 2),
            "glyph Y should center on line box without Qt tightBoundingRect baseline bias")
  }

  function test_stepped_slider_draws_interior_notches_only() {
    var slider = createTemporaryObject(sliderComponent, testCase)
    verify(slider)
    var repeater = null
    for (var i = 0; i < slider.data.length; i++) {
      if (slider.data[i].count !== undefined) { repeater = slider.data[i]; break }
    }
    verify(repeater)
    compare(repeater.count, 3, "5 stops have 3 interior notches so track end caps stay intact")
  }

  function test_tile_grid_vertically_centers_single_line_tiles() {
    var grid = createTemporaryObject(tileGridComponent, testCase)
    verify(grid)
    wait(20)
    var tile = null
    for (var i = 0; i < grid.children.length; i++) {
      if (grid.children[i].hasCursor !== undefined) { tile = grid.children[i]; break }
    }
    verify(tile, "first tile exists")
    var col = null
    for (var j = 0; j < tile.children.length; j++) {
      if (tile.children[j].spacing !== undefined && tile.children[j].height > 0) { col = tile.children[j]; break }
    }
    verify(col, "tile content column exists")
    var topGap = col.y
    var bottomGap = tile.height - (col.y + col.height)
    verify(Math.abs(topGap - bottomGap) <= 1.0,
           "1-line tile content should be vertically centered (top=" + topGap + ", bottom=" + bottomGap + ")")
  }

  function test_stacked_heroes_align_titles_horizontally() {
    var wifi = createTemporaryObject(heroWifiComponent, testCase)
    var bt = createTemporaryObject(heroBtComponent, testCase)
    verify(wifi && bt)
    compare(wifi.glyphSlotWidth, bt.glyphSlotWidth, "heroes reserve a uniform glyph slot width")
  }

  function test_banners_match_height_with_and_without_action() {
    var withAction = createTemporaryObject(bannerActionComponent, testCase)
    var plain = createTemporaryObject(bannerPlainComponent, testCase)
    verify(withAction && plain)
    compare(plain.implicitHeight, withAction.implicitHeight,
            "dismiss-only banner height must match action banner height")
  }

  function test_number_field_and_dropdown_match_labeled_height() {
    var nf = createTemporaryObject(numberFieldComponent, testCase)
    var dd = createTemporaryObject(dropdownComponent, testCase)
    verify(nf && dd)
    compare(nf.implicitHeight, dd.implicitHeight,
            "labeled NumberField and Dropdown must share total height")
  }

  function test_calendar_next_button_stays_over_sunday_column() {
    var cal = createTemporaryObject(calendarWideComponent, testCase)
    verify(cal)
    var col = cal.children[0]
    var header = col.children[0]
    var nextBtn = header.children[2]
    verify(nextBtn.x + nextBtn.width <= cal.gridWidth,
           "next-month button must stay aligned within gridWidth even when Calendar is wider")
  }
}
