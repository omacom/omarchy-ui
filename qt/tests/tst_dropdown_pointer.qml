import QtQuick
import QtQuick.Controls as QQC
import QtTest
import Omarchy.Ui as O

TestCase {
  id: testCase
  name: "DropdownPointer"
  width: 600
  height: 500
  visible: true
  when: windowShown

  Component {
    id: dropdownComponent
    O.Dropdown {}
  }

  Component {
    id: searchableComponent
    O.SearchableDropdown {}
  }

  SignalSpy { id: changed; signalName: "changed" }

  function init_data() {
    return [{ tag: "dropdown", searchable: false }, { tag: "searchable", searchable: true }]
  }

  function open(searchable) {
    var picker = createTemporaryObject(searchable ? searchableComponent : dropdownComponent,
                                       testCase, { value: "alpha", options: [
                                         { value: "alpha", label: "Alpha" },
                                         { value: "beta", label: "Beta" }
                                       ] })
    verify(picker)
    changed.target = picker
    changed.clear()
    picker.open()
    tryCompare(picker, "popupOpen", true)
    if (searchable) {
      // The search field takes focus after the popup opens.
      tryVerify(function() {
        var item = testCase.Window.window.activeFocusItem
        return item && item.hasOwnProperty("placeholderText")
      })
      keyClick(Qt.Key_Down)
    }
    return picker
  }

  function test_click_without_motion_uses_the_clicked_row(data) {
    mouseMove(testCase, testCase.width - 1, testCase.height - 1)
    var picker = open(data.searchable)
    var beta = null
    tryVerify(function() {
      beta = findText(QQC.Overlay.overlay, "Beta")
      return !!beta
    })
    for (var item = beta.parent; item; item = item.parent) {
      if (isPolishScheduled(item)) verify(waitForPolish(item))
    }

    // Alpha is the current row. Press and release Beta without a move over it.
    mousePress(beta)
    mouseRelease(beta)
    compare(changed.count, 1)
    compare(changed.signalArguments[0][0], "beta")
    compare(picker.value, "beta")
  }

  function test_enter_uses_the_keyboard_cursor(data) {
    var picker = open(data.searchable)
    keyClick(Qt.Key_Down)
    keyClick(Qt.Key_Return)
    compare(changed.count, 1)
    compare(changed.signalArguments[0][0], "beta")
    compare(picker.value, "beta")
  }

  function findText(item, text) {
    if (!item || !item.visible) return null
    if (item.text === text) return item
    for (var child of item.children) {
      var hit = findText(child, text)
      if (hit) return hit
    }
    return null
  }
}
