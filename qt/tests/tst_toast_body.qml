import QtQuick
import QtTest
import Omarchy.Ui as O

TestCase {
  id: testCase
  name: "ToastBody"
  width: 320
  height: 240
  visible: true
  when: windowShown

  Component {
    id: toastComponent
    O.Toast {
      width: 280
      readonly property string announcedName: Accessible.name
    }
  }

  function test_bodySuppliesAccessibleName() {
    var toast = createTemporaryObject(toastComponent, testCase, { text: "Export finished" })
    verify(toast)
    compare(toast.announcedName, "Export finished")

    toast.title = "Export"
    compare(toast.announcedName, "Export")
  }

  function test_bodyUsesOneTextRow() {
    var body = createTemporaryObject(toastComponent, testCase, { text: "Export finished" })
    var titled = createTemporaryObject(toastComponent, testCase, { title: "Export", text: "Export finished" })
    verify(body && titled)
    verify(body.implicitHeight < titled.implicitHeight, "a body-only toast should omit the empty title row")
  }
}
