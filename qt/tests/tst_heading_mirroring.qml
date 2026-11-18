import QtQuick
import QtTest
import Omarchy.Theme
import Omarchy.Ui as O

TestCase {
  id: testCase
  name: "HeadingMirroring"
  width: 320
  height: 240
  visible: true
  when: windowShown

  Component {
    id: hostComponent
    Item {
      property bool rightToLeft: false
      readonly property Item heading: headingItem
      readonly property Item action: actionItem
      LayoutMirroring.enabled: rightToLeft
      LayoutMirroring.childrenInherit: true

      O.Heading {
        id: headingItem
        width: 230
        text: "A long heading with a trailing action"
        subtitle: "A long subtitle with room to elide"

        Item {
          id: actionItem
          implicitWidth: 48
          implicitHeight: 20
        }
      }
    }
  }

  function test_trailingAction_data() {
    return [
      { tag: "ltr", rightToLeft: false },
      { tag: "rtl", rightToLeft: true }
    ]
  }

  function test_trailingAction(data) {
    var host = createTemporaryObject(hostComponent, testCase, { rightToLeft: data.rightToLeft })
    verify(host)
    var heading = host.heading
    var action = host.action
    tryCompare(action, "width", 48)
    compare(action.mapToItem(heading, 0, 0).x, data.rightToLeft ? 0 : heading.width - action.width)

    var labels = heading.children.find(function (child) {
      return child.children.some(function (label) { return label.text === heading.text })
    })
    verify(labels)
    var reserved = action.width + Style.spacing.controlGap
    compare(labels.width, heading.width - reserved)
    compare(labels.x, data.rightToLeft ? reserved : 0)
  }
}
