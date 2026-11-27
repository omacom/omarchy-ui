// ColorWell, SwatchStrip and Dialog.

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <cmath>

#include "Omarchy/Forms.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

static bool isActivateKey(const QKeyEvent *e) {
  return e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space;
}

// ------------------------------------------------------------------------------------- ColorWell

ColorWell::ColorWell(const QColor &swatch, QWidget *parent) : Control(parent), m_swatch(swatch) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::TabFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_give = new QVariantAnimation(this);
  m_give->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_give, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    m_scale = v.toReal();
    update();
  });
}

QColor ColorWell::swatch() const {
  if (m_swatchFn) return m_swatchFn(t());
  return m_swatch.isValid() ? m_swatch : t().accent();
}

void ColorWell::setSwatch(const QColor &c) {
  m_swatch = c;
  m_swatchFn = nullptr;
  update();
}

void ColorWell::setSwatch(std::function<QColor(const Tokens &)> swatch) {
  m_swatchFn = std::move(swatch);
  update();
}

// The well, space(44) × space(26), with the cursor ring sm outside it.
QSize ColorWell::sizeHint() const {
  const Tokens &tk = t();
  const int pad = tk.spacing("sm");
  return QSize(tk.space(44) + pad * 2, tk.space(26) + pad * 2);
}

void ColorWell::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF pr = paintRect();
  const int radius = tk.smallRadius();
  if (isHot() || hasFocus()) Paint::surface(p, pr, radius, Qt::transparent, tk.border(State::Hover));
  const int pad = tk.spacing("sm");
  const QRectF well(pr.x() + pad, pr.y() + pad, tk.space(44), tk.space(26));
  if (m_scale != 1) {
    p.translate(well.center());
    p.scale(m_scale, m_scale);
    p.translate(-well.center());
  }
  const Border b = tk.border(State::Normal);
  Paint::surface(p, well, radius, tk.fill(State::Normal), b);
  const int inset = b.top + tk.spacing("xxs");
  Paint::surface(p, well.adjusted(inset, inset, -inset, -inset), qMax(0, radius - tk.spacing("xxs")), swatch());
}

void ColorWell::give(bool pressed) {
  const qreal to = pressed ? 0.97 : 1.0;
  const int ms = t().duration(160);
  m_give->stop();
  if (ms <= 0) {
    m_scale = to;
    update();
    return;
  }
  m_give->setDuration(ms);
  m_give->setStartValue(m_scale);
  m_give->setEndValue(to);
  m_give->start();
}

void ColorWell::mousePressEvent(QMouseEvent *e) {
  Control::mousePressEvent(e);
  if (isPressed()) give(true);
}

void ColorWell::mouseReleaseEvent(QMouseEvent *e) {
  give(false);
  Control::mouseReleaseEvent(e);
}

void ColorWell::keyPressEvent(QKeyEvent *e) {
  if (isActivateKey(e)) {
    emit clicked();
    e->accept();
    return;
  }
  Control::keyPressEvent(e);
}

void ColorWell::activate(Qt::MouseButton b) {
  if (b == Qt::LeftButton) emit clicked();
}

// ----------------------------------------------------------------------------------- SwatchStrip

SwatchStrip::SwatchStrip(QWidget *parent) : Control(parent) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::TabFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void SwatchStrip::setColors(const QList<QColor> &colors) {
  m_colors = colors;
  m_colorsFn = nullptr;
  updateGeometry();
  update();
}

void SwatchStrip::setColors(std::function<QList<QColor>(const Tokens &)> colors) {
  m_colorsFn = std::move(colors);
  updateGeometry();
  update();
}

QList<QColor> SwatchStrip::colors() const { return m_colorsFn ? m_colorsFn(t()) : m_colors; }

void SwatchStrip::setCurrentIndex(int i) {
  m_current = i;
  update();
}

void SwatchStrip::setCursorIndex(int i) {
  m_cursor = i;
  update();
}

void SwatchStrip::pick(int index) {
  if (index < 0 || index >= colors().size()) return;
  m_current = index;
  update();
  emit picked(index);
}

// A chip, its ring gap and the ring on each side; cells xxs apart.
int SwatchStrip::cellSize() const {
  const Tokens &tk = t();
  return tk.space(22) + (tk.spacing("xxs") + qMax(1, tk.space(2))) * 2;
}

QRectF SwatchStrip::cellRect(int index) const {
  const int cell = cellSize();
  const QRectF pr = paintRect();
  return QRectF(pr.x() + index * (cell + t().spacing("xxs")), pr.y(), cell, cell);
}

int SwatchStrip::indexAt(const QPointF &p) const {
  const int n = int(colors().size());
  for (int i = 0; i < n; ++i)
    if (cellRect(i).contains(p)) return i;
  return -1;
}

QSize SwatchStrip::sizeHint() const {
  const int n = int(colors().size());
  const int cell = cellSize();
  return QSize(n > 0 ? n * cell + (n - 1) * t().spacing("xxs") : 0, cell);
}

void SwatchStrip::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QList<QColor> cs = colors();
  const int chip = tk.space(22), ring = qMax(1, tk.space(2)), gap = tk.spacing("xxs");
  const int radius = tk.smallRadius();
  for (int i = 0; i < cs.size(); ++i) {
    const QRectF cell = cellRect(i);
    const bool chosen = i == m_current, hot = i == m_cursor || i == m_hover;
    if (chosen || hot)
      Paint::surface(p, cell, radius > 0 ? radius + gap : 0, Qt::transparent, Border::flat(chosen ? tk.accent() : tk.border(State::Hover).color, ring));
    const QRectF c(cell.x() + Paint::centered(cell.width(), chip), cell.y() + Paint::centered(cell.height(), chip), chip, chip);
    Paint::surface(p, c, radius, cs[i], Border::flat(tk.track(), 1));
  }
}

void SwatchStrip::keyPressEvent(QKeyEvent *e) {
  const int n = int(colors().size());
  const int from = m_cursor >= 0 ? m_cursor : qMax(0, m_current);
  if (e->key() == Qt::Key_Right || e->text() == QLatin1String("l")) setCursorIndex(qMin(n - 1, from + 1));
  else if (e->key() == Qt::Key_Left || e->text() == QLatin1String("h")) setCursorIndex(qMax(0, from - 1));
  else if (isActivateKey(e)) pick(from);
  else return Control::keyPressEvent(e);
  e->accept();
}

void SwatchStrip::mousePressEvent(QMouseEvent *e) {
  m_pressAt = e->position();
  Control::mousePressEvent(e);
}

void SwatchStrip::mouseMoveEvent(QMouseEvent *e) {
  const int i = indexAt(e->position());
  if (i != m_hover) {
    m_hover = i;
    update();
  }
  Control::mouseMoveEvent(e);
}

void SwatchStrip::leaveEvent(QEvent *e) {
  m_hover = -1;
  update();
  Control::leaveEvent(e);
}

void SwatchStrip::activate(Qt::MouseButton b) {
  if (b != Qt::LeftButton) return;
  pick(indexAt(m_pressAt));
}

// ---------------------------------------------------------------------------------------- Dialog

Dialog::Dialog(QWidget *parent) : Dialog(QString(), QString(), parent) {}

Dialog::Dialog(const QString &title, const QString &message, QWidget *parent) : QWidget(parent), m_title(title), m_message(message) {
  follow(this);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  m_body = new QWidget(this);
  m_bodyColumn = new Column(m_body);
  m_bodyColumn->setSpacing(shown().space(10));
  m_body->hide();
  connect(Theme::instance(), &Theme::metricsChanged, this, &Dialog::place);
  if (parent) {
    parent->installEventFilter(this);
    setGeometry(parent->rect());
  }
  hide();
}

void Dialog::setOpened(bool on) {
  if (on) {
    if (parentWidget()) setGeometry(parentWidget()->rect());
    show();
    raise();
    place();
    setFocus(Qt::OtherFocusReason);
  } else {
    hide();
  }
}

void Dialog::setTitle(const QString &t) {
  m_title = t;
  place();
  update();
}
void Dialog::setMessage(const QString &t) {
  m_message = t;
  place();
  update();
}
void Dialog::addWidget(QWidget *w) {
  w->setParent(m_body);
  m_bodyColumn->addWidget(w);
  w->show();
  m_body->show();
  place();
}
void Dialog::setSecondaryText(const QString &t) {
  m_secondary = t;
  update();
}
void Dialog::setPrimaryText(const QString &t) {
  m_primary = t;
  update();
}
QString Dialog::secondaryText() const {
  return m_secondary.isEmpty() ? Theme::instance()->string(QStringLiteral("common.cancel"), QStringLiteral("Cancel")) : m_secondary;
}
QString Dialog::primaryText() const {
  return m_primary.isEmpty() ? Theme::instance()->string(QStringLiteral("common.confirm"), QStringLiteral("Confirm")) : m_primary;
}
void Dialog::setDestructive(bool on) {
  m_destructive = on;
  update();
}
void Dialog::setSelectedIndex(int i) {
  m_selected = i ? 1 : 0;
  update();
}

// The card's column: title, message and body, space(10) apart (those that show).
qreal Dialog::contentHeight(qreal width) const {
  const Tokens &tk = shown();
  QList<qreal> parts;
  if (!m_title.isEmpty()) parts << Paint::wrappedHeight(tk.font("title", true), m_title, width);
  if (!m_message.isEmpty()) parts << Paint::wrappedHeight(tk.font("body"), m_message, width);
  if (m_bodyColumn->count() > 0) parts << m_bodyColumn->heightForWidth(int(width));
  qreal h = 0;
  for (qreal p : parts) h += p;
  if (parts.size() > 1) h += (parts.size() - 1) * tk.space(10);
  return h;
}

QRectF Dialog::cardRect() const {
  const Tokens &tk = shown();
  const int inset = tk.border(State::Normal).top + tk.space(18);
  const qreal w = qMin<qreal>(width() - tk.space(32), tk.space(370));
  const qreal h = inset * 2 + contentHeight(w - inset * 2) + tk.space(20) + tk.space(34);
  return QRectF(Paint::centered(width(), w), Paint::centered(height(), h), w, h);
}

QRectF Dialog::buttonRect(int index) const {
  const Tokens &tk = shown();
  const int inset = tk.border(State::Normal).top + tk.space(18);
  const QRectF card = cardRect();
  const int bw = tk.space(88), bh = tk.space(34), gap = tk.space(10);
  const qreal right = card.right() - inset;
  return QRectF(index == 1 ? right - bw : right - bw * 2 - gap, card.bottom() - inset - bh, bw, bh);
}

void Dialog::place() {
  if (m_bodyColumn->count() == 0) return;
  const Tokens &tk = shown();
  m_bodyColumn->setSpacing(tk.space(10));
  const int inset = tk.border(State::Normal).top + tk.space(18);
  const QRectF card = cardRect();
  const qreal w = card.width() - inset * 2;
  qreal y = card.y() + inset;
  if (!m_title.isEmpty()) y += Paint::wrappedHeight(tk.font("title", true), m_title, w) + tk.space(10);
  if (!m_message.isEmpty()) y += Paint::wrappedHeight(tk.font("body"), m_message, w) + tk.space(10);
  m_body->setGeometry(QRectF(card.x() + inset, y, w, m_bodyColumn->heightForWidth(int(w))).toAlignedRect());
}

void Dialog::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  p.fillRect(rect(), tk.scrim());
  const QRectF card = cardRect();
  const int bw = tk.border(State::Normal).top;
  const int inset = bw + tk.space(18);
  const QColor fg = tk.foreground(), urgent = tk.urgent();
  const QColor selBg = tk.color(QStringLiteral("surfaces.menu.selectedBackground"));
  const QColor selText = tk.color(QStringLiteral("surfaces.menu.selectedText"));
  Paint::surface(p, card, tk.radius(), tk.background(), Border::flat(selText, bw));

  const qreal w = card.width() - inset * 2;
  const qreal x = card.x() + inset;
  qreal y = card.y() + inset;
  if (!m_title.isEmpty()) {
    const QFont f = tk.font("title", true);
    Paint::wrapped(p, QRectF(x, y, w, 1e6), f, fg, m_title);
    y += Paint::wrappedHeight(f, m_title, w) + tk.space(10);
  }
  if (!m_message.isEmpty()) Paint::wrapped(p, QRectF(x, y, w, 1e6), tk.font("body"), fg, m_message);

  const QString labels[2] = {secondaryText(), primaryText()};
  const QFont f = tk.font("caption");
  for (int i = 0; i < 2; ++i) {
    const bool selected = m_selected == i, urgentButton = i == 1 && m_destructive;
    const QColor fill = selected ? (urgentButton ? Paint::alpha(urgent, 0.22) : selBg) : QColor(Qt::transparent);
    const QColor line = urgentButton ? (selected ? urgent : Paint::alpha(urgent, 0.56)) : (selected ? selText : Paint::alpha(fg, 0.38));
    const QColor text = urgentButton ? (selected ? urgent : fg) : (selected ? selText : fg);
    const QRectF r = buttonRect(i);
    Paint::surface(p, r, tk.radius(), fill, Border::flat(line, bw));
    const QSizeF s = Paint::textSize(f, labels[i]);
    Paint::text(p, QPointF(r.x() + Paint::centered(r.width(), s.width()), r.y() + Paint::centered(r.height(), s.height())), f, text, labels[i]);
  }
}

bool Dialog::handleKey(QKeyEvent *e) {
  if (!isVisible()) return false;
  const int k = e->key();
  if (k == Qt::Key_Escape) {
    emit rejected();
  } else if (k == Qt::Key_Left || k == Qt::Key_Right || e->text() == QLatin1String("h") || e->text() == QLatin1String("l") || k == Qt::Key_Tab ||
             k == Qt::Key_Backtab) {
    setSelectedIndex(m_selected == 0 ? 1 : 0);
  } else if (k == Qt::Key_Return || k == Qt::Key_Enter) {
    if (m_selected == 0) emit rejected();
    else emit accepted();
  } else {
    return false;
  }
  return true;
}

void Dialog::keyPressEvent(QKeyEvent *e) {
  if (handleKey(e)) e->accept();
  else QWidget::keyPressEvent(e);
}

bool Dialog::event(QEvent *e) {
  // Tab moves between the buttons rather than out of the dialog.
  if (e->type() == QEvent::KeyPress) {
    auto *k = static_cast<QKeyEvent *>(e);
    if ((k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) && handleKey(k)) return true;
  }
  return QWidget::event(e);
}

bool Dialog::eventFilter(QObject *o, QEvent *e) {
  if (o == parentWidget() && e->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
  return QWidget::eventFilter(o, e);
}

void Dialog::mousePressEvent(QMouseEvent *e) {
  m_pressedOnScrim = !cardRect().contains(e->position());
  e->accept();
}

void Dialog::mouseMoveEvent(QMouseEvent *e) {
  for (int i = 0; i < 2; ++i)
    if (buttonRect(i).contains(e->position()) && m_selected != i) setSelectedIndex(i);
}

void Dialog::mouseReleaseEvent(QMouseEvent *e) {
  const QPointF pt = e->position();
  const bool scrim = m_pressedOnScrim;
  m_pressedOnScrim = false;
  if (e->button() != Qt::LeftButton) return;
  if (buttonRect(0).contains(pt)) emit rejected();
  else if (buttonRect(1).contains(pt)) emit accepted();
  else if (scrim && rect().contains(pt.toPoint()) && !cardRect().contains(pt)) emit rejected();
}

}  // namespace Omarchy
