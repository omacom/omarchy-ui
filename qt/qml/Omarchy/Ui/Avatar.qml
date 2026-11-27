import QtQuick
import Omarchy.Theme
import Omarchy.Theme as Commons
import Omarchy.Theme.Native

// A person's or an app's picture with fallbacks, tried in order: `source` (an image URL or
// path), then `iconName` (a freedesktop icon name, looked up in the Omarchy theme's icon theme
// by IconLookup), then `glyph` (a Nerd Font icon), then the first letter of `name`, the last two
// on a foreground-glass tile. A source that fails to load gives way to the next.
//
//   Avatar { name: "Ryan"; source: "file:///…/ryan.png"; round: true; status: "success" }
//   Avatar { name: "Files"; iconName: "org.gnome.Nautilus"; glyph: "" }
//   Avatar { name: "Terminal"; glyph: ""; size: "small" }
//
// An icon is drawn whole, fitted to the square, with no tile and no clipping (its artwork has
// its own shape), rasterized at the device size so SVGs stay crisp. It re-resolves when the
// theme switches to another icon theme.
//
// Corners follow Hyprland's rounding (square at Omarchy's default 0), as Toast's thumbnails do;
// `round` makes a disc, for people, as elsewhen's avatars are. `size` is "small" (iconLarge,
// fills a ListRow's leading slot), "medium" (displayLarge) or "large" (display × 2), or set
// `pixelSize`. `status` docks a StatusDot on the bottom-right corner, ringed in `ground`.
Item {
  id: root

  property string name: ""
  property url source: ""
  property string iconName: ""
  property string glyph: ""
  property string size: "medium"
  property real pixelSize: size === "small" ? Style.font.iconLarge
    : size === "large" ? Style.font.display * 2
    : Style.font.displayLarge
  property bool round: false
  property string status: ""
  property color ground: Commons.Color.popups.background

  readonly property real cornerRadius: round ? pixelSize / 2 : Math.min(Style.cornerRadius, pixelSize / 2)
  readonly property bool pictured: String(source) !== "" && probe.status === Image.Ready
  readonly property int iconPixels: Math.ceil(pixelSize * Screen.devicePixelRatio)
  readonly property url iconSource: {
    void IconLookup.theme  // re-resolve on an icon theme switch
    return iconName === "" ? "" : IconLookup.url(iconName, iconPixels)
  }
  readonly property bool iconed: !pictured && String(iconSource) !== "" && iconImage.status === Image.Ready
  readonly property string initial: {
    var text = String(name || "").trim()
    if (text === "") return ""
    var code = text.charCodeAt(0)
    return (code >= 0xd800 && code < 0xdc00 ? text.slice(0, 2) : text.charAt(0)).toUpperCase()
  }

  implicitWidth: pixelSize
  implicitHeight: pixelSize

  // Loads the picture and says whether it did; draws it when no rounding is needed.
  Image {
    id: probe
    anchors.fill: parent
    visible: root.pictured && root.cornerRadius <= 0
    source: root.source
    sourceSize.width: Math.ceil(root.pixelSize * Screen.devicePixelRatio)
    sourceSize.height: Math.ceil(root.pixelSize * Screen.devicePixelRatio)
    fillMode: Image.PreserveAspectCrop
    smooth: true
  }

  // A rounded picture, clipped on a canvas at device resolution (no shader effects, so it
  // renders on the software and offscreen backends too).
  Item {
    anchors.fill: parent
    visible: root.pictured && root.cornerRadius > 0

    Canvas {
      id: canvas
      readonly property real dpr: Screen.devicePixelRatio
      width: parent.width * dpr
      height: parent.height * dpr
      scale: 1 / dpr
      transformOrigin: Item.TopLeft
      readonly property string url: String(root.source)
      readonly property real r: root.cornerRadius * dpr
      onUrlChanged: { if (url !== "") loadImage(url); requestPaint() }
      onRChanged: requestPaint()
      onWidthChanged: requestPaint()
      Component.onCompleted: if (url !== "") loadImage(url)
      onImageLoaded: requestPaint()
      onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        if (url === "" || !isImageLoaded(url)) return
        ctx.beginPath()
        ctx.roundedRect(0, 0, width, height, r, r)
        ctx.clip()
        // Cover the square, cropping the long side, as PreserveAspectCrop does.
        var iw = probe.implicitWidth, ih = probe.implicitHeight
        var k = iw > 0 && ih > 0 ? Math.max(width / iw, height / ih) : 1
        var w = iw > 0 ? iw * k : width, h = ih > 0 ? ih * k : height
        ctx.drawImage(url, (width - w) / 2, (height - h) / 2, w, h)
      }
    }
  }

  Image {
    id: iconImage
    anchors.fill: parent
    visible: root.iconed
    source: root.iconSource
    sourceSize.width: root.iconPixels
    sourceSize.height: root.iconPixels
    fillMode: Image.PreserveAspectFit
    smooth: true
  }

  Rectangle {
    anchors.fill: parent
    visible: !root.pictured && !root.iconed
    radius: root.cornerRadius
    color: Style.selectedFill

    OpticalGlyph {
      visible: root.glyph !== ""
      anchors.fill: parent
      text: root.glyph
      color: Commons.Color.foreground
      fontSize: Math.round(root.pixelSize * 0.5)
    }

    Text {
      visible: root.glyph === ""
      anchors.centerIn: parent
      textFormat: Text.PlainText
      text: root.initial
      color: Commons.Color.foreground
      font.family: Style.font.family
      font.pixelSize: Math.round(root.pixelSize * 0.5)
      font.bold: true
    }
  }

  StatusDot {
    visible: root.status !== ""
    tone: root.status
    ground: root.ground
    size: Math.max(Style.space(6), Math.round(root.pixelSize * 0.28))
    x: root.pixelSize - width + ring
    y: root.pixelSize - height + ring
  }
}
