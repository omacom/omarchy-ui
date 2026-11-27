#include "Omarchy/Paint.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QTextLayout>
#include <cmath>

namespace Omarchy::Paint {

QPainterPath roundedRect(const QRectF &r, qreal radius) {
  QPainterPath path;
  radius = qMin(radius, qMin(r.width(), r.height()) / 2);
  if (radius <= 0) path.addRect(r);
  else path.addRoundedRect(r, radius, radius);
  return path;
}

static qreal half(qreal w) {
  const int iw = int(w);
  return (iw % 2) ? (w + 1) / 2 : w / 2;
}

qreal centered(qreal outer, qreal inner) { return qRound(half(outer) - half(inner)); }

QColor mix(const QColor &a, const QColor &b, qreal t) {
  auto ch = [t](qreal x, qreal y) { return x + (y - x) * t; };
  return QColor::fromRgbF(ch(a.redF(), b.redF()), ch(a.greenF(), b.greenF()), ch(a.blueF(), b.blueF()), ch(a.alphaF(), b.alphaF()));
}

// Qt Quick's Rectangle: the fill covers what's inside the border, the border is a ring, and
// only a rounded rectangle is antialiased. A gradient or per-side border (BorderOverlay) is
// drawn over a full fill instead, as the kit's BorderSurface does.
void surface(QPainter &p, const QRectF &r, qreal radius, const QColor &fill, const Border &b) {
  if (r.width() <= 0 || r.height() <= 0) return;
  radius = qMax<qreal>(0, qMin(radius, qMin(r.width(), r.height()) / 2));
  p.save();
  p.setPen(Qt::NoPen);
  p.setRenderHint(QPainter::Antialiasing, radius > 0);
  const bool hasBorder = !b.isNone() && (b.color.alpha() > 0 || b.hasGradient());
  const bool native = hasBorder && b.isUniform() && !b.hasGradient();
  if (native) {
    const qreal w = b.top;
    const QRectF inner = r.adjusted(w, w, -w, -w);
    const qreal ir = qMax<qreal>(0, radius - w);
    if (fill.alpha() > 0 && inner.width() > 0 && inner.height() > 0) p.fillPath(roundedRect(inner, ir), fill);
    QPainterPath ring = roundedRect(r, radius);
    if (inner.width() > 0 && inner.height() > 0) ring.addPath(roundedRect(inner, ir));
    ring.setFillRule(Qt::OddEvenFill);
    p.fillPath(ring, b.color);
  } else {
    if (fill.alpha() > 0) p.fillPath(roundedRect(r, radius), fill);
    if (hasBorder) {
      p.setRenderHint(QPainter::Antialiasing, true);
      const QRectF inner = r.adjusted(b.left, b.top, -b.right, -b.bottom);
      QPainterPath ring = roundedRect(r, radius);
      if (inner.width() > 0 && inner.height() > 0) {
        const qreal ir = qMax<qreal>(0, radius - qMax(qMax(b.left, b.right), qMax(b.top, b.bottom)));
        ring.addPath(roundedRect(inner, ir));
      }
      ring.setFillRule(Qt::OddEvenFill);
      if (b.hasGradient()) {
        // BorderGeometry.gradientEndpoints: through the center at `angle`, spanning the box.
        const qreal rad = b.angle * M_PI / 180.0, dx = std::cos(rad), dy = std::sin(rad);
        const qreal len = (std::abs(r.width() * dx) + std::abs(r.height() * dy)) / 2;
        const QPointF c = r.center();
        QLinearGradient g(c - QPointF(dx * len, dy * len), c + QPointF(dx * len, dy * len));
        for (int i = 0; i < b.gradient.size(); ++i) g.setColorAt(qreal(i) / (b.gradient.size() - 1), b.gradient.at(i));
        p.fillPath(ring, g);
      } else {
        p.fillPath(ring, b.color);
      }
    }
  }
  p.restore();
}

// ------------------------------------------------------------------------------------------ text

static void layoutLine(QTextLayout &layout, const QFont &font, const QString &text, qreal width = -1, bool wrap = false) {
  layout.setText(text);
  layout.setFont(font);
  QTextOption opt;
  // Qt Quick's Text (QtRendering) lays out with design metrics: fractional advances.
  opt.setUseDesignMetrics(true);
  opt.setWrapMode(wrap ? QTextOption::WordWrap : QTextOption::NoWrap);
  layout.setTextOption(opt);
  layout.setCacheEnabled(true);
  layout.beginLayout();
  qreal y = 0;
  for (;;) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) break;
    if (width >= 0 && wrap) line.setLineWidth(width);
    line.setPosition(QPointF(0, y));
    y += line.height();
  }
  layout.endLayout();
}

QSizeF textSize(const QFont &font, const QString &text) {
  QTextLayout layout;
  layoutLine(layout, font, text);
  if (layout.lineCount() == 0) return QSizeF(0, lineHeight(font));
  const QTextLine line = layout.lineAt(0);
  return QSizeF(line.naturalTextWidth(), line.height());
}

qreal textWidth(const QFont &font, const QString &text) { return text.isEmpty() ? 0 : textSize(font, text).width(); }

qreal lineHeight(const QFont &font) {
  QTextLayout layout;
  layoutLine(layout, font, QStringLiteral(" "));
  return layout.lineCount() ? layout.lineAt(0).height() : QFontMetricsF(font).height();
}

qreal ascent(const QFont &font) {
  QTextLayout layout;
  layoutLine(layout, font, QStringLiteral(" "));
  return layout.lineCount() ? layout.lineAt(0).ascent() : QFontMetricsF(font).ascent();
}

void text(QPainter &p, const QPointF &topLeft, const QFont &font, const QColor &color, const QString &s) {
  if (s.isEmpty()) return;
  QTextLayout layout;
  layoutLine(layout, font, s);
  p.save();
  p.setPen(color);
  layout.draw(&p, topLeft);
  p.restore();
}

QString elided(const QFont &font, const QString &s, qreal width, Qt::TextElideMode mode) {
  if (mode == Qt::ElideNone || textWidth(font, s) <= width + 0.01) return s;
  return QFontMetricsF(font).elidedText(s, mode, width);
}

void textIn(QPainter &p, const QRectF &r, int align, const QFont &font, const QColor &color, const QString &s, Qt::TextElideMode mode) {
  const QString shown = elided(font, s, r.width(), mode);
  const QSizeF size = textSize(font, shown);
  qreal x = r.left(), y = r.top();
  if (align & Qt::AlignHCenter) x = r.left() + (r.width() - size.width()) / 2;
  else if (align & Qt::AlignRight) x = r.right() - size.width();
  if (align & Qt::AlignVCenter) y = r.top() + (r.height() - size.height()) / 2;
  else if (align & Qt::AlignBottom) y = r.bottom() - size.height();
  text(p, QPointF(x, y), font, color, shown);
}

void opticalGlyph(QPainter &p, const QRectF &box, const QFont &font, const QColor &color, const QString &s) {
  if (s.isEmpty()) return;
  constexpr int kScale = 100;
  QFont bigFont = font;
  bigFont.setPixelSize(qMax(1, font.pixelSize()) * kScale);
  const QRectF big = QFontMetricsF(bigFont).tightBoundingRect(s);
  const qreal inkX = big.x() / kScale;
  const qreal inkW = big.width() / kScale;
  const qreal x = inkW > 0 ? box.x() + box.width() / 2.0 - (inkX + qMax<qreal>(1.0, inkW) / 2.0)
                           : box.x() + (box.width() - textWidth(font, s)) / 2.0;
  const qreal y = box.y() + std::floor((box.height() - lineHeight(font)) / 2.0 + 0.5);
  text(p, QPointF(x, y), font, color, s);
}

qreal wrappedHeight(const QFont &font, const QString &s, qreal width) {
  QTextLayout layout;
  layoutLine(layout, font, s, width, true);
  qreal h = 0;
  for (int i = 0; i < layout.lineCount(); ++i) h += layout.lineAt(i).height();
  return layout.lineCount() ? h : lineHeight(font);
}

void wrapped(QPainter &p, const QRectF &r, const QFont &font, const QColor &color, const QString &s, Qt::Alignment align) {
  if (s.isEmpty()) return;
  QTextLayout layout;
  layoutLine(layout, font, s, r.width(), true);
  p.save();
  p.setPen(color);
  if (align & Qt::AlignHCenter) {
    for (int i = 0; i < layout.lineCount(); ++i) {
      QTextLine line = layout.lineAt(i);
      line.setPosition(QPointF((r.width() - line.naturalTextWidth()) / 2, line.position().y()));
    }
  }
  layout.draw(&p, r.topLeft());
  p.restore();
}

}  // namespace Omarchy::Paint
