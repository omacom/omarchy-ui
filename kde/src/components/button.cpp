#include "Omarchy/Button.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QTextLayout>
#include <cmath>

#include "Omarchy/Layout.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

static bool isActivateKey(const QKeyEvent *e) {
  return e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space;
}

static QMargins maxMargins(QMargins a, const Border &b) {
  return QMargins(qMax(a.left(), b.left), qMax(a.top(), b.top), qMax(a.right(), b.right), qMax(a.bottom(), b.bottom));
}

// ---------------------------------------------------------------------------------------- Button

Button::Button(const QString &text, QWidget *parent) : Control(parent), m_text(text) {
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_give = new QVariantAnimation(this);
  connect(m_give, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    m_scale = v.toReal();
    update();
  });
}

void Button::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  update();
}
void Button::setIconText(const QString &t) {
  m_icon = t;
  updateGeometry();
  update();
}
void Button::setSelected(bool on) {
  m_selected = on;
  update();
}
void Button::setActive(bool on) {
  m_active = on;
  update();
}
void Button::setBordered(bool on) {
  m_bordered = on;
  updateGeometry();
  update();
}
void Button::setFocusable(bool on) {
  m_focusable = on;
  setFocusPolicy(on ? Qt::StrongFocus : Qt::NoFocus);
  updateGeometry();
}
void Button::setForeground(const QColor &c) {
  m_fg = c;
  update();
}
void Button::setBackground(const QColor &c) {
  m_bg = c;
  update();
}
void Button::setFontToken(const char *token) {
  m_fontToken = token;
  m_fontPx = 0;
  updateGeometry();
}
void Button::setFontPixelSize(int px) {
  m_fontPx = px;
  updateGeometry();
}
void Button::setPadding(int horizontal, int vertical) {
  m_hpad = horizontal;
  m_vpad = vertical;
  updateGeometry();
}
void Button::setLeftAlign(bool on) {
  m_leftAlign = on;
  update();
}

QColor Button::fg() const { return m_fg.isValid() ? m_fg : t().foreground(); }
int Button::fontPx() const { return m_fontPx > 0 ? m_fontPx : t().fontSize(m_fontToken.constData()); }
int Button::hpad() const { return m_hpad >= 0 ? m_hpad : t().spacing("controlPaddingX"); }
int Button::vpad() const { return m_vpad >= 0 ? m_vpad : t().spacing("controlPaddingY"); }

// The widest border any state can paint, so hovering never relayouts.
QMargins Button::reserved() const {
  QMargins m;
  if (m_focusable) m = maxMargins(m, t().border(State::Focus));
  m = maxMargins(m, t().border(State::Hover));
  m = maxMargins(m, t().border(State::Selected));
  if (m_bordered) m = maxMargins(m, t().border(State::Normal));
  return m;
}

QSizeF Button::implicitSize() const {
  const Tokens &tk = t();
  const QFont label = tk.fontPx(fontPx(), m_selected);
  const QFont icon = tk.font("icon");
  qreal w = 0, h = 0;
  if (!m_icon.isEmpty()) {
    const QSizeF s = Paint::textSize(icon, m_icon);
    w += s.width() + (m_text.isEmpty() ? 0 : tk.spacing("controlGap"));
    if (m_text.isEmpty()) h = s.height();
  }
  if (!m_text.isEmpty()) {
    const QSizeF s = Paint::textSize(label, m_text);
    w += s.width();
    h = s.height();
  }
  const QMargins r = reserved();
  return QSizeF(std::ceil(w - 1e-6) + hpad() * 2 + r.left() + r.right(),
                std::ceil(h - 1e-6) + vpad() * 2 + r.top() + r.bottom());
}

QSize Button::sizeHint() const {
  const QSizeF s = implicitSize();
  return QSize(int(std::ceil(s.width() - 1e-6)), int(std::ceil(s.height() - 1e-6)));
}

void Button::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QColor fore = fg();
  const QColor accent = tk.accent();
  const bool focusRing = m_focusable && hasFocus();
  const bool hot = isHot();

  QColor fill = m_bg.isValid() ? m_bg : QColor(Qt::transparent);
  if (isPressed()) fill = tk.fill(State::Pressed, fore);
  else if (focusRing) fill = tk.fill(State::Focus, fore);
  else if (hot) fill = tk.fill(State::Hover, fore);
  else if (m_selected || m_active) fill = tk.fill(State::Selected, fore);

  Border border;
  if (focusRing) border = tk.border(State::Focus, fore);
  else if (hot) border = tk.border(State::Hover, fore);
  else if (m_selected) {
    const Border sel = tk.border(State::Selected, fore);
    border = !sel.isNone() ? sel : (m_bordered ? tk.border(State::Normal, fore) : Border());
  } else if (m_bordered) border = tk.border(State::Normal, fore);

  const QRectF box = paintRect();
  if (m_scale != 1.0) {
    const QPointF c = box.center();
    p.translate(c);
    p.scale(m_scale, m_scale);
    p.translate(-c);
  }
  Paint::surface(p, box, tk.radius(), fill, border);

  const QColor textColor = m_selected ? tk.selectedText(fore) : fore;
  const QFont label = tk.fontPx(fontPx(), m_selected);
  const QFont icon = tk.font("icon");
  const QSizeF is = m_icon.isEmpty() ? QSizeF(0, 0) : Paint::textSize(icon, m_icon);
  const qreal gap = (!m_icon.isEmpty() && !m_text.isEmpty()) ? tk.spacing("controlGap") : 0;
  const QMargins r = reserved();
  const qreal labelRoom = qMax<qreal>(0, box.width() - hpad() - r.left() - r.right() - is.width() - gap);
  const QString shown = Paint::elided(label, m_text, labelRoom);
  const QSizeF ls = m_text.isEmpty() ? QSizeF() : Paint::textSize(label, shown);
  const qreal rowW = is.width() + gap + ls.width();
  // Center both icon and label vertically in the button box so icon+label buttons align with text-only siblings.
  const qreal x = box.x() + (m_leftAlign ? r.left() + hpad() : Paint::centered(box.width(), rowW));
  if (!m_icon.isEmpty()) Paint::text(p, QPointF(x, box.y() + Paint::centered(box.height(), is.height())), icon, textColor, m_icon);
  if (!m_text.isEmpty()) Paint::text(p, QPointF(x + is.width() + gap, box.y() + Paint::centered(box.height(), ls.height())), label, textColor, shown);
}

void Button::keyPressEvent(QKeyEvent *e) {
  if (m_focusable && isActivateKey(e) && !e->isAutoRepeat()) {
    emit clicked();
    e->accept();
    return;
  }
  Control::keyPressEvent(e);
}

void Button::press(bool down) {
  const Tokens &tk = shown();
  m_give->stop();
  const int ms = tk.duration(tk.number(QStringLiteral("motion.press"), 160));
  if (ms <= 0) {
    m_scale = down ? 0.97 : 1.0;
    update();
    return;
  }
  QEasingCurve curve(QEasingCurve::BezierSpline);
  curve.addCubicBezierSegment(QPointF(0.23, 1.0), QPointF(0.32, 1.0), QPointF(1, 1));
  m_give->setEasingCurve(curve);
  m_give->setDuration(ms);
  m_give->setStartValue(m_scale);
  m_give->setEndValue(down ? 0.97 : 1.0);
  m_give->start();
}

void Button::mousePressEvent(QMouseEvent *e) {
  Control::mousePressEvent(e);
  if (isPressed()) press(true);
}

void Button::mouseReleaseEvent(QMouseEvent *e) {
  const bool was = isPressed();
  Control::mouseReleaseEvent(e);
  if (was) press(false);
}

void Button::activate(Qt::MouseButton b) {
  if (b == Qt::RightButton) emit rightClicked();
  else if (b == Qt::LeftButton) emit clicked();
}

// ------------------------------------------------------------------------------------ IconButton

IconButton::IconButton(const QString &glyph, QWidget *parent) : Control(parent), m_icon(glyph) {
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void IconButton::setIconText(const QString &t) {
  m_icon = t;
  update();
}
void IconButton::setBordered(bool on) {
  m_bordered = on;
  update();
}
void IconButton::setFocusable(bool on) {
  m_focusable = on;
  setFocusPolicy(on ? Qt::StrongFocus : Qt::NoFocus);
}
void IconButton::setHoverColor(const QColor &c) {
  m_hover = c;
  update();
}
void IconButton::setForeground(const QColor &c) {
  m_fg = c;
  update();
}
void IconButton::setFontPixelSize(int px) {
  m_fontPx = px;
  updateGeometry();
}

// Whole and odd, so a glyph (usually an odd number of pixels wide) is centered exactly.
int IconButton::side() const {
  const Tokens &tk = t();
  const int font = m_fontPx > 0 ? m_fontPx : tk.fontSize("icon");
  return int(std::lround(qMax<qreal>(tk.space(22), font + tk.spacing("sm") * 2))) | 1;
}

QSize IconButton::sizeHint() const { return QSize(side(), side()); }

void IconButton::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QColor fore = m_fg.isValid() ? m_fg : tk.foreground();
  const QColor hover = m_hover.isValid() ? m_hover : fore;
  const bool focusRing = m_focusable && hasFocus();
  const bool hot = isHot();
  QColor fill = Qt::transparent;
  Border border;
  if (focusRing) {
    fill = tk.fill(State::Focus, hover);
    border = tk.border(State::Focus, hover);
  } else if (hot) {
    fill = tk.fill(State::Hover, hover);
    if (m_bordered) border = tk.border(State::Hover, hover);
  } else if (m_bordered) {
    border = tk.border(State::Normal, fore);
  }
  Paint::surface(p, QRectF(rect()), tk.smallRadius(), fill, border);
  const int fontPx = m_fontPx > 0 ? m_fontPx : tk.fontSize("icon");
  const QFont font = tk.fontPx(fontPx);
  const QColor color = !isEnabled() ? tk.textFaint() : hot ? hover : fore;
  if (!m_icon.isEmpty()) {
    constexpr int kScale = 100;
    const QRectF big = QFontMetricsF(tk.fontPx(qMax(1, fontPx) * kScale)).tightBoundingRect(m_icon);
    const QRectF ink(big.x() / kScale, big.y() / kScale, big.width() / kScale, big.height() / kScale);
    QTextLayout layout(m_icon, font);
    QTextOption opt;
    opt.setUseDesignMetrics(true);
    opt.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(opt);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) line.setPosition(QPointF(0, 0));
    layout.endLayout();
    const qreal lh = line.isValid() ? line.height() : Paint::lineHeight(font);
    qreal x;
    if (ink.width() > 0 && ink.height() > 0) {
      x = std::floor(width() / 2.0 - (ink.x() + ink.width() / 2.0) + 0.5);
    } else {
      const qreal w = line.isValid() ? line.naturalTextWidth() : Paint::textWidth(font, m_icon);
      x = std::floor((width() - w) / 2.0 + 0.5);
    }
    // Center Y on the line box (Qt's tightBoundingRect unions with y=0, shifting glyphs above the baseline like U+F141 upward by 2pt).
    const qreal y = Paint::centered(height(), lh);
    p.setPen(color);
    layout.draw(&p, QPointF(x, y));
  }
}

void IconButton::keyPressEvent(QKeyEvent *e) {
  if (m_focusable && isActivateKey(e) && !e->isAutoRepeat()) {
    emit clicked();
    e->accept();
    return;
  }
  Control::keyPressEvent(e);
}

void IconButton::activate(Qt::MouseButton b) {
  if (b == Qt::LeftButton) emit clicked();
}

// ----------------------------------------------------------------------------------- ButtonGroup

ButtonGroup::ButtonGroup(QWidget *parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  connect(Theme::instance(), &Theme::metricsChanged, this, [this] {
    updateGeometry();
    place();
  });
}

// Qt Quick's Row: each chip at its exact implicit width, md apart, at fractional positions.
void ButtonGroup::place() {
  qreal x = 0;
  const qreal gap = shown().spacing("md");
  for (Button *b : m_buttons) {
    const QSizeF s = b->implicitSize();
    b->setGeometryF(QRectF(x, 0, s.width(), s.height()));
    x += s.width() + gap;
  }
}

void ButtonGroup::setOptions(const QList<Option> &options) {
  m_options = options;
  rebuild();
}

void ButtonGroup::setOptions(const QStringList &labels) {
  QList<Option> out;
  for (const QString &l : labels) out << Option{l, l, QString(), QString()};
  setOptions(out);
}

void ButtonGroup::setValue(const QString &v) {
  m_value = v;
  refresh();
}

void ButtonGroup::setCursorIndex(int i) {
  m_cursor = i;
  refresh();
}

void ButtonGroup::rebuild() {
  qDeleteAll(m_buttons);
  m_buttons.clear();
  for (int i = 0; i < m_options.size(); ++i) {
    const Option &o = m_options[i];
    auto *b = new Button(o.label, this);
    b->setIconText(o.icon);
    b->setTooltipText(o.tooltip);
    b->setBordered(true);
    connect(b, &Button::clicked, this, [this, i] {
      const QString v = m_options.value(i).value;
      if (v != m_value) {
        setValue(v);
      }
      emit changed(v);
    });
    connect(b, &Control::hovered, this, [this, i](bool on) { emit hovered(i, on); });
    m_buttons << b;
    b->show();
  }
  refresh();
  updateGeometry();
}

void ButtonGroup::refresh() {
  for (int i = 0; i < m_buttons.size(); ++i) {
    m_buttons[i]->setSelected(m_options[i].value == m_value);
    m_buttons[i]->setHasCursor(m_cursor == i || (hasFocus() && m_focused == i));
  }
  updateGeometry();
  place();
}

QSize ButtonGroup::sizeHint() const {
  qreal w = 0, h = 0;
  for (Button *b : m_buttons) {
    const QSizeF s = b->implicitSize();
    w += s.width();
    h = qMax(h, s.height());
  }
  if (m_buttons.size() > 1) w += shown().spacing("md") * (m_buttons.size() - 1);
  return QSize(int(std::ceil(w - 1e-6)), int(std::ceil(h - 1e-6)));
}

void ButtonGroup::focusInEvent(QFocusEvent *e) {
  QWidget::focusInEvent(e);
  int idx = -1;
  for (int i = 0; i < m_options.size(); ++i)
    if (m_options[i].value == m_value) idx = i;
  m_focused = idx < 0 ? 0 : idx;
  refresh();
}

void ButtonGroup::focusOutEvent(QFocusEvent *e) {
  QWidget::focusOutEvent(e);
  m_focused = -1;
  refresh();
}

void ButtonGroup::keyPressEvent(QKeyEvent *e) {
  int key = e->key();
  if (key == Qt::Key_H || e->text() == QLatin1String("h")) key = Qt::Key_Left;
  else if (key == Qt::Key_L || e->text() == QLatin1String("l")) key = Qt::Key_Right;
  if (layoutDirection() == Qt::RightToLeft) {
    if (key == Qt::Key_Left) key = Qt::Key_Right;
    else if (key == Qt::Key_Right) key = Qt::Key_Left;
  }
  if (key == Qt::Key_Left) {
    m_focused = qMax(0, (m_focused < 0 ? 0 : m_focused) - 1);
  } else if (key == Qt::Key_Right) {
    m_focused = qMin(int(m_options.size()) - 1, (m_focused < 0 ? 0 : m_focused) + 1);
  } else if (isActivateKey(e)) {
    if (m_focused >= 0 && m_focused < m_options.size()) {
      const QString v = m_options[m_focused].value;
      setValue(v);
      emit changed(v);
    }
  } else {
    QWidget::keyPressEvent(e);
    return;
  }
  e->accept();
  refresh();
}

}  // namespace Omarchy
