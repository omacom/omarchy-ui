#include "Omarchy/Inputs.h"

#include <QApplication>
#include <QIntValidator>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
#include <cmath>

#include "Omarchy/Paint.h"

namespace Omarchy {

static int ceilPx(qreal v) { return int(std::ceil(v - 1e-6)); }

// The input surface: focus > hover-cursor > normal glass and border.
static void inputSurface(QPainter &p, const QRectF &r, bool focus, bool hot) {
  const Tokens &tk = shown();
  const State st = focus ? State::Focus : hot ? State::Hover : State::Normal;
  Paint::surface(p, r, tk.radius(), tk.fill(st), tk.border(st));
}

// The state colors on a line edit's palette: text, placeholder, selection.
static void editPalette(QLineEdit *e) {
  const Tokens &tk = shown();
  QPalette pal = e->palette();
  for (const QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    pal.setColor(g, QPalette::Text, g == QPalette::Disabled ? tk.textFaint() : tk.foreground());
    pal.setColor(g, QPalette::PlaceholderText, tk.textFaint());
    pal.setColor(g, QPalette::Highlight, tk.fill(State::Selection));
    pal.setColor(g, QPalette::HighlightedText, tk.foreground());
    pal.setColor(g, QPalette::Base, Qt::transparent);
    pal.setColor(g, QPalette::Window, Qt::transparent);
  }
  e->setPalette(pal);
}

// ------------------------------------------------------------------------------------- TextField

TextField::TextField(QWidget *parent) : TextField(QString(), parent) {}

TextField::TextField(const QString &text, QWidget *parent) : QLineEdit(text, parent) {
  setFrame(false);
  setAttribute(Qt::WA_Hover);
  setAttribute(Qt::WA_MacShowFocusRect, false);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  retheme();
  connect(Theme::instance(), &Theme::colorsChanged, this, [this] {
    editPalette(this);
    update();
  });
  connect(Theme::instance(), &Theme::metricsChanged, this, &TextField::retheme);
}

void TextField::retheme() {
  const Tokens &tk = shown();
  setFont(tk.font("body"));
  editPalette(this);
  // QLineEdit keeps 2 px left and right of its text on its own; the rest is the kit's padding
  // inside the widest border.
  const Border b = tk.border(State::Normal);
  const int px = m_padX >= 0 ? m_padX : tk.spacing("controlPaddingX");
  const int py = m_padY >= 0 ? m_padY : tk.spacing("inputPaddingY");
  setTextMargins(b.left + px - 2, b.top + py, b.right + px - 2, b.bottom + py);
  updateGeometry();
  update();
}

void TextField::setHasCursor(bool on) {
  m_hasCursor = on;
  update();
}

void TextField::setPadding(int horizontal, int vertical) {
  m_padX = horizontal;
  m_padY = vertical;
  retheme();
}

QSize TextField::sizeHint() const {
  const Tokens &tk = shown();
  const QMargins m = textMargins();
  const QFont f = tk.font("body");
  const qreal w = qMax(Paint::textWidth(f, text()), Paint::textWidth(f, placeholderText()));
  return QSize(ceilPx(w) + m.left() + m.right() + 4, ceilPx(Paint::lineHeight(f)) + m.top() + m.bottom());
}

QSize TextField::minimumSizeHint() const { return QSize(0, sizeHint().height()); }

void TextField::paintEvent(QPaintEvent *e) {
  {
    QPainter p(this);
    inputSurface(p, QRectF(rect()), hasFocus(), hot());
  }
  QLineEdit::paintEvent(e);
}

void TextField::enterEvent(QEnterEvent *e) {
  QLineEdit::enterEvent(e);
  m_hovered = true;
  update();
  emit hovered(true);
}

void TextField::leaveEvent(QEvent *e) {
  QLineEdit::leaveEvent(e);
  m_hovered = false;
  update();
  emit hovered(false);
}

bool TextField::event(QEvent *e) {
  if (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut) update();
  return QLineEdit::event(e);
}

// ----------------------------------------------------------------------------------- NumberField

NumberField::NumberField(const QString &label, QWidget *parent) : Control(parent), m_label(label) {
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_edit = new QLineEdit(this);
  m_edit->setFrame(false);
  m_edit->setAlignment(Qt::AlignCenter);
  m_edit->setValidator(new QIntValidator(m_from, m_to, m_edit));
  m_edit->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
  m_edit->installEventFilter(this);
  setFocusProxy(m_edit);
  editPalette(m_edit);
  m_edit->setFont(shown().font("body"));
  connect(Theme::instance(), &Theme::colorsChanged, m_edit, [this] { editPalette(m_edit); });
  connect(m_edit, &QLineEdit::textEdited, this, [this](const QString &t) {
    bool ok = false;
    const int v = t.toInt(&ok);
    if (ok && v >= m_from && v <= m_to && v != m_value) {
      m_value = v;
      emit modified(v);
    }
  });
  connect(m_edit, &QLineEdit::editingFinished, this, &NumberField::sync);
  sync();
}

void NumberField::setLabel(const QString &t) {
  m_label = t;
  updateGeometry();
  place();
  update();
}

void NumberField::setValue(int v) {
  m_value = qBound(m_from, v, m_to);
  sync();
  update();
}

void NumberField::setRange(int from, int to) {
  m_from = from;
  m_to = qMax(from, to);
  static_cast<QIntValidator *>(const_cast<QValidator *>(m_edit->validator()))->setRange(m_from, m_to);
  setValue(m_value);
}

void NumberField::sync() {
  const QString t = QString::number(m_value);
  if (m_edit->text() != t) m_edit->setText(t);
}

void NumberField::step(int dir) {
  const int v = qBound(m_from, m_value + dir * m_step, m_to);
  if (v == m_value) return;
  m_value = v;
  sync();
  update();
  emit modified(v);
}

qreal NumberField::labelHeight() const { return m_label.isEmpty() ? 0 : Paint::lineHeight(t().font("caption", true)); }

// The spin box under the label: numberFieldWidth × controlHeight.
QRectF NumberField::fieldRect() const {
  const Tokens &tk = t();
  const qreal y = m_label.isEmpty() ? 0 : labelHeight() + tk.spacing("labelGap");
  const int h = qMax(tk.spacing("controlHeight"), tk.fontSize("body") + tk.spacing("controlPaddingY") * 2);
  return QRectF(0, y, tk.spacing("numberFieldWidth"), h);
}

QRectF NumberField::downRect() const {
  const QRectF f = fieldRect();
  const int inset = t().border(State::Normal).top;
  const qreal side = f.height() - inset * 2;
  return QRectF(f.x() + inset, f.y() + inset, side, side);
}

QRectF NumberField::upRect() const {
  const QRectF f = fieldRect();
  const int inset = t().border(State::Normal).top;
  const qreal side = f.height() - inset * 2;
  return QRectF(f.right() - side - inset, f.y() + inset, side, side);
}

QSize NumberField::sizeHint() const {
  const QRectF f = fieldRect();
  const qreal lw = m_label.isEmpty() ? 0 : Paint::textWidth(t().font("caption", true), m_label);
  return QSize(ceilPx(qMax(f.width(), lw)), ceilPx(f.bottom()));
}

void NumberField::place() {
  const Tokens &tk = t();
  const QRectF f = fieldRect(), d = downRect(), u = upRect();
  const int b = tk.border(State::Normal).top;
  // The value is centered in the field; the editor spans the room between the steps with sm clearance.
  m_edit->setFont(tk.font("body"));
  const int smPad = qMax(0, tk.spacing("sm") - 2);
  m_edit->setTextMargins(smPad, 0, smPad, 0);
  m_edit->setGeometry(QRectF(d.right(), f.y() + b, u.left() - d.right(), f.height() - 2 * b).toAlignedRect());
}

void NumberField::resizeEvent(QResizeEvent *) { place(); }

void NumberField::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  if (!m_label.isEmpty()) Paint::text(p, QPointF(0, 0), tk.font("caption", true), tk.textDim(), m_label);
  inputSurface(p, fieldRect(), m_edit->hasFocus(), isHot());
  const QFont f = tk.font("body");
  const int inset = tk.border(State::Normal).top;
  const qreal stepRadius = qMax<qreal>(0, tk.radius() - inset);
  for (int part : {-1, 1}) {
    const QRectF r = part < 0 ? downRect() : upRect();
    const bool enabled = isEnabled() && (part < 0 ? m_value > m_from : m_value < m_to);
    const bool pressed = enabled && m_pressedPart == part;
    const bool hot = enabled && m_hoverPart == part;
    if (pressed || hot) Paint::surface(p, r, stepRadius, pressed ? tk.fill(State::Pressed) : tk.fill(State::Hover));
    const QString g = part < 0 ? QStringLiteral("−") : QStringLiteral("+");
    const QSizeF s = Paint::textSize(f, g);
    p.save();
    if (!enabled) p.setOpacity(0.4);
    Paint::text(p, QPointF(r.x() + Paint::centered(r.width(), s.width()), r.y() + Paint::centered(r.height(), s.height())), f, tk.foreground(), g);
    p.restore();
  }
}

void NumberField::mousePressEvent(QMouseEvent *e) {
  const QPointF pt = e->position();
  m_pressedPart = downRect().contains(pt) ? -1 : upRect().contains(pt) ? 1 : 0;
  m_edit->setFocus(Qt::MouseFocusReason);
  update();
  e->accept();
}

void NumberField::mouseReleaseEvent(QMouseEvent *e) {
  const QPointF pt = e->position();
  const int part = downRect().contains(pt) ? -1 : upRect().contains(pt) ? 1 : 0;
  if (part != 0 && part == m_pressedPart) step(part);
  m_pressedPart = 0;
  update();
  e->accept();
}

void NumberField::mouseMoveEvent(QMouseEvent *e) {
  const QPointF pt = e->position();
  const int part = downRect().contains(pt) ? -1 : upRect().contains(pt) ? 1 : 0;
  if (part != m_hoverPart) {
    m_hoverPart = part;
    update();
  }
}

void NumberField::wheelEvent(QWheelEvent *e) {
  const int dy = e->angleDelta().y();
  if (dy != 0) step(dy > 0 ? 1 : -1);
  e->accept();
}

void NumberField::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
  case Qt::Key_Up: step(1); break;
  case Qt::Key_Down: step(-1); break;
  case Qt::Key_PageUp: step(10); break;
  case Qt::Key_PageDown: step(-10); break;
  default: Control::keyPressEvent(e); return;
  }
  e->accept();
}

bool NumberField::eventFilter(QObject *o, QEvent *e) {
  if (o == m_edit) {
    if (e->type() == QEvent::KeyPress) {
      auto *k = static_cast<QKeyEvent *>(e);
      if (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down || k->key() == Qt::Key_PageUp || k->key() == Qt::Key_PageDown) {
        keyPressEvent(k);
        return true;
      }
    } else if (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut) {
      if (e->type() == QEvent::FocusOut) sync();
      update();
    }
  }
  return Control::eventFilter(o, e);
}

// -------------------------------------------------------------------------------------- Dropdown

// The open list: a popup on the popups surface, rows popupRowHeight tall, labelGap apart.
class DropdownPopup : public QWidget {
public:
  explicit DropdownPopup(Dropdown *owner) : QWidget(owner, Qt::Popup | Qt::FramelessWindowHint), m_owner(owner) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    follow(this);
  }
  int index = -1;

  Border border() const {
    const Tokens &tk = shown();
    return tk.border(QStringLiteral("surfaces.popups.border")).withWidth(tk.border(State::Normal).top);
  }
  QMargins insets() const {
    const Border b = border();
    const int h = shown().spacing("hairline");
    return QMargins(b.left + h, b.top + h, b.right + h, b.bottom + h);
  }
  QRectF rowRect(int i) const {
    const Tokens &tk = shown();
    const QMargins m = insets();
    const int rh = tk.spacing("popupRowHeight"), gap = tk.spacing("labelGap");
    return QRectF(m.left(), m.top() + i * (rh + gap) - m_scroll, width() - m.left() - m.right(), rh);
  }
  QSize sizeFor(int w) const {
    const Tokens &tk = shown();
    const int n = int(m_owner->m_options.size());
    const int rh = tk.spacing("popupRowHeight"), gap = tk.spacing("labelGap");
    const QMargins m = insets();
    const int h = qMin(n * rh + qMax(0, n - 1) * gap, rh * 8 + 7 * gap) + m.top() + m.bottom();
    return QSize(w, h);
  }
  void ensureVisible() {
    const Tokens &tk = shown();
    const int rh = tk.spacing("popupRowHeight"), gap = tk.spacing("labelGap");
    const QMargins m = insets();
    const int view = height() - m.top() - m.bottom();
    const int top = index * (rh + gap);
    if (top < m_scroll) m_scroll = top;
    else if (top + rh > m_scroll + view) m_scroll = top + rh - view;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    const Tokens &tk = shown();
    QPainter p(this);
    Paint::surface(p, QRectF(rect()), tk.radius(), tk.color(QStringLiteral("surfaces.popups.background")), border());
    const QMargins m = insets();
    p.setClipRect(rect().marginsRemoved(m));
    const QFont f = tk.font("body");
    const QColor fg = tk.color(QStringLiteral("surfaces.popups.text"));
    const qreal lh = Paint::lineHeight(f);
    const int pad = tk.spacing("controlPaddingX");
    for (int i = 0; i < m_owner->m_options.size(); ++i) {
      const QRectF r = rowRect(i);
      if (i == index) Paint::surface(p, r, tk.smallRadius(), tk.fill(State::Hover, fg));
      Paint::textIn(p, QRectF(r.x() + pad, r.y() + Paint::centered(r.height(), lh), r.width() - 2 * pad, lh), Qt::AlignLeft | Qt::AlignTop, f, fg,
                    m_owner->m_options[i].label);
    }
  }
  void mouseMoveEvent(QMouseEvent *e) override {
    for (int i = 0; i < m_owner->m_options.size(); ++i)
      if (rowRect(i).contains(e->position()) && index != i) {
        index = i;
        update();
      }
  }
  void mouseReleaseEvent(QMouseEvent *e) override {
    if (!rect().contains(e->position().toPoint())) return;
    for (int i = 0; i < m_owner->m_options.size(); ++i)
      if (rowRect(i).contains(e->position())) m_owner->choose(i);
  }
  void wheelEvent(QWheelEvent *e) override {
    const QMargins m = insets();
    const int content = sizeFor(width()).height();
    const int all = int(m_owner->m_options.size()) * (shown().spacing("popupRowHeight") + shown().spacing("labelGap"));
    Q_UNUSED(content);
    m_scroll = qBound(0, m_scroll - e->angleDelta().y() / 4, qMax(0, all - (height() - m.top() - m.bottom())));
    update();
  }
  void keyPressEvent(QKeyEvent *e) override {
    const int n = int(m_owner->m_options.size());
    if (e->key() == Qt::Key_Escape) close();
    else if (e->key() == Qt::Key_Down || e->text() == QLatin1String("j")) index = qMin(n - 1, index + 1), ensureVisible();
    else if (e->key() == Qt::Key_Up || e->text() == QLatin1String("k")) index = qMax(0, index - 1), ensureVisible();
    else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) m_owner->choose(index);
    else return QWidget::keyPressEvent(e);
    e->accept();
  }
  void hideEvent(QHideEvent *e) override {
    QWidget::hideEvent(e);
    m_owner->update();
  }

private:
  Dropdown *m_owner;
  int m_scroll = 0;
};

Dropdown::Dropdown(const QString &label, QWidget *parent) : Control(parent), m_label(label) {
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void Dropdown::setLabel(const QString &t) {
  m_label = t;
  updateGeometry();
  update();
}
void Dropdown::setShowLabel(bool on) {
  m_showLabel = on;
  updateGeometry();
  update();
}
void Dropdown::setOptions(const QList<Option> &options) {
  m_options = options;
  update();
}
void Dropdown::setOptions(const QStringList &labels) {
  QList<Option> out;
  for (const QString &l : labels) out << Option{l, l};
  setOptions(out);
}
void Dropdown::setValue(const QString &v) {
  m_value = v;
  update();
}

QString Dropdown::currentLabel() const {
  for (const Option &o : m_options)
    if (o.value == m_value) return o.label;
  return m_value;
}

bool Dropdown::isPopupOpen() const { return m_popup && m_popup->isVisible(); }
QWidget *Dropdown::popup() const { return m_popup; }
int Dropdown::popupIndex() const { return m_popup ? m_popup->index : -1; }

qreal Dropdown::labelHeight() const { return Paint::lineHeight(t().font("caption", true)); }

QRectF Dropdown::triggerRect() const {
  const Tokens &tk = t();
  const bool labeled = m_showLabel && !m_label.isEmpty();
  return QRectF(0, labeled ? labelHeight() + tk.spacing("labelGap") : 0, width(), tk.spacing("controlHeight"));
}

QSize Dropdown::sizeHint() const {
  const Tokens &tk = t();
  const bool labeled = m_showLabel && !m_label.isEmpty();
  const qreal h = labeled ? labelHeight() + tk.spacing("labelGap") + tk.spacing("controlHeight") : tk.spacing("controlHeight");
  return QSize(tk.spacing("dropdownWidth"), ceilPx(h));
}

void Dropdown::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  if (m_showLabel && !m_label.isEmpty()) Paint::text(p, QPointF(0, 0), tk.font("caption", true), tk.textDim(), m_label);
  const QRectF r = triggerRect();
  const bool focus = hasFocus();
  const State st = focus ? State::Focus : isHot() ? State::Hover : State::Normal;
  const QColor fg = tk.color(QStringLiteral("surfaces.popups.text"));
  const Border b = tk.border(st, fg);
  Paint::surface(p, r, tk.radius(), tk.fill(st, fg), b);
  const QFont f = tk.font("body");
  const QString chev = QString::fromUcs4(U"\U000F0140", 1);
  const QSizeF cs = Paint::textSize(f, chev);
  const qreal cx = r.right() - b.right - tk.spacing("controlGap") - cs.width();
  Paint::text(p, QPointF(cx, r.y() + Paint::centered(r.height(), cs.height())), f, tk.textDim(), chev);
  const qreal lx = r.x() + b.left + tk.spacing("controlPaddingX");
  const qreal lh = Paint::lineHeight(f);
  Paint::textIn(p, QRectF(lx, r.y() + Paint::centered(r.height(), lh), cx - tk.spacing("md") - lx, lh), Qt::AlignLeft | Qt::AlignTop, f, fg, currentLabel());
}

void Dropdown::open() {
  if (isPopupOpen() || m_options.isEmpty()) return;
  auto *pop = new DropdownPopup(this);
  m_popup = pop;
  int idx = 0;
  for (int i = 0; i < m_options.size(); ++i)
    if (m_options[i].value == m_value) idx = i;
  pop->index = idx;
  const QRectF r = triggerRect();
  pop->resize(pop->sizeFor(int(r.width())));
  pop->move(mapToGlobal(QPoint(0, int(std::lround(r.bottom())) + t().spacing("xxs"))));
  pop->show();
  pop->ensureVisible();
  pop->setFocus();
}

void Dropdown::close() {
  if (m_popup) m_popup->close();
}

void Dropdown::choose(int index) {
  if (index < 0 || index >= m_options.size()) return;
  const QString v = m_options[index].value;
  m_value = v;
  close();
  update();
  emit changed(v);
}

void Dropdown::keyPressEvent(QKeyEvent *e) {
  const int k = e->key();
  if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space || k == Qt::Key_Down || e->text() == QLatin1String("j")) {
    isPopupOpen() ? close() : open();
    e->accept();
    return;
  }
  if (k == Qt::Key_Escape && isPopupOpen()) {
    close();
    e->accept();
    return;
  }
  Control::keyPressEvent(e);
}

void Dropdown::mousePressEvent(QMouseEvent *e) {
  if (!triggerRect().contains(e->position())) {
    e->ignore();
    return;
  }
  Control::mousePressEvent(e);
}

void Dropdown::activate(Qt::MouseButton b) {
  if (b != Qt::LeftButton) return;
  setFocus(Qt::MouseFocusReason);
  isPopupOpen() ? close() : open();
}

}  // namespace Omarchy
