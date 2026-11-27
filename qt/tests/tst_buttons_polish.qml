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
    var inkCenterY = glyph.y + glyph.baselineOffset + glyph._ink.y + glyph._ink.height / 2
    verify(Math.abs(inkCenterX - btn.width / 2) <= 1.0,
           "glyph ink X center (" + inkCenterX + ") should sit near button center (" + (btn.width / 2) + ")")
    verify(Math.abs(inkCenterY - btn.height / 2) <= 1.0,
           "glyph ink Y center (" + inkCenterY + ") should sit near button center (" + (btn.height / 2) + ")")
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
}
