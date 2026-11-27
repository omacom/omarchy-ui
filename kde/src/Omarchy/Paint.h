#pragma once

// Drawing the kit's way, for kit widgets and for app code with its own painting: surfaces with
// Omarchy border specs (Qt Quick Rectangle semantics: the fill inside the border, rounded
// corners antialiased, square ones on the pixel grid), and text laid out as Qt Quick's Text lays
// it out (design metrics), so a widget measures and draws like its Qt Quick twin.

#include <QColor>
#include <QFont>
#include <QPainterPath>
#include <QRectF>
#include <QString>

#include "Omarchy/Export.h"
#include "Omarchy/Theme.h"

class QPainter;

namespace Omarchy::Paint {

// A rounded rectangle, radius clamped to half the shorter side.
OMARCHY_EXPORT QPainterPath roundedRect(const QRectF &r, qreal radius);

// A surface: `fill` inside the border, the border as a ring (its gradient when it has one).
OMARCHY_EXPORT void surface(QPainter &p, const QRectF &r, qreal radius, const QColor &fill, const Border &border = Border());

// Text, one line. size() is Qt Quick Text's implicit size (advance width × line height).
OMARCHY_EXPORT QSizeF textSize(const QFont &font, const QString &text);
OMARCHY_EXPORT qreal textWidth(const QFont &font, const QString &text);
OMARCHY_EXPORT qreal lineHeight(const QFont &font);
OMARCHY_EXPORT qreal ascent(const QFont &font);
// Draws `text` with its line box's top-left at `topLeft`.
OMARCHY_EXPORT void text(QPainter &p, const QPointF &topLeft, const QFont &font, const QColor &color, const QString &text);
// Elided to `width` (Text.ElideRight) when it doesn't fit.
OMARCHY_EXPORT QString elided(const QFont &font, const QString &text, qreal width, Qt::TextElideMode mode = Qt::ElideRight);
// Aligned in `r` (Qt::Align*), elided to its width.
OMARCHY_EXPORT void textIn(QPainter &p, const QRectF &r, int align, const QFont &font, const QColor &color, const QString &text,
                           Qt::TextElideMode elide = Qt::ElideRight);
// OpticalGlyph (paintedSize 0): centers `text` horizontally on its outline ink (measured 100×
// larger so whole-pixel bounds don't shift it) and vertically on its line box inside `box`.
OMARCHY_EXPORT void opticalGlyph(QPainter &p, const QRectF &box, const QFont &font, const QColor &color, const QString &text);

// Word-wrapped text (Text.WordWrap) at `width`: its height, and drawing it.
OMARCHY_EXPORT qreal wrappedHeight(const QFont &font, const QString &text, qreal width);
OMARCHY_EXPORT void wrapped(QPainter &p, const QRectF &r, const QFont &font, const QColor &color, const QString &text,
                            Qt::Alignment align = Qt::AlignLeft);

// Where Qt Quick's centering anchors (anchors.centerIn / horizontalCenter / verticalCenter, with
// alignWhenCentered) put an item of size `inner` in one of size `outer`: each half is taken as
// (n + 1) / 2 when its whole part is odd, and the offset is rounded to a whole pixel.
OMARCHY_EXPORT qreal centered(qreal outer, qreal inner);

// QColor helpers the kit's QML uses: Qt.darker, Qt.rgba(c, a), a mix.
inline QColor darker(const QColor &c, qreal factor) { return c.darker(int(factor * 100 + 0.5)); }
inline QColor alpha(const QColor &c, qreal a) {
  QColor o = c;
  o.setAlphaF(qBound(0.0, a, 1.0));
  return o;
}
OMARCHY_EXPORT QColor mix(const QColor &a, const QColor &b, qreal t);

}  // namespace Omarchy::Paint
