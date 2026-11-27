#include "Omarchy/Slider.h"

#include <QKeyEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

#include "Omarchy/Paint.h"

namespace Omarchy {

Slider::Slider(QWidget *parent) : Control(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_anim = new QVariantAnimation(this);
  m_anim->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    m_shown = v.toReal();
    update();
  });
}

qreal Slider::progress() const {
  const qreal range = qMax(0.0001, m_max - m_min);
  return qBound(0.0, (m_live - m_min) / range, 1.0);
}

void Slider::setValue(qreal v) {
  m_value = v;
  if (m_dragging) return;
  m_live = v;
  const qreal target = progress();
  const int ms = shown().duration(140);
  m_anim->stop();
  if (ms <= 0 || !isVisible()) {
    m_shown = target;
  } else {
    m_anim->setDuration(ms);
    m_anim->setStartValue(m_shown);
    m_anim->setEndValue(target);
    m_anim->start();
  }
  update();
}

void Slider::setRange(qreal minimum, qreal maximum) {
  m_min = minimum;
  m_max = qMax(minimum, maximum);
  setValue(m_value);
}

void Slider::setTickCount(int n) {
  m_ticks = n;
  update();
}

int Slider::knobSize() const { return qMax(14, int(std::lround(t().spacing("controlHeight") * 0.38))); }

QSize Slider::sizeHint() const {
  const Tokens &tk = t();
  return QSize(tk.space(200), qMax(tk.space(22), knobSize() + tk.spacing("md")));
}

QRectF Slider::trackRect() const {
  const int th = qMax(4, int(std::lround(t().spacing("controlHeight") * 0.11)));
  const qreal k = knobSize() / 2.0;
  return QRectF(k, Paint::centered(height(), th), width() - 2 * k, th);
}

QRectF Slider::knobRect() const {
  const QRectF track = trackRect();
  const int k = knobSize();
  const bool mirrored = layoutDirection() == Qt::RightToLeft;
  const qreal frac = m_dragging ? progress() : m_shown;
  const qreal x = track.x() + track.width() * (mirrored ? 1 - frac : frac);
  return QRectF(x - k / 2.0, track.y() + Paint::centered(track.height(), k), k, k);
}

void Slider::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QColor fg = tk.foreground(), surface = tk.color(QStringLiteral("surfaces.popups.background"));
  const QRectF track = trackRect();
  const qreal radius = track.height() / 2;
  Paint::surface(p, track, radius, tk.fill(State::Selected, fg));
  const qreal frac = m_dragging ? progress() : m_shown;
  const bool mirrored = layoutDirection() == Qt::RightToLeft;
  const qreal fw = track.width() * frac;
  Paint::surface(p, QRectF(mirrored ? track.right() - fw : track.x(), track.y(), fw, track.height()), radius, fg);
  if (m_ticks > 2) {
    const qreal nw = qMax(1, tk.space(2)), nh = track.height() + tk.space(4);
    for (int i = 1; i < m_ticks - 1; ++i) {
      const qreal f = qreal(i) / (m_ticks - 1);
      const qreal x = track.x() + track.width() * (mirrored ? 1 - f : f);
      Paint::surface(p, QRectF(x - nw / 2, track.y() + Paint::centered(track.height(), nh), nw, nh), 1, surface);
    }
  }
  QRectF k = knobRect();
  // Under the pointer, while dragging or focused, the knob grows by 15%.
  if (isHot() || m_dragging || hasFocus()) {
    const QPointF c = k.center();
    p.translate(c);
    p.scale(1.15, 1.15);
    p.translate(-c);
  }
  Paint::surface(p, k, k.width() / 2, fg, Border::flat(surface, qMax(1, tk.space(2))));
}

qreal Slider::valueAt(qreal x) const {
  const QRectF track = trackRect();
  if (track.width() <= 0) return m_min;
  qreal f = qBound(0.0, (x - track.x()) / track.width(), 1.0);
  if (layoutDirection() == Qt::RightToLeft) f = 1 - f;
  qreal raw = m_min + f * (m_max - m_min);
  if (m_integer) raw = std::round(raw);
  return qBound(m_min, raw, m_max);
}

void Slider::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::RightButton) {
    emit rightClicked();
    return;
  }
  if (e->button() != Qt::LeftButton) return;
  setFocus(Qt::MouseFocusReason);
  m_dragging = true;
  m_live = valueAt(e->position().x());
  update();
  emit moved(m_live);
}

void Slider::mouseMoveEvent(QMouseEvent *e) {
  if (!m_dragging) return;
  m_live = valueAt(e->position().x());
  update();
  emit moved(m_live);
}

void Slider::mouseReleaseEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton || !m_dragging) return;
  m_dragging = false;
  const qreal v = m_live;
  m_shown = progress();
  emit released(v);
  if (m_autoSet) setValue(v);
  else setValue(m_value);
}

void Slider::nudge(qreal delta) {
  qreal next = qBound(m_min, m_live + delta, m_max);
  if (m_integer) next = std::round(next);
  emit moved(next);
  emit released(next);
  if (m_autoSet) setValue(next);
}

void Slider::wheelEvent(QWheelEvent *e) {
  const int dy = e->angleDelta().y();
  if (dy != 0) nudge(dy > 0 ? m_step : -m_step);
  e->accept();
}

void Slider::keyPressEvent(QKeyEvent *e) {
  const bool rtl = layoutDirection() == Qt::RightToLeft;
  int key = e->key();
  if (e->text() == QLatin1String("h")) key = Qt::Key_Left;
  else if (e->text() == QLatin1String("l")) key = Qt::Key_Right;
  switch (key) {
  case Qt::Key_Left: nudge(rtl ? m_step : -m_step); break;
  case Qt::Key_Right: nudge(rtl ? -m_step : m_step); break;
  case Qt::Key_Down: nudge(-m_step); break;
  case Qt::Key_Up: nudge(m_step); break;
  case Qt::Key_PageDown: nudge(-(m_max - m_min) / 10); break;
  case Qt::Key_PageUp: nudge((m_max - m_min) / 10); break;
  case Qt::Key_Home: nudge(m_min - m_live); break;
  case Qt::Key_End: nudge(m_max - m_live); break;
  default: Control::keyPressEvent(e); return;
  }
  e->accept();
}

}  // namespace Omarchy
