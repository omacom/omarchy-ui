import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons

// Small (22×22 by default) icon button used at the right edge of panel rows
// for inline actions — forget network, confirm passphrase, unpair device,
// etc. Two visual modes are supported via `hoverColor`:
//   - default: hoverColor === foreground → subtle foreground-tint hover
//   - urgent:  hoverColor === bar.urgent → red-tint hover for destructive
//              actions like forget/unpair
//
// `enabled` gates clicks and dims the icon. The component owns its own
// hover state visuals; mouse hover does NOT update any panel cursor state
// here because action buttons are not cursor targets — the row they live
// in is.
//
// Set `focusable: true` to make the button keyboard-tabbable with the
// shared hover-cursor/focus tokens. Use this
// in form contexts where Tab walks a list
// of controls; leave it false for the right-edge actions on panel rows
// where the row's CursorSurface owns the keyboard cursor.
//
// Set `hasCursor: true` to have the button render the same hover state as
// mouse hover — so a panel's keyboard cursor lands on it identically.
// Use this when a PanelActionButton is itself the cursor target (rather
// than living inside a CursorSurface row). Emits `hovered(bool)` on
// pointer enter/leave so the panel can update its cursor state to match.
BorderSurface {
  id: root

  property string iconText: ""
  property string tooltipText: ""
  property color foreground: Commons.Color.foreground
  property color hoverColor: foreground
  property string fontFamily: Style.font.family
  property real fontSize: Style.font.icon
  // Whole and odd, so an icon glyph (usually an odd number of pixels wide)
  // has the same space on both sides instead of one pixel more on one side.
  property real size: Math.round(Math.max(Style.space(22), fontSize + Style.spacing.sm * 2)) | 1

  property bool focusable: false
  property bool hasCursor: false
  property bool bordered: false

  signal clicked()
  signal hovered(bool isHovered)

  activeFocusOnTab: focusable
  Keys.onReturnPressed: if (focusable) root.clicked()
  Keys.onEnterPressed: if (focusable) root.clicked()
  Keys.onSpacePressed: if (focusable) root.clicked()

  implicitWidth: size
  implicitHeight: size
  radius: Math.min(4, Style.cornerRadius)

  readonly property bool _showFocusRing: focusable && activeFocus
  readonly property bool _hot: (mouse.containsMouse || root.hasCursor) && root.enabled
  readonly property var _borderSpec: _showFocusRing
    ? Border.controlSpec("focus", hoverColor, hoverColor)
    : (_hot && bordered
      ? Border.controlSpec("hover-cursor", hoverColor, hoverColor)
      : (bordered ? Border.controlSpec("normal", foreground, Commons.Color.accent) : Border.none()))

  color: _showFocusRing
    ? Style.focusFillFor(hoverColor, hoverColor)
    : (_hot
      ? Style.hoverFillFor(hoverColor, hoverColor)
      : "transparent")
  borderSpec: _borderSpec

  Behavior on color { ColorAnimation { duration: Style.duration(60) } }

  // Measure tight ink bounds at 100x so Nerd Font glyphs with asymmetric side
  // bearings or vertical offsets center optically inside the square button.
  readonly property int _metricsScale: 100
  TextMetrics {
    id: iconMetrics
    font.family: root.fontFamily
    font.pixelSize: Math.max(1, Math.round(root.fontSize)) * root._metricsScale
    text: root.iconText
  }

  Text {
    id: iconGlyph
    textFormat: Text.PlainText
    readonly property rect _ink: Qt.rect(
      iconMetrics.tightBoundingRect.x / root._metricsScale,
      iconMetrics.tightBoundingRect.y / root._metricsScale,
      iconMetrics.tightBoundingRect.width / root._metricsScale,
      iconMetrics.tightBoundingRect.height / root._metricsScale)
    readonly property bool _hasInk: _ink.width > 0 && _ink.height > 0
    // Center horizontally by optical ink bounds and vertically on the line box
    // (Qt's tightBoundingRect unions with y=0, which shifts glyphs above the
    // baseline like U+F141 upward by 2pt).
    x: _hasInk ? Math.round(root.width / 2 - (_ink.x + _ink.width / 2)) : Math.round((root.width - width) / 2)
    y: Math.round((root.height - height) / 2)
    text: root.iconText
    color: root.enabled
      ? (root._hot ? root.hoverColor : root.foreground)
      : Commons.Color.textFaint
    font.family: root.fontFamily
    font.pixelSize: root.fontSize
  }

  MouseArea {
    id: mouse
    anchors.fill: parent
    hoverEnabled: true
    cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    enabled: root.enabled
    onContainsMouseChanged: root.hovered(containsMouse)
    onClicked: {
      if (root.focusable) root.forceActiveFocus()
      root.clicked()
    }
  }

  PanelToolTip {
    visible: root.tooltipText !== "" && mouse.containsMouse
    text: root.tooltipText
    fontFamily: root.fontFamily
  }
}
