#include "Omarchy/Toggle.h"

#include <QKeyEvent>
#include <QPainter>
#include <cmath>

#include "Omarchy/Paint.h"

namespace Omarchy {

static bool isActivateKey(const QKeyEvent *e) {
  return e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space;
}

// ---------------------------------------------------------------------------------- ToggleSwitch

ToggleSwitch::ToggleSwitch(QWidget *parent) : Control(parent) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_slide = new QVariantAnimation(this);
  m_slide->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    m_knob = v.toReal();
    update();
    if (parentWidget()) parentWidget()->update();
  });
}

void ToggleSwitch::setChecked(bool on) {
  if (m_checked == on) return;
  m_checked = on;
  const int ms = shown().duration(120);
  m_slide->stop();
  if (ms <= 0 || !isVisible()) {
    m_knob = on ? 1 : 0;
    update();
  } else {
    m_slide->setDuration(ms);
    m_slide->setStartValue(m_knob);
    m_slide->setEndValue(on ? 1.0 : 0.0);
    m_slide->start();
  }
  emit toggled(on);
}

void ToggleSwitch::setInteractive(bool on) {
  m_interactive = on;
  setAttribute(Qt::WA_TransparentForMouseEvents, !on);
  setFocusPolicy(on ? Qt::StrongFocus : Qt::NoFocus);
  updateGeometry();
  update();
}

void ToggleSwitch::setTrackHeight(int px) {
  m_trackHeight = px;
  updateGeometry();
}

int ToggleSwitch::trackHeight() const {
  return m_trackHeight > 0 ? m_trackHeight : qMax(22, int(std::lround(t().spacing("controlHeight") * 0.55)));
}

// The cursor ring sits space(6) outside the track; it follows `interactive`.
int ToggleSwitch::pad() const { return m_interactive ? t().space(6) : 0; }

QSize ToggleSwitch::sizeHint() const {
  const int th = trackHeight();
  const int tw = int(std::lround(th * 1.9));
  return QSize(tw + pad() * 2, th + pad() * 2);
}

QRectF ToggleSwitch::trackRect() const {
  const int th = trackHeight();
  const int tw = int(std::lround(th * 1.9));
  return QRectF(Paint::centered(width(), tw), Paint::centered(height(), th), tw, th);
}

void ToggleSwitch::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const bool rounded = tk.radius() > 0;
  if (m_interactive && (isHot() || hasFocus())) Paint::surface(p, QRectF(rect()), tk.radius(), Qt::transparent, tk.border(hasFocus() ? State::Focus : State::Hover));
  const QRectF track = trackRect();
  const State st = m_checked ? State::Selected : State::Normal;
  Paint::surface(p, track, rounded ? track.height() / 2 : 0, tk.fill(st), tk.border(st));
  const int th = trackHeight();
  const int knob = qMax(6, int(std::lround(th * 0.72)));
  const int inset = qMax(1, int(std::lround((th - knob) / 2.0)));
  const qreal off = inset, on = track.width() - knob - inset;
  const bool mirrored = layoutDirection() == Qt::RightToLeft;
  const qreal k = mirrored ? 1 - m_knob : m_knob;
  const QRectF kr(track.x() + off + (on - off) * k, track.y() + Paint::centered(th, knob), knob, knob);
  const QColor knobColor = m_checked ? tk.selectedText() : tk.textDim();
  Paint::surface(p, kr, rounded ? knob / 2.0 : 0, knobColor);
}

void ToggleSwitch::keyPressEvent(QKeyEvent *e) {
  if (m_interactive && isActivateKey(e) && !e->isAutoRepeat()) {
    activate(Qt::LeftButton);
    e->accept();
    return;
  }
  Control::keyPressEvent(e);
}

void ToggleSwitch::activate(Qt::MouseButton b) {
  if (b != Qt::LeftButton || m_busy) return;
  emit clicked();
  if (m_auto) setChecked(!m_checked);
}

// ---------------------------------------------------------------------------------------- Toggle

Toggle::Toggle(const QString &label, const QString &description, QWidget *parent)
    : Control(parent), m_label(label), m_description(description) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::StrongFocus);
  QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Fixed);
  sp.setHeightForWidth(true);
  setSizePolicy(sp);
  m_switch = new ToggleSwitch(this);
  m_switch->setInteractive(false);
  m_switch->setAutoToggle(false);
  connect(m_switch, &ToggleSwitch::toggled, this, &Toggle::toggled);
}

void Toggle::setLabel(const QString &t) {
  m_label = t;
  updateGeometry();
  update();
}
void Toggle::setDescription(const QString &t) {
  m_description = t;
  updateGeometry();
  place();
  update();
}
bool Toggle::isChecked() const { return m_switch->isChecked(); }
void Toggle::setChecked(bool on) { m_switch->setChecked(on); }

static Border toggleBorder(const Tokens &tk, bool focus, bool hot) {
  return tk.border(focus ? State::Focus : hot ? State::Hover : State::Normal);
}

// The words' column: title, then the description xs under it, wrapped.
qreal Toggle::contentHeight(int width) const {
  const Tokens &tk = t();
  const Border b = toggleBorder(tk, hasFocus(), isHot());
  const int padX = tk.spacing("rowPaddingX");
  const qreal rowW = width - b.left - b.right - 2 * padX;
  const qreal colW = rowW - m_switch->sizeHint().width() - padX;
  qreal h = Paint::lineHeight(tk.font("subtitle", true));
  if (!m_description.isEmpty()) h += tk.spacing("xs") + Paint::wrappedHeight(tk.font("caption"), m_description, qMax<qreal>(1, colW));
  return h;
}

int Toggle::heightForWidth(int w) const {
  const qreal rowH = qMax<qreal>(contentHeight(w), m_switch->sizeHint().height());
  return int(std::ceil(qMax<qreal>(54, rowH + t().spacing("huge")) - 1e-6));
}

QSize Toggle::sizeHint() const {
  const int w = t().space(240);
  return QSize(w, heightForWidth(w));
}

void Toggle::place() {
  const Tokens &tk = t();
  const Border b = toggleBorder(tk, hasFocus(), isHot());
  const int padX = tk.spacing("rowPaddingX");
  const QSize s = m_switch->sizeHint();
  const qreal rowW = width() - b.left - b.right - 2 * padX;
  const qreal colH = contentHeight(width());
  const qreal rowH = qMax<qreal>(colH, s.height());
  const qreal rowY = Paint::centered(height(), rowH);
  const qreal x = b.left + padX + rowW - s.width();
  m_switch->setGeometry(int(std::lround(x)), int(std::lround(rowY + Paint::centered(rowH, s.height()))), s.width(), s.height());
}

void Toggle::resizeEvent(QResizeEvent *) { place(); }

void Toggle::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const bool focus = hasFocus(), hot = isHot();
  const Border b = toggleBorder(tk, focus, hot);
  const QColor fill = tk.fill(focus ? State::Focus : hot ? State::Hover : State::Normal);
  Paint::surface(p, QRectF(rect()), tk.radius(), fill, b);

  const int padX = tk.spacing("rowPaddingX");
  const QSize s = m_switch->sizeHint();
  const qreal rowW = width() - b.left - b.right - 2 * padX;
  const qreal colW = rowW - s.width() - padX;
  const qreal colH = contentHeight(width());
  const qreal rowH = qMax<qreal>(colH, s.height());
  const qreal rowY = Paint::centered(height(), rowH);
  const qreal y = rowY + Paint::centered(rowH, colH);
  const qreal x = b.left + padX;
  const QFont title = tk.font("subtitle", true);
  const qreal th = Paint::lineHeight(title);
  Paint::textIn(p, QRectF(x, y, colW, th), Qt::AlignLeft | Qt::AlignTop, title, tk.foreground(), m_label);
  if (!m_description.isEmpty())
    Paint::wrapped(p, QRectF(x, y + th + tk.spacing("xs"), colW, 1e6), tk.font("caption"), tk.textDim(), m_description);
}

void Toggle::keyPressEvent(QKeyEvent *e) {
  if (isActivateKey(e) && !e->isAutoRepeat()) {
    activate(Qt::LeftButton);
    e->accept();
    return;
  }
  Control::keyPressEvent(e);
}

void Toggle::activate(Qt::MouseButton b) {
  if (b != Qt::LeftButton) return;
  emit clicked();
  if (m_auto) setChecked(!isChecked());
}

}  // namespace Omarchy
