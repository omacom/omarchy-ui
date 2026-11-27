#include "Omarchy/Editing.h"

#include <QKeyEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

#include "Omarchy/Paint.h"

namespace Omarchy {

// JavaScript's Math.round: halves toward +∞.
static qreal jsRound(qreal v) { return std::floor(v + 0.5); }

// "Hot accent": Qt.tint(color, alpha(foreground, 0.25)).
static QColor hotColor(const QColor &c, const Tokens &tk) { return Paint::mix(c, tk.foreground(), 0.25); }

static QPointF globalPos(const QMouseEvent *e) { return e->globalPosition(); }

// ------------------------------------------------------------------------------------ TrimHandle

TrimHandle::TrimHandle(const QString &edge, QWidget *parent) : Control(parent), m_edge(edge) {
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  updateCursor();
}

void TrimHandle::setEdge(const QString &e) {
  m_edge = e;
  updateCursor();
  updateGeometry();
  refresh();
}
void TrimHandle::setCrop(bool on) {
  m_crop = on;
  updateGeometry();
  refresh();
}
QColor TrimHandle::color() const { return m_color.isValid() ? m_color : shown().accent(); }
void TrimHandle::setColor(const QColor &c) {
  m_color = c;
  refresh();
}
QColor TrimHandle::gripColor() const { return m_grip.isValid() ? m_grip : shown().background(); }
void TrimHandle::setGripColor(const QColor &c) {
  m_grip = c;
  refresh();
}
void TrimHandle::setCapHeight(int px) {
  m_capHeight = px;
  updateGeometry();
}
void TrimHandle::setGrabMargin(int margin) {
  m_margin = margin;
  updateGeometry();
  refresh();
}
void TrimHandle::setDrawnByOwner(bool on) {
  m_drawnByOwner = on;
  refresh();
}

bool TrimHandle::isHandleHot() const { return isHovered() || hasCursor() || hasFocus() || m_dragging; }

QSize TrimHandle::handleSize() const {
  const Tokens &tk = shown();
  const int th = m_crop ? tk.space(4) : tk.space(8), len = tk.space(18);
  const bool corner = m_edge.indexOf(QLatin1Char('-')) > 0;
  const bool hbar = m_edge == QLatin1String("top") || m_edge == QLatin1String("bottom");
  if (!m_crop) return QSize(th, m_capHeight > 0 ? m_capHeight : tk.space(40));
  return QSize(corner || hbar ? len : th, corner || !hbar ? len : th);
}

QSize TrimHandle::sizeHint() const { return handleSize() + QSize(m_margin * 2, m_margin * 2); }

void TrimHandle::setHandleGeometry(const QRectF &visible) {
  setGeometryF(visible.adjusted(-m_margin, -m_margin, m_margin, m_margin));
}

QRectF TrimHandle::handleRect() const { return paintRect().adjusted(m_margin, m_margin, -m_margin, -m_margin); }

void TrimHandle::refresh() {
  update();
  if (m_drawnByOwner && parentWidget()) parentWidget()->update();
}

void TrimHandle::updateCursor() {
  if (m_edge == QLatin1String("left") || m_edge == QLatin1String("right")) setCursor(Qt::SizeHorCursor);
  else if (m_edge == QLatin1String("top") || m_edge == QLatin1String("bottom")) setCursor(Qt::SizeVerCursor);
  else if (m_edge == QLatin1String("top-left") || m_edge == QLatin1String("bottom-right")) setCursor(Qt::SizeFDiagCursor);
  else setCursor(Qt::SizeBDiagCursor);
}

static void fillRoundedCorners(QPainter &p, const QRectF &r, qreal tl, qreal tr, qreal bl, qreal br, const QColor &fill) {
  if (r.width() <= 0 || r.height() <= 0 || fill.alpha() <= 0) return;
  const qreal maxR = qMin(r.width(), r.height()) / 2.0;
  tl = qBound(0.0, tl, maxR);
  tr = qBound(0.0, tr, maxR);
  bl = qBound(0.0, bl, maxR);
  br = qBound(0.0, br, maxR);
  p.save();
  p.setPen(Qt::NoPen);
  const bool any = tl > 0 || tr > 0 || bl > 0 || br > 0;
  p.setRenderHint(QPainter::Antialiasing, any);
  if (!any) {
    p.fillRect(r, fill);
  } else {
    QPainterPath path;
    path.moveTo(r.left() + tl, r.top());
    path.lineTo(r.right() - tr, r.top());
    if (tr > 0) path.arcTo(QRectF(r.right() - 2 * tr, r.top(), 2 * tr, 2 * tr), 90, -90);
    path.lineTo(r.right(), r.bottom() - br);
    if (br > 0) path.arcTo(QRectF(r.right() - 2 * br, r.bottom() - 2 * br, 2 * br, 2 * br), 0, -90);
    path.lineTo(r.left() + bl, r.bottom());
    if (bl > 0) path.arcTo(QRectF(r.left(), r.bottom() - 2 * bl, 2 * bl, 2 * bl), 270, -90);
    path.lineTo(r.left(), r.top() + tl);
    if (tl > 0) path.arcTo(QRectF(r.left(), r.top(), 2 * tl, 2 * tl), 180, -90);
    path.closeSubpath();
    p.fillPath(path, fill);
  }
  p.restore();
}

void TrimHandle::paint(QPainter &p, const QRectF &r, const QString &edge, bool crop, bool hot, const QColor &color, const QColor &grip) {
  const Tokens &tk = shown();
  const QColor c = hot ? hotColor(color, tk) : color;
  const qreal small = tk.smallRadius();
  if (!crop) {
    Paint::surface(p, r, small, c);
    // Square off the inner side.
    const qreal half = r.width() / 2;
    Paint::surface(p, QRectF(edge == QLatin1String("right") ? r.x() : r.x() + half, r.y(), half, r.height()), 0, c);
    const qreal gw = qMax(1, tk.space(2)), gh = jsRound(r.height() * (hot ? 0.6 : 0.4));
    Paint::surface(p, QRectF(r.x() + jsRound((r.width() - gw) / 2), r.y() + jsRound((r.height() - gh) / 2), gw, gh), qMin(small, gw / 2), grip);
    return;
  }
  // Outer edges flush against the 1px crop border stay square at r8; only inner-facing / free corners round.
  const qreal th = tk.space(4), cr = qMin(small, th / 2);
  const bool corner = edge.indexOf(QLatin1Char('-')) > 0;
  const bool hbar = edge == QLatin1String("top") || edge == QLatin1String("bottom");
  const qreal y = corner && edge.startsWith(QLatin1String("bottom")) ? r.height() - th : 0;
  const qreal tl1 = (edge == QLatin1String("bottom") || edge == QLatin1String("right") || edge == QLatin1String("bottom-right")) ? cr : 0;
  const qreal tr1 = (edge == QLatin1String("bottom") || edge == QLatin1String("left") || edge == QLatin1String("bottom-left")) ? cr : 0;
  const qreal bl1 = (edge == QLatin1String("top") || edge == QLatin1String("right") || edge == QLatin1String("top-right")) ? cr : 0;
  const qreal br1 = (edge == QLatin1String("top") || edge == QLatin1String("left") || edge == QLatin1String("top-left")) ? cr : 0;
  fillRoundedCorners(p, QRectF(r.x(), r.y() + y, corner || hbar ? r.width() : th, corner || hbar ? th : r.height()), tl1, tr1, bl1, br1, c);
  if (corner) {
    const qreal x = edge.indexOf(QLatin1String("right")) > 0 ? r.width() - th : 0;
    const qreal tl2 = edge == QLatin1String("bottom-right") ? cr : 0;
    const qreal tr2 = edge == QLatin1String("bottom-left") ? cr : 0;
    const qreal bl2 = edge == QLatin1String("top-right") ? cr : 0;
    const qreal br2 = edge == QLatin1String("top-left") ? cr : 0;
    fillRoundedCorners(p, QRectF(r.x() + x, r.y(), th, r.height()), tl2, tr2, bl2, br2, c);
  }
}

void TrimHandle::paintEvent(QPaintEvent *) {
  if (m_drawnByOwner) return;
  QPainter p(this);
  paint(p, handleRect(), m_edge, m_crop, isHandleHot(), color(), gripColor());
}

void TrimHandle::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) return Control::mousePressEvent(e);
  setFocus(Qt::MouseFocusReason);
  m_origin = globalPos(e);
  m_dragging = true;
  emit draggingChanged(true);
  emit pressed();
  refresh();
  e->accept();
}

void TrimHandle::mouseMoveEvent(QMouseEvent *e) {
  if (!m_dragging) return;
  const QPointF d = globalPos(e) - m_origin;
  emit moved(d.x(), d.y());
}

void TrimHandle::mouseReleaseEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton || !m_dragging) return Control::mouseReleaseEvent(e);
  m_dragging = false;
  emit draggingChanged(false);
  emit released();
  refresh();
  e->accept();
}

void TrimHandle::keyPressEvent(QKeyEvent *e) {
  const qreal step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
  qreal dx = 0, dy = 0;
  switch (e->key()) {
  case Qt::Key_Left: dx = -step; break;
  case Qt::Key_Right: dx = step; break;
  case Qt::Key_Up: dy = -step; break;
  case Qt::Key_Down: dy = step; break;
  default: return Control::keyPressEvent(e);
  }
  if (!m_crop) dy = 0;
  if (m_crop && (m_edge == QLatin1String("top") || m_edge == QLatin1String("bottom"))) dx = 0;
  if (m_crop && (m_edge == QLatin1String("left") || m_edge == QLatin1String("right"))) dy = 0;
  if (dx == 0 && dy == 0) return Control::keyPressEvent(e);
  emit nudged(dx, dy);
  e->accept();
}

void TrimHandle::enterEvent(QEnterEvent *e) {
  Control::enterEvent(e);
  refresh();
}
void TrimHandle::leaveEvent(QEvent *e) {
  Control::leaveEvent(e);
  refresh();
}
void TrimHandle::focusInEvent(QFocusEvent *e) {
  Control::focusInEvent(e);
  refresh();
}
void TrimHandle::focusOutEvent(QFocusEvent *e) {
  Control::focusOutEvent(e);
  refresh();
}

// -------------------------------------------------------------------------------------- Timeline

Timeline::Timeline(QWidget *parent) : Control(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  for (TrimHandle **h : {&m_left, &m_right}) {
    const QString edge = h == &m_left ? QStringLiteral("left") : QStringLiteral("right");
    auto *handle = new TrimHandle(edge, this);
    handle->setDrawnByOwner(true);
    handle->hide();
    connect(handle, &TrimHandle::draggingChanged, this, [this, edge](bool on) {
      if (!on || m_selTrack < 0) return;
      const Clip c = m_tracks.value(m_selTrack).value(m_selClip);
      m_trimOrigin = edge == QLatin1String("left") ? c.start : c.end;
    });
    connect(handle, &TrimHandle::moved, this, [this, edge](qreal dx, qreal) { handleMoved(edge, dx); });
    connect(handle, &TrimHandle::nudged, this, [this, edge](qreal dx, qreal) { handleNudged(edge, dx); });
    *h = handle;
  }
}

void Timeline::setDuration(qreal s) {
  m_duration = s;
  placeHandles();
}
void Timeline::setPosition(qreal s) {
  m_position = s;
  update();
}
void Timeline::setTracks(const QList<Track> &tracks) {
  m_tracks = tracks;
  placeHandles();
}
void Timeline::setSelected(int track, int clip) {
  m_selTrack = track;
  m_selClip = clip;
  placeHandles();
}
void Timeline::setView(qreal zoom, qreal viewStart) {
  m_zoom = zoom;
  m_viewStart = viewStart;
  placeHandles();
}

qreal Timeline::pixelsPerSecond() const { return width() * m_zoom / qMax(0.001, m_duration); }

qreal Timeline::tickStep() const {
  const qreal target = shown().space(90) / qMax(0.001, pixelsPerSecond());
  static const qreal steps[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600};
  for (qreal s : steps)
    if (s >= target) return s;
  return 3600;
}

int Timeline::xAt(qreal time) const { return int(jsRound((time - m_viewStart) * pixelsPerSecond())); }
qreal Timeline::timeAt(qreal x) const { return qBound(0.0, m_viewStart + x / pixelsPerSecond(), m_duration); }
qreal Timeline::rulerHeight() const { return shown().space(24); }

qreal Timeline::laneHeight() const {
  if (m_tracks.isEmpty()) return 0;
  const qreal gap = shown().spacing("xs");
  return std::floor((height() - rulerHeight() - gap * (m_tracks.size() - 1)) / m_tracks.size());
}

qreal Timeline::laneY(int track) const { return rulerHeight() + track * (laneHeight() + shown().spacing("xs")); }

QRectF Timeline::clipRect(int track, int clip) const {
  const Clip c = m_tracks.value(track).value(clip);
  const qreal inset = shown().spacing("xxs");
  const int x = xAt(c.start);
  return QRectF(x, laneY(track) + inset, xAt(c.end) - x, laneHeight() - inset * 2);
}

QRectF Timeline::playheadHeadRect() const {
  const qreal head = shown().space(10);
  return QRectF(xAt(m_position) - jsRound(head / 2), rulerHeight() - head, head, head);
}

QString Timeline::format(qreal seconds) const {
  const qreal s = qMax(0.0, std::round(seconds * 10) / 10);
  const int m = int(std::floor(s / 60));
  const qreal rest = s - m * 60;
  const int whole = int(std::floor(rest));
  QString text = QString::number(m) + QLatin1Char(':') + (whole < 10 ? QStringLiteral("0") : QString()) + QString::number(whole);
  if (tickStep() < 1) text += QLatin1Char('.') + QString::number(int(jsRound((rest - whole) * 10)));
  return text;
}

TrimHandle *Timeline::trimHandle(const QString &edge) const {
  if (m_selTrack < 0 || m_selClip < 0) return nullptr;
  return edge == QLatin1String("left") ? m_left : m_right;
}

bool Timeline::isPlayheadHot() const { return hasCursor() || hasFocus() || m_headHover || m_headDrag; }

QSize Timeline::sizeHint() const { return QSize(shown().space(720), shown().space(160)); }

void Timeline::placeHandles() {
  const bool on = m_selTrack >= 0 && m_selTrack < m_tracks.size() && m_selClip >= 0 && m_selClip < m_tracks[m_selTrack].size();
  if (on) {
    const QRectF r = clipRect(m_selTrack, m_selClip);
    const int w = shown().space(8), margin = shown().spacing("sm");
    for (TrimHandle *h : {m_left, m_right}) {
      h->setGrabMargin(margin);
      h->setCapHeight(int(r.height()));
      h->setHandleGeometry(QRectF(h == m_left ? r.x() : r.right() - w, r.y(), w, r.height()));
    }
  }
  m_left->setVisible(on);
  m_right->setVisible(on);
  update();
}

void Timeline::handleMoved(const QString &edge, qreal dx) {
  emit trimmed(m_selTrack, m_selClip, edge, qBound(0.0, m_trimOrigin + dx / pixelsPerSecond(), m_duration));
}

void Timeline::handleNudged(const QString &edge, qreal dx) {
  const Clip c = m_tracks.value(m_selTrack).value(m_selClip);
  const qreal at = edge == QLatin1String("left") ? c.start : c.end;
  emit trimmed(m_selTrack, m_selClip, edge, qBound(0.0, at + dx / pixelsPerSecond(), m_duration));
}

QPair<int, int> Timeline::clipAt(const QPointF &p) const {
  for (int t = int(m_tracks.size()) - 1; t >= 0; --t)
    for (int c = int(m_tracks[t].size()) - 1; c >= 0; --c)
      if (clipRect(t, c).contains(p)) return {t, c};
  return {-1, -1};
}

bool Timeline::onHead(const QPointF &p) const {
  const qreal sm = shown().spacing("sm");
  return playheadHeadRect().adjusted(-sm, 0, sm, 0).contains(p) && m_position >= m_viewStart && m_position <= m_viewStart + m_duration / m_zoom;
}

void Timeline::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  p.setClipRect(rect());
  const qreal W = width(), rh = rulerHeight();

  // The ruler: minor ticks, five per labeled step, and the labels.
  const qreal step = tickStep(), minor = step / 5;
  const int first = int(qMax(0.0, std::floor(m_viewStart / minor)));
  const int last = int(std::ceil((m_viewStart + m_duration / m_zoom) / minor));
  for (int i = first; i <= last; ++i) {
    const qreal tm = i * minor;
    if (tm > m_duration + 1e-6) break;
    const qreal h = i % 5 == 0 ? tk.space(6) : tk.space(3);
    Paint::surface(p, QRectF(qMin(W - 1, qreal(xAt(tm))), rh - h, 1, h), 0, tk.textFaint());
  }
  const QFont caption = tk.font("caption");
  const int lf = int(qMax(0.0, std::ceil(m_viewStart / step - 1e-6)));
  const int ll = int(std::floor(qMin(m_duration, m_viewStart + m_duration / m_zoom) / step + 1e-6));
  for (int i = lf; i <= ll; ++i) {
    const QString label = format(i * step);
    const qreal w = Paint::textWidth(caption, label);
    const qreal x = qMax(0.0, qMin(W - w, xAt(i * step) - jsRound(w / 2)));
    Paint::text(p, QPointF(x, tk.spacing("xxs")), caption, tk.textDim(), label);
  }

  // Lanes and clips (clipped to the lanes region below the ruler).
  const qreal small = tk.smallRadius();
  const QFont label = tk.font("bodySmall"), bold = tk.font("bodySmall", true);
  const qreal px = tk.spacing("controlPaddingX");
  p.save();
  p.setClipRect(QRectF(0, rh, W, qMax<qreal>(0, height() - rh)));
  for (int tr = 0; tr < m_tracks.size(); ++tr) {
    Paint::surface(p, QRectF(0, laneY(tr), W, laneHeight()), small, tk.fill(State::Normal));
    for (int c = 0; c < m_tracks[tr].size(); ++c) {
      const QRectF r = clipRect(tr, c);
      const bool sel = tr == m_selTrack && c == m_selClip;
      const bool hover = tr == m_hoverTrack && c == m_hoverClip;
      const QColor fill = sel ? tk.fill(State::Selected) : hover ? tk.fill(State::Hover) : tk.fill(State::Normal);
      const Border border = sel ? Border::flat(tk.accent(), qMax(1, tk.space(2))) : tk.border(hover ? State::Hover : State::Normal);
      Paint::surface(p, r, small, fill, border);
      const QFont &f = sel ? bold : label;
      const qreal inset = sel ? tk.space(8) + px : px;
      const qreal lw = qMax(0.0, r.width() - inset * 2);
      Paint::text(p, QPointF(r.x() + inset, r.y() + Paint::centered(r.height(), Paint::lineHeight(f))), f, tk.foreground(),
                  Paint::elided(f, m_tracks[tr][c].label, lw));
      if (sel) {
        for (TrimHandle *h : {m_left, m_right})
          TrimHandle::paint(p, h->handleRect().translated(h->pos()), h->edge(), false, h->isHandleHot(), h->color(), h->gripColor());
      }
    }
  }
  p.restore();

  // The playhead: ends flush with the bottom of the last lane.
  if (m_position >= m_viewStart && m_position <= m_viewStart + m_duration / m_zoom) {
    const QColor c = isPlayheadHot() ? hotColor(tk.accent(), tk) : tk.accent();
    const QRectF head = playheadHeadRect();
    const qreal line = qMax(1, tk.space(2));
    const qreal bottom = m_tracks.isEmpty() ? qreal(height()) : laneY(int(m_tracks.size()) - 1) + laneHeight();
    Paint::surface(p, head, small, c);
    Paint::surface(p, QRectF(head.x() + jsRound((head.width() - line) / 2), head.bottom(), line, qMax(0.0, bottom - head.bottom())), 0, c);
  }
}

void Timeline::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) return Control::mousePressEvent(e);
  setFocus(Qt::MouseFocusReason);
  const QPointF pos = e->position();
  if (onHead(pos)) {
    m_headDrag = true;
  } else {
    const auto hit = clipAt(pos);
    if (hit.first >= 0) {
      emit clipPressed(hit.first, hit.second);
    } else {
      m_scrubbing = true;
      emit seek(timeAt(pos.x()));
    }
  }
  update();
  e->accept();
}

void Timeline::mouseMoveEvent(QMouseEvent *e) {
  const QPointF pos = e->position();
  if (m_headDrag || m_scrubbing) {
    emit seek(timeAt(pos.x()));
    return;
  }
  const bool head = onHead(pos);
  const auto hit = head ? qMakePair(-1, -1) : clipAt(pos);
  if (head != m_headHover || hit.first != m_hoverTrack || hit.second != m_hoverClip) {
    m_headHover = head;
    m_hoverTrack = hit.first;
    m_hoverClip = hit.second;
    update();
  }
  if (head) setCursor(Qt::SizeHorCursor);
  else if (hit.first >= 0) setCursor(Qt::PointingHandCursor);
  else unsetCursor();
}

void Timeline::mouseReleaseEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) return Control::mouseReleaseEvent(e);
  m_headDrag = m_scrubbing = false;
  update();
  e->accept();
}

void Timeline::leaveEvent(QEvent *e) {
  Control::leaveEvent(e);
  m_headHover = false;
  m_hoverTrack = m_hoverClip = -1;
  update();
}

void Timeline::wheelEvent(QWheelEvent *e) {
  const qreal steps = e->angleDelta().y() / 120.0;
  if (steps == 0) return;
  const qreal x = e->position().x();
  const qreal anchor = timeAt(x);
  auto clampView = [this](qreal z, qreal start) {
    z = qBound(1.0, z, m_maxZoom);
    const qreal visible = m_duration / z;
    return qMakePair(z, qMax(0.0, qMin(m_duration - visible, start)));
  };
  const qreal z = clampView(m_zoom * std::pow(1.25, steps), 0).first;
  const auto next = clampView(z, anchor - x / (width() * z / m_duration));
  setView(next.first, next.second);
  emit viewChanged(next.first, next.second);
  e->accept();
}

void Timeline::keyPressEvent(QKeyEvent *e) {
  const qreal step = (e->modifiers() & Qt::ShiftModifier) ? tickStep() : 1;
  switch (e->key()) {
  case Qt::Key_Left: emit seek(qMax(0.0, m_position - step)); break;
  case Qt::Key_Right: emit seek(qMin(m_duration, m_position + step)); break;
  case Qt::Key_Home: emit seek(0); break;
  case Qt::Key_End: emit seek(m_duration); break;
  default: return Control::keyPressEvent(e);
  }
  e->accept();
}

// -------------------------------------------------------------------------------------- Scrubber

Scrubber::Scrubber(QWidget *parent) : Control(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void Scrubber::setPosition(qreal s) {
  m_position = s;
  update();
}
void Scrubber::setDuration(qreal s) {
  m_duration = s;
  update();
}
void Scrubber::setSurfaceColor(const QColor &c) {
  m_surface = c;
  update();
}

QString Scrubber::format(qreal seconds) const {
  const int s = int(qMax(0.0, std::floor(seconds)));
  const int h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
  auto pad = [](int n) { return QString::number(n).rightJustified(2, QLatin1Char('0')); };
  return h > 0 ? QString::number(h) + QLatin1Char(':') + pad(m) + QLatin1Char(':') + pad(sec) : QString::number(m) + QLatin1Char(':') + pad(sec);
}

static int scrubKnob(const Tokens &tk) { return qMax(14, int(std::lround(tk.spacing("controlHeight") * 0.38))); }

QSize Scrubber::sizeHint() const {
  const Tokens &tk = t();
  return QSize(tk.space(360), qMax(tk.space(22), scrubKnob(tk) + tk.spacing("md")));
}

QRectF Scrubber::barRect() const {
  const Tokens &tk = t();
  const QFont f = tk.font("bodySmall");
  const QRectF r = paintRect();
  const qreal gap = tk.spacing("controlGap");
  const qreal x = Paint::textWidth(f, format(m_position)) + gap;
  const qreal right = r.width() - Paint::textWidth(f, format(m_duration)) - gap;
  return QRectF(r.x() + x, r.y(), right - x, r.height());
}

QRectF Scrubber::trackRect() const {
  const Tokens &tk = t();
  const QRectF bar = barRect();
  const int k = scrubKnob(tk), th = qMax(4, int(std::lround(tk.spacing("controlHeight") * 0.11)));
  return QRectF(bar.x() + k / 2.0, bar.y() + Paint::centered(bar.height(), th), bar.width() - k, th);
}

QRectF Scrubber::knobRect() const {
  const QRectF bar = barRect(), track = trackRect();
  const int k = scrubKnob(t());
  const qreal progress = m_duration > 0 ? qBound(0.0, m_position / m_duration, 1.0) : 0;
  return QRectF(track.x() + jsRound(track.width() * progress) - k / 2.0, bar.y() + Paint::centered(bar.height(), k), k, k);
}

bool Scrubber::isScrubHot() const { return hasCursor() || hasFocus() || m_barHover || m_dragging; }

void Scrubber::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF r = paintRect();
  const QFont f = tk.font("bodySmall");
  const qreal lh = Paint::lineHeight(f);
  const QString total = format(m_duration);
  Paint::text(p, QPointF(r.x(), r.y() + Paint::centered(r.height(), lh)), f, tk.textDim(), format(m_position));
  Paint::text(p, QPointF(r.right() - Paint::textWidth(f, total), r.y() + Paint::centered(r.height(), lh)), f, tk.textDim(), total);
  const QRectF bar = barRect();
  const QRectF track = trackRect();
  const qreal progress = m_duration > 0 ? qBound(0.0, m_position / m_duration, 1.0) : 0;
  const int ks = scrubKnob(tk);
  const qreal halfKnob = ks / 2.0;
  const qreal knobCenter = track.x() + jsRound(track.width() * progress);
  const qreal fillW = qMax<qreal>(0, qMin(track.width(), knobCenter - halfKnob - track.x()));
  if (fillW > 0) Paint::surface(p, QRectF(track.x(), track.y(), fillW, track.height()), track.height() / 2, tk.foreground());
  const qreal unfilledStart = qMin(track.right(), qMax(track.x(), knobCenter + halfKnob));
  const qreal unfilledW = qMax<qreal>(0, track.right() - unfilledStart);
  if (unfilledW > 0) Paint::surface(p, QRectF(unfilledStart, track.y(), unfilledW, track.height()), track.height() / 2, tk.track());
  const qreal ring = qMax(1, tk.space(2));
  const qreal inner = qMax<qreal>(4, ks - 2 * ring);
  const QRectF k(knobCenter - inner / 2.0, bar.y() + Paint::centered(bar.height(), inner), inner, inner);
  if (isScrubHot()) {
    const QPointF c = k.center();
    p.translate(c);
    p.scale(1.15, 1.15);
    p.translate(-c);
  }
  Paint::surface(p, k, k.width() / 2, tk.foreground());
}

qreal Scrubber::timeAt(qreal x) const {
  const QRectF track = trackRect();
  if (track.width() <= 0) return 0;
  return qBound(0.0, (x - track.x()) / track.width(), 1.0) * m_duration;
}

void Scrubber::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton || !barRect().contains(e->position())) return Control::mousePressEvent(e);
  setFocus(Qt::MouseFocusReason);
  m_dragging = true;
  emit seek(timeAt(e->position().x()));
  update();
  e->accept();
}

void Scrubber::mouseMoveEvent(QMouseEvent *e) {
  if (m_dragging) {
    emit seek(timeAt(e->position().x()));
    return;
  }
  const bool on = barRect().contains(e->position());
  if (on != m_barHover) {
    m_barHover = on;
    if (on) setCursor(Qt::PointingHandCursor);
    else unsetCursor();
    update();
  }
}

void Scrubber::mouseReleaseEvent(QMouseEvent *e) {
  if (!m_dragging) return Control::mouseReleaseEvent(e);
  m_dragging = false;
  update();
  e->accept();
}

void Scrubber::leaveEvent(QEvent *e) {
  Control::leaveEvent(e);
  m_barHover = false;
  update();
}

void Scrubber::keyPressEvent(QKeyEvent *e) {
  const qreal step = (e->modifiers() & Qt::ShiftModifier) ? 30 : 5;
  switch (e->key()) {
  case Qt::Key_Left: emit seek(qMax(0.0, m_position - step)); break;
  case Qt::Key_Right: emit seek(qMin(m_duration, m_position + step)); break;
  case Qt::Key_Home: emit seek(0); break;
  case Qt::Key_End: emit seek(m_duration); break;
  default: return Control::keyPressEvent(e);
  }
  e->accept();
}

// ------------------------------------------------------------------------------------- CropFrame

QStringList CropFrame::edges() {
  return {QStringLiteral("top-left"),    QStringLiteral("top"),    QStringLiteral("top-right"),    QStringLiteral("left"),
          QStringLiteral("right"),       QStringLiteral("bottom-left"), QStringLiteral("bottom"), QStringLiteral("bottom-right")};
}

CropFrame::CropFrame(QWidget *parent) : Control(parent) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  for (const QString &edge : edges()) {
    auto *h = new TrimHandle(edge, this);
    h->setCrop(true);
    connect(h, &TrimHandle::draggingChanged, this, [this, edge](bool on) { on ? emit pressed(edge) : emit released(edge); });
    connect(h, &TrimHandle::moved, this, [this, edge](qreal dx, qreal dy) { emit moved(edge, dx, dy); });
    connect(h, &TrimHandle::nudged, this, [this, edge](qreal dx, qreal dy) { emit nudged(edge, dx, dy); });
    m_handles << h;
  }
}

void CropFrame::setCursorEdge(const QString &edge) {
  m_cursorEdge = edge;
  for (TrimHandle *h : m_handles) h->setHasCursor(h->edge() == edge);
}

TrimHandle *CropFrame::handle(const QString &edge) const {
  for (TrimHandle *h : m_handles)
    if (h->edge() == edge) return h;
  return nullptr;
}

QSize CropFrame::sizeHint() const { return QSize(shown().space(240), shown().space(140)); }

void CropFrame::place() {
  const qreal W = width(), H = height();
  const int margin = shown().spacing("sm");
  for (TrimHandle *h : m_handles) {
    h->setGrabMargin(margin);
    const QSize s = h->handleSize();
    const QString e = h->edge();
    const qreal x = e.contains(QLatin1String("left")) ? 0 : e.contains(QLatin1String("right")) ? W - s.width() : jsRound((W - s.width()) / 2);
    const qreal y = e.startsWith(QLatin1String("top")) ? 0 : e.startsWith(QLatin1String("bottom")) ? H - s.height() : jsRound((H - s.height()) / 2);
    h->setHandleGeometry(QRectF(x, y, s.width(), s.height()));
  }
  update();
}

void CropFrame::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const qreal W = width(), H = height();
  for (int i = 1; i <= 2; ++i) {
    Paint::surface(p, QRectF(jsRound(W * i / 3), 0, 1, H), 0, tk.track());
    Paint::surface(p, QRectF(0, jsRound(H * i / 3), W, 1), 0, tk.track());
  }
  Paint::surface(p, QRectF(rect()), 0, Qt::transparent, Border::flat(tk.accent(), 1));
}

// ------------------------------------------------------------------------------------ DragHandle

DragHandle::DragHandle(QWidget *parent) : Control(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setCursor(Qt::OpenHandCursor);
}

bool DragHandle::isGripHot() const { return hasCursor() || hasFocus() || isHovered() || m_dragging; }

QSize DragHandle::sizeHint() const { return QSize(shown().space(16), shown().space(22)); }

void DragHandle::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF r = paintRect();
  const qreal d = tk.space(3), g = tk.space(3);
  const qreal x0 = r.x() + Paint::centered(r.width(), d * 2 + g), y0 = r.y() + Paint::centered(r.height(), d * 3 + g * 2);
  const QColor c = isGripHot() ? tk.foreground() : tk.textDim();
  for (int i = 0; i < 6; ++i) Paint::surface(p, QRectF(x0 + (i % 2) * (d + g), y0 + (i / 2) * (d + g), d, d), qMin<qreal>(tk.smallRadius(), d / 2), c);
}

void DragHandle::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) return Control::mousePressEvent(e);
  setFocus(Qt::MouseFocusReason);
  m_origin = globalPos(e);
  m_dragging = true;
  setCursor(Qt::ClosedHandCursor);
  emit pressed();
  update();
  e->accept();
}

void DragHandle::mouseMoveEvent(QMouseEvent *e) {
  if (!m_dragging) return;
  const QPointF d = globalPos(e) - m_origin;
  emit moved(d.x(), d.y());
}

void DragHandle::mouseReleaseEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton || !m_dragging) return Control::mouseReleaseEvent(e);
  m_dragging = false;
  setCursor(Qt::OpenHandCursor);
  emit released();
  update();
  e->accept();
}

void DragHandle::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Up) emit moveRequested(-1);
  else if (e->key() == Qt::Key_Down) emit moveRequested(1);
  else return Control::keyPressEvent(e);
  e->accept();
}

// --------------------------------------------------------------------------------- DropIndicator

DropIndicator::DropIndicator(QWidget *parent) : QWidget(parent) {
  follow(this);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

int DropIndicator::thickness() { return qMax(1, shown().space(2)); }

void DropIndicator::placeAt(int x, int y, int width) {
  const int h = thickness();
  setGeometry(x, y - int(jsRound(h / 2.0)), width, h);
  raise();
}

QSize DropIndicator::sizeHint() const { return QSize(shown().space(200), thickness()); }

void DropIndicator::paintEvent(QPaintEvent *) {
  QPainter p(this);
  const Tokens &tk = shown();
  Paint::surface(p, QRectF(rect()), qMin<qreal>(tk.smallRadius(), height() / 2.0), tk.accent());
}

}  // namespace Omarchy
