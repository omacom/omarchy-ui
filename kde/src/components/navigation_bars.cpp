// Toolbar, ToolbarSeparator, Tabs and IconRail (Toolbar.qml, ToolbarSeparator.qml, Tabs.qml,
// IconRail.qml).

#include <QEasingCurve>
#include <QKeyEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <cmath>

#include "Omarchy/Button.h"
#include "Omarchy/Navigation.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

namespace NavDetail {
void glyphByInk(QPainter &p, const QRectF &box, const QString &text, qreal fontSize, const QColor &color);
int ceilPx(qreal v);
}  // namespace NavDetail
using NavDetail::ceilPx;
using NavDetail::glyphByInk;

static int qmlRound(qreal v) { return int(std::floor(v + 0.5)); }

// The easeOut curve (motion.easeOut), what the underline and the rail's bar slide on.
static QEasingCurve easeOut() {
  QEasingCurve c(QEasingCurve::BezierSpline);
  c.addCubicBezierSegment(QPointF(0.23, 1.0), QPointF(0.32, 1.0), QPointF(1, 1));
  return c;
}

// ----------------------------------------------------------------------------- ToolbarSeparator

ToolbarSeparator::ToolbarSeparator(QWidget *parent) : QWidget(parent) {
  follow(this);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize ToolbarSeparator::sizeHint() const { return QSize(shown().spacing("sm") * 2 + 1, shown().space(16)); }

void ToolbarSeparator::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.fillRect(QRect(shown().spacing("sm"), 0, 1, height()), shown().track());
}

// -------------------------------------------------------------------------------------- Toolbar

static QWidget *slot(QWidget *parent, Row *&row) {
  auto *w = new QWidget(parent);
  row = new Row(w);
  return w;
}

Toolbar::Toolbar(QWidget *parent) : QWidget(parent) {
  follow(this);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_leading = slot(this, m_leadingRow);
  m_center = slot(this, m_centerRow);
  m_end = slot(this, m_endRow);
  m_trailing = slot(m_end, m_trailingRow);
  m_endRow->addWidget(m_trailing);
  m_overflow = new IconButton(QString(QChar(0xF141)), m_end);
  m_overflow->hide();
  m_endRow->addWidget(m_overflow);
  connect(m_overflow, &IconButton::clicked, this, &Toolbar::overflowTriggered);
  respace();
  connect(Theme::instance(), &Theme::metricsChanged, this, [this] {
    respace();
    place();
  });
  for (QWidget *w : {m_leading, m_center, m_end, m_trailing}) w->installEventFilter(this);
}

void Toolbar::respace() {
  const int xxs = shown().spacing("xxs");
  for (Row *r : {m_leadingRow, m_centerRow, m_endRow, m_trailingRow}) r->setSpacing(xxs);
  updateGeometry();
}

void Toolbar::addLeading(QWidget *w) {
  m_leadingRow->addWidget(w);
  updateGeometry();
  place();
}
void Toolbar::addCenter(QWidget *w) {
  m_centerRow->addWidget(w);
  updateGeometry();
  place();
}
void Toolbar::addTrailing(QWidget *w) {
  m_trailingRow->addWidget(w);
  updateGeometry();
  place();
}
ToolbarSeparator *Toolbar::addSeparator() {
  auto *s = new ToolbarSeparator;
  addLeading(s);
  return s;
}

bool Toolbar::hasOverflow() const { return !m_overflow->isHidden(); }
void Toolbar::setOverflow(bool on) {
  m_overflow->setVisible(on);
  m_endRow->invalidate();
  updateGeometry();
  place();
}
bool Toolbar::overflowHasCursor() const { return m_overflow->hasCursor(); }
void Toolbar::setOverflowHasCursor(bool on) { m_overflow->setHasCursor(on); }
void Toolbar::setDivided(bool on) {
  m_divided = on;
  updateGeometry();
  update();
}

QSize Toolbar::sizeHint() const {
  const Tokens &tk = shown();
  const QSize l = m_leadingRow->sizeHint(), c = m_centerRow->sizeHint(), e = m_endRow->sizeHint();
  const int pad = tk.spacing("sm");
  const int content = qMax(l.height(), qMax(c.height(), e.height()));
  return QSize(l.width() + c.width() + e.width() + pad * 2 + tk.spacing("huge") * 2, content + pad * 2 + (m_divided ? 1 : 0));
}

// Leading at the left, center centered on the whole bar, the end (trailing, then the overflow)
// at the right; each vertically centered on the content line.
void Toolbar::place() {
  const int pad = shown().spacing("sm");
  m_trailing->setFixedSize(m_trailingRow->sizeHint());
  const QSize l = m_leadingRow->sizeHint(), c = m_centerRow->sizeHint(), e = m_endRow->sizeHint();
  const int content = qMax(l.height(), qMax(c.height(), e.height()));
  m_leading->setGeometry(pad, pad + qmlRound((content - l.height()) / 2.0), l.width(), l.height());
  m_center->setGeometry(qmlRound((width() - c.width()) / 2.0), pad + qmlRound((content - c.height()) / 2.0), c.width(), c.height());
  m_end->setGeometry(width() - pad - e.width(), pad + qmlRound((content - e.height()) / 2.0), e.width(), e.height());
}

void Toolbar::resizeEvent(QResizeEvent *) { place(); }

// A slot's items changed size: the slots move.
bool Toolbar::eventFilter(QObject *o, QEvent *e) {
  if (e->type() == QEvent::LayoutRequest) {
    updateGeometry();
    place();
  }
  return QWidget::eventFilter(o, e);
}

void Toolbar::paintEvent(QPaintEvent *) {
  if (!m_divided) return;
  QPainter p(this);
  p.fillRect(QRect(0, height() - 1, width(), 1), shown().track());
}

// ----------------------------------------------------------------------------------------- Tabs

Tabs::Tabs(QWidget *parent) : QWidget(parent) {
  follow(this);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAttribute(Qt::WA_Hover);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_anim = new QVariantAnimation(this);
  m_anim->setEasingCurve(easeOut());
  connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    m_bar = v.toRectF();
    update();
  });
  connect(Theme::instance(), &Theme::metricsChanged, this, [this] { m_anim->stop(); });
}

void Tabs::setModel(const QStringList &labels) {
  m_model = labels;
  m_anim->stop();
  updateGeometry();
  update();
}

void Tabs::setCurrentIndex(int i) {
  if (i == m_current) return;
  const int was = m_current;
  m_current = i;
  slide(was);
  emit currentIndexChanged(i);
}

void Tabs::setCursorIndex(int i) {
  m_cursor = i;
  update();
}

void Tabs::select(int i) {
  const int n = int(m_model.size());
  if (n == 0) return;
  i = qBound(0, i, n - 1);
  if (i == m_current) return;
  setCurrentIndex(i);
  emit activated(i);
}

// Qt Quick's Row of tabs: each ⌈label⌉ + 2 × controlPaddingX wide (the selected one's label bold),
// xxs apart.
QRectF Tabs::tabRect(int i) const {
  const Tokens &tk = shown();
  const qreal pad = tk.spacing("controlPaddingX"), xxs = tk.spacing("xxs");
  const qreal h = Paint::lineHeight(tk.font("body")) + tk.spacing("controlPaddingY") * 2;
  qreal x = 0;
  for (int k = 0; k < m_model.size(); ++k) {
    const qreal w = ceilPx(Paint::textWidth(tk.font("body", k == m_current), m_model[k])) + pad * 2;
    if (k == i) return QRectF(x, 0, w, h);
    x += w + xxs;
  }
  return QRectF();
}

QSizeF Tabs::implicitSize() const {
  if (m_model.isEmpty()) return QSizeF(0, 0);
  const QRectF last = tabRect(int(m_model.size()) - 1);
  return QSizeF(last.right(), last.height());
}

QSize Tabs::sizeHint() const {
  const QSizeF s = implicitSize();
  return QSize(ceilPx(s.width()), ceilPx(s.height()));
}

int Tabs::indexAt(const QPointF &p) const {
  for (int i = 0; i < m_model.size(); ++i)
    if (tabRect(i).contains(p)) return i;
  return -1;
}

// The underline slides from where it is drawn now to the selected tab.
void Tabs::slide(int from) {
  const QRectF target = tabRect(m_current);
  const QRectF start = m_anim->state() == QAbstractAnimation::Running ? m_bar : tabRect(from);
  const int ms = shown().duration(160);
  m_anim->stop();
  update();
  if (ms <= 0 || !start.isValid() || !isVisible()) return;
  m_anim->setDuration(ms);
  m_anim->setStartValue(QRectF(start.x(), 0, start.width(), 1));
  m_anim->setEndValue(QRectF(target.x(), 0, target.width(), 1));
  m_anim->start();
}

static void fillTopRounded(QPainter &p, const QRectF &r, qreal radius, const QColor &fill) {
  if (fill.alpha() <= 0 || r.width() <= 0 || r.height() <= 0) return;
  radius = qMax<qreal>(0, qMin(radius, qMin(r.width() / 2, r.height())));
  p.save();
  p.setPen(Qt::NoPen);
  p.setRenderHint(QPainter::Antialiasing, radius > 0);
  if (radius <= 0) {
    p.fillRect(r, fill);
  } else {
    const qreal d = radius * 2;
    QPainterPath path;
    path.moveTo(r.left(), r.bottom());
    path.lineTo(r.left(), r.top() + radius);
    path.arcTo(QRectF(r.left(), r.top(), d, d), 180, -90);
    path.lineTo(r.right() - radius, r.top());
    path.arcTo(QRectF(r.right() - d, r.top(), d, d), 90, -90);
    path.lineTo(r.right(), r.bottom());
    path.closeSubpath();
    p.fillPath(path, fill);
  }
  p.restore();
}

void Tabs::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  p.fillRect(QRectF(0, height() - 1, width(), 1), tk.track());
  for (int i = 0; i < m_model.size(); ++i) {
    const QRectF r = tabRect(i);
    const bool selected = i == m_current;
    const bool hot = m_hover == i || m_cursor == i || (hasFocus() && selected);
    if (m_pressed == i && m_hover == i) fillTopRounded(p, r, tk.smallRadius(), tk.fill(State::Pressed));
    else if (hot) fillTopRounded(p, r, tk.smallRadius(), tk.fill(State::Hover));
    const QFont f = tk.font("body", selected);
    const QSizeF s = Paint::textSize(f, m_model[i]);
    const QColor c = selected ? tk.selectedText() : hot ? tk.foreground() : tk.textDim();
    Paint::text(p, QPointF(r.x() + Paint::centered(r.width(), s.width()), r.y() + Paint::centered(r.height(), s.height())), f, c, m_model[i]);
  }
  if (m_current >= 0 && m_current < m_model.size()) {
    const QRectF bar = m_anim->state() == QAbstractAnimation::Running ? m_bar : tabRect(m_current);
    const qreal ul = tk.spacing("xxs");
    p.fillRect(QRectF(bar.x(), height() - ul, bar.width(), ul), tk.accent());
  }
}

void Tabs::keyPressEvent(QKeyEvent *e) {
  const QString t = e->text();
  const int key = e->key();
  if (key == Qt::Key_Left || t == QLatin1String("h")) select(m_current - 1);
  else if (key == Qt::Key_Right || t == QLatin1String("l")) select(m_current + 1);
  else if (key == Qt::Key_Home) select(0);
  else if (key == Qt::Key_End) select(int(m_model.size()) - 1);
  else {
    QWidget::keyPressEvent(e);
    return;
  }
  e->accept();
}

void Tabs::mouseMoveEvent(QMouseEvent *e) {
  const int i = indexAt(e->position());
  if (i != m_hover) {
    m_hover = i;
    update();
  }
}

void Tabs::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
  m_hover = indexAt(e->position());
  m_pressed = m_hover;
  update();
}

void Tabs::mouseReleaseEvent(QMouseEvent *e) {
  const int i = indexAt(e->position());
  const int was = m_pressed;
  m_pressed = -1;
  update();
  if (e->button() == Qt::LeftButton && was >= 0 && was == i) {
    setFocus(Qt::MouseFocusReason);
    select(i);
  }
}

void Tabs::leaveEvent(QEvent *e) {
  QWidget::leaveEvent(e);
  m_hover = -1;
  update();
}

void Tabs::focusInEvent(QFocusEvent *e) {
  QWidget::focusInEvent(e);
  update();
}
void Tabs::focusOutEvent(QFocusEvent *e) {
  QWidget::focusOutEvent(e);
  update();
}

// ------------------------------------------------------------------------------------- IconRail

IconRail::IconRail(QWidget *parent) : QWidget(parent) {
  follow(this);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAttribute(Qt::WA_Hover);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_anim = new QVariantAnimation(this);
  m_anim->setEasingCurve(easeOut());
  connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    m_barY = v.toReal();
    update();
  });
}

void IconRail::setModel(const QList<Item> &items) {
  m_model = items;
  updateGeometry();
  update();
}

void IconRail::setCurrentIndex(int i) {
  if (i == m_current) return;
  const int was = m_current;
  m_current = i;
  const int ms = shown().duration(160);
  m_anim->stop();
  if (ms > 0 && isVisible() && was >= 0 && was < m_model.size() && i >= 0 && i < m_model.size()) {
    m_anim->setDuration(ms);
    m_anim->setStartValue(m_barY >= 0 ? m_barY : barY(was));
    m_anim->setEndValue(barY(i));
    m_anim->start();
  } else {
    m_barY = -1;
  }
  update();
  emit currentIndexChanged(i);
}

void IconRail::setCursorIndex(int i) {
  m_cursor = i;
  update();
}

void IconRail::select(int i) {
  const int n = int(m_model.size());
  if (n == 0) return;
  i = qBound(0, i, n - 1);
  if (i == m_current) return;
  setCurrentIndex(i);
  emit activated(i);
}

int IconRail::itemSize() const { return shown().spacing("controlHeight") + shown().spacing("sm") * 2; }

QRectF IconRail::itemRect(int i) const {
  const int pad = shown().spacing("sm"), s = itemSize();
  return QRectF(pad, pad + i * (s + pad), s, s);
}

qreal IconRail::barY(int i) const {
  const QRectF r = itemRect(i);
  return r.y() + qmlRound((r.height() - shown().fontSize("iconLarge")) / 2.0);
}

QSize IconRail::sizeHint() const {
  const int pad = shown().spacing("sm"), s = itemSize(), n = int(m_model.size());
  return QSize(s + pad * 2 + 1, (n > 0 ? n * s + (n - 1) * pad : 0) + pad * 2);
}

int IconRail::indexAt(const QPointF &p) const {
  for (int i = 0; i < m_model.size(); ++i)
    if (itemRect(i).contains(p)) return i;
  return -1;
}

void IconRail::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  p.fillRect(QRectF(width() - 1, 0, 1, height()), tk.track());
  const int large = tk.fontSize("iconLarge");
  for (int i = 0; i < m_model.size(); ++i) {
    const QRectF r = itemRect(i);
    const bool selected = i == m_current;
    const bool hot = m_hover == i || m_cursor == i || (hasFocus() && selected);
    if (m_pressed == i && m_hover == i) Paint::surface(p, r, tk.radius(), tk.fill(State::Pressed));
    else if (hot) Paint::surface(p, r, tk.radius(), tk.fill(State::Hover));
    glyphByInk(p, r, m_model[i].icon, large, selected ? tk.accent() : hot ? tk.foreground() : tk.textDim());
  }
  if (m_current >= 0 && m_current < m_model.size()) {
    const qreal y = m_anim->state() == QAbstractAnimation::Running && m_barY >= 0 ? m_barY : barY(m_current);
    Paint::surface(p, QRectF(0, y, tk.spacing("xxs"), large), qMin(1, tk.radius()), tk.accent());
  }
}

void IconRail::keyPressEvent(QKeyEvent *e) {
  const QString t = e->text();
  const int key = e->key();
  if (key == Qt::Key_Up || t == QLatin1String("k")) select(m_current - 1);
  else if (key == Qt::Key_Down || t == QLatin1String("j")) select(m_current + 1);
  else if (key == Qt::Key_Home) select(0);
  else if (key == Qt::Key_End) select(int(m_model.size()) - 1);
  else {
    QWidget::keyPressEvent(e);
    return;
  }
  e->accept();
}

void IconRail::mouseMoveEvent(QMouseEvent *e) {
  const int i = indexAt(e->position());
  if (i != m_hover) {
    m_hover = i;
    update();
  }
}

void IconRail::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
  m_hover = indexAt(e->position());
  m_pressed = m_hover;
  update();
}

void IconRail::mouseReleaseEvent(QMouseEvent *e) {
  const int i = indexAt(e->position());
  const int was = m_pressed;
  m_pressed = -1;
  update();
  if (e->button() == Qt::LeftButton && was >= 0 && was == i) {
    setFocus(Qt::MouseFocusReason);
    select(i);
  }
}

void IconRail::leaveEvent(QEvent *e) {
  QWidget::leaveEvent(e);
  m_hover = -1;
  update();
}

void IconRail::focusInEvent(QFocusEvent *e) {
  QWidget::focusInEvent(e);
  update();
}
void IconRail::focusOutEvent(QFocusEvent *e) {
  QWidget::focusOutEvent(e);
  update();
}

}  // namespace Omarchy
