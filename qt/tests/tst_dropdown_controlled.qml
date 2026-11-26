import QtQuick
import QtTest
import Omarchy.Ui as O

TestCase {
  id: testCase
  name: "DropdownControlledValue"
  width: 600
  height: 500
  visible: true
  when: windowShown

  property string confirmedValue: "alpha"

  Component {
    id: dropdownComponent
    O.Dropdown { value: testCase.confirmedValue }
  }

  Component {
    id: searchableComponent
    O.SearchableDropdown { value: testCase.confirmedValue }
  }

  SignalSpy { id: changed; signalName: "changed" }

  function init_data() {
    return [{ tag: "dropdown", searchable: false }, { tag: "searchable", searchable: true }]
  }

  function make(searchable, controlled) {
    confirmedValue = "alpha"
    var properties = { options: [
      { value: "alpha", label: "Alpha" },
      { value: "beta", label: "Beta" }
    ] }
    if (controlled) properties.updateValueOnSelection = false
    var picker = createTemporaryObject(searchable ? searchableComponent : dropdownComponent,
                                       testCase, properties)
    verify(picker)
    if (!controlled) picker.value = "alpha"
    changed.target = picker
    changed.clear()
    return picker
  }

  function chooseSecond(picker, searchable) {
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
    keyClick(Qt.Key_Down)
    keyClick(Qt.Key_Return)
    compare(changed.count, 1)
    compare(changed.signalArguments[0][0], "beta")
    tryCompare(picker, "popupOpen", false)
  }

  function test_waits_for_confirmed_value(data) {
    var picker = make(data.searchable, true)
    chooseSecond(picker, data.searchable)
    compare(picker.value, "alpha", "the selection signal leaves the confirmed value alone")
    confirmedValue = "beta"
    compare(picker.value, "beta", "the app can confirm the selection later")
    confirmedValue = "alpha"
    compare(picker.value, "alpha", "later model updates still reach the picker")
  }

  function test_default_updates_immediately(data) {
    var picker = make(data.searchable, false)
    chooseSecond(picker, data.searchable)
    compare(picker.value, "beta")
  }
}
