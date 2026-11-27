#include "Omarchy/Forms.h"

#include <QKeyEvent>
#include <QPainter>
#include <cmath>

#include "Omarchy/Paint.h"

namespace Omarchy {

static int ceilPx(qreal v) { return int(std::ceil(v - 1e-6)); }

static bool isActivateKey(const QKeyEvent *e) {
  return e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space;
}

QList<Choice> Choice::fromStrings(const QStringList &values) {
  QList<Choice> out;
  for (const QString &v : values) out << Choice{v, v, QString()};
  return out;
}

// ------------------------------------------------------------------------------------ SettingRow

SettingRow::SettingRow(const QString &label, const QString &description, QWidget *parent)
    : Control(parent), m_label(label), m_description(description) {
  QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Fixed);
  sp.setHeightForWidth(true);
  setSizePolicy(sp);
  m_trailing = new QWidget(this);
  m_row = new Row(m_trailing);
  m_row->setSpacing(shown().spacing("md"));
}

void SettingRow::setLabel(const QString &t) {
  m_label = t;
  update();
}

void SettingRow::setDescription(const QString &t) {
  m_description = t;
  updateGeometry();
  place();
  update();
}

void SettingRow::addControl(QWidget *w) {
  w->setParent(m_trailing);
  m_row->addWidget(w);
  w->show();
  updateGeometry();
  place();
}

// The words' column: the label, then the description xxs under it, wrapped beside the control.
qreal SettingRow::wordsHeight(int width) const {
  const Tokens &tk = t();
  const int padX = tk.spacing("rowPaddingX");
  const qreal w = qMax<qreal>(0, width - padX * 2 - m_row->sizeHint().width() - tk.spacing("xxl"));
  qreal h = Paint::lineHeight(tk.font("body"));
  if (!m_description.isEmpty()) h += tk.spacing("xxs") + Paint::wrappedHeight(tk.font("caption"), m_description, qMax<qreal>(1, w));
  return h;
}

int SettingRow::heightForWidth(int w) const {
  const Tokens &tk = t();
  const int padY = tk.spacing("md");
  const qreal h = qMax<qreal>(qMax<qreal>(tk.spacing("controlHeight") + padY * 2, wordsHeight(w) + padY * 2),
                              m_row->sizeHint().height() + tk.spacing("xxs") * 2);
  return ceilPx(h);
}

QSize SettingRow::sizeHint() const {
  const int w = t().space(240);
  return QSize(w, heightForWidth(w));
}

void SettingRow::place() {
  const Tokens &tk = t();
  m_row->setSpacing(tk.spacing("md"));
  const QSize s = m_row->sizeHint();
  const int padX = tk.spacing("rowPaddingX");
  m_trailing->setGeometry(width() - padX - s.width(), int(Paint::centered(height(), s.height())), s.width(), s.height());
}

void SettingRow::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  if (hasCursor()) Paint::surface(p, QRectF(rect()), tk.radius(), tk.fill(State::Hover));
  const int padX = tk.spacing("rowPaddingX");
  const qreal w = qMax<qreal>(0, width() - padX * 2 - m_row->sizeHint().width() - tk.spacing("xxl"));
  const qreal wh = wordsHeight(width());
  const qreal y = Paint::centered(height(), wh);
  const QFont body = tk.font("body");
  const qreal lh = Paint::lineHeight(body);
  Paint::textIn(p, QRectF(padX, y, w, lh), Qt::AlignLeft | Qt::AlignTop, body, isEnabled() ? tk.foreground() : tk.textFaint(), m_label);
  if (!m_description.isEmpty())
    Paint::wrapped(p, QRectF(padX, y + lh + tk.spacing("xxs"), w, 1e6), tk.font("caption"), isEnabled() ? tk.textDim() : tk.textFaint(), m_description);
}

// -------------------------------------------------------------------------------------- Checkbox

Checkbox::Checkbox(const QString &label, const QString &description, QWidget *parent)
    : Control(parent), m_label(label), m_description(description) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::TabFocus);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void Checkbox::setLabel(const QString &t) {
  m_label = t;
  updateGeometry();
  update();
}

void Checkbox::setDescription(const QString &t) {
  m_description = t;
  updateGeometry();
  update();
}

void Checkbox::setChecked(bool on) {
  if (m_checked == on) return;
  m_checked = on;
  update();
  emit toggled(on);
}

void Checkbox::setExclusive(bool on) {
  m_exclusive = on;
  update();
}

// The box and the label line center on each other: round((line − box) / 2), whichever is taller.
static int boxSize(const Tokens &tk) { return tk.space(16); }
static int boxY(const Tokens &tk) { return qMax(0, int(std::floor((Paint::lineHeight(tk.font("body")) - boxSize(tk)) / 2.0 + 0.5))); }
static int wordsY(const Tokens &tk) { return qMax(0, int(std::floor((boxSize(tk) - Paint::lineHeight(tk.font("body"))) / 2.0 + 0.5))); }

qreal Checkbox::wordsHeight(qreal width) const {
  const Tokens &tk = t();
  qreal h = Paint::lineHeight(tk.font("body"));
  if (!m_description.isEmpty()) h += tk.spacing("xxs") + Paint::wrappedHeight(tk.font("caption"), m_description, qMax<qreal>(1, width));
  return h;
}

QSizeF Checkbox::implicitSize() const {
  const Tokens &tk = t();
  const int box = boxSize(tk);
  const qreal words = qMax(Paint::textWidth(tk.font("body"), m_label), Paint::textWidth(tk.font("caption"), m_description));
  const qreal w = box + tk.spacing("rowGap") + words;
  const qreal width = paintRect().width() > 0 && isVisible() ? paintRect().width() : w;
  return QSizeF(w, qMax<qreal>(boxY(tk) + box, wordsY(tk) + wordsHeight(width - box - tk.spacing("rowGap"))));
}

QSize Checkbox::sizeHint() const {
  const QSizeF s = implicitSize();
  return QSize(ceilPx(s.width()), ceilPx(s.height()));
}

QRectF Checkbox::boxRect() const {
  const Tokens &tk = t();
  const QRectF pr = paintRect();
  return QRectF(pr.x(), pr.y() + boxY(tk), boxSize(tk), boxSize(tk));
}

void Checkbox::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const bool enabled = isEnabled();
  const bool hot = enabled && (hasCursor() || isHovered() || hasFocus());
  const State st = hot ? State::Hover : m_checked ? State::Selected : State::Normal;
  const QRectF box = boxRect();
  const int size = boxSize(tk);

  p.save();
  if (!enabled) p.setOpacity(0.5);
  Paint::surface(p, box, m_exclusive ? size / 2.0 : tk.smallRadius(), tk.fill(st), tk.border(st));
  if (m_checked && !m_exclusive) {
    const QFont f = tk.fontPx(std::lround(size * 0.85), true);
    const QString check = QStringLiteral("✓");
    const QSizeF s = Paint::textSize(f, check);
    Paint::text(p, QPointF(box.x() + Paint::centered(size, s.width()), box.y() + Paint::centered(size, s.height())), f, tk.accent(), check);
  } else if (m_checked) {
    const int dot = int(std::lround(size / 2.0));
    const QRectF d(box.x() + Paint::centered(size, dot), box.y() + Paint::centered(size, dot), dot, dot);
    Paint::surface(p, d, dot / 2.0, tk.accent());
  }
  p.restore();

  const QRectF pr = paintRect();
  const qreal x = pr.x() + size + tk.spacing("rowGap");
  const qreal w = qMax<qreal>(0, pr.right() - x);
  const qreal y = pr.y() + wordsY(tk);
  const QFont body = tk.font("body");
  const qreal lh = Paint::lineHeight(body);
  Paint::textIn(p, QRectF(x, y, w, lh), Qt::AlignLeft | Qt::AlignTop, body, enabled ? tk.foreground() : tk.textFaint(), m_label);
  if (!m_description.isEmpty())
    Paint::wrapped(p, QRectF(x, y + lh + tk.spacing("xxs"), w, 1e6), tk.font("caption"), enabled ? tk.textDim() : tk.textFaint(), m_description);
}

void Checkbox::keyPressEvent(QKeyEvent *e) {
  if (isActivateKey(e)) {
    activate(Qt::LeftButton);
    e->accept();
    return;
  }
  Control::keyPressEvent(e);
}

void Checkbox::activate(Qt::MouseButton b) {
  if (b != Qt::LeftButton || !isEnabled()) return;
  emit clicked();
  if (m_auto) setChecked(m_exclusive ? true : !m_checked);
}

void Checkbox::changeEvent(QEvent *e) {
  Control::changeEvent(e);
  if (e->type() == QEvent::EnabledChange && focusPolicy() != Qt::NoFocus) setFocusPolicy(isEnabled() ? Qt::TabFocus : Qt::NoFocus);
}

Radio::Radio(const QString &label, const QString &description, QWidget *parent) : Checkbox(label, description, parent) { setExclusive(true); }

// ------------------------------------------------------------------------------------ RadioGroup

RadioGroup::RadioGroup(QWidget *parent) : Control(parent) {
  setFocusPolicy(Qt::TabFocus);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void RadioGroup::setOptions(const QList<Choice> &options) {
  m_options = options;
  rebuild();
}

void RadioGroup::setValue(const QString &v) {
  m_value = v;
  sync();
}

void RadioGroup::setCursorIndex(int i) {
  m_cursor = i;
  sync();
}

int RadioGroup::indexOfValue(const QString &v) const {
  for (int i = 0; i < m_options.size(); ++i)
    if (m_options[i].value == v) return i;
  return -1;
}

void RadioGroup::choose(int index) {
  if (index < 0 || index >= m_options.size()) return;
  const QString v = m_options[index].value;
  if (v == m_value) return;
  m_value = v;
  sync();
  emit changed(v);
}

void RadioGroup::rebuild() {
  qDeleteAll(m_radios);
  m_radios.clear();
  for (int i = 0; i < m_options.size(); ++i) {
    auto *r = new Radio(m_options[i].text(), m_options[i].description, this);
    r->setAutoToggle(false);
    r->setFocusPolicy(Qt::NoFocus);
    connect(r, &Checkbox::clicked, this, [this, i] { choose(i); });
    m_radios << r;
    r->show();
  }
  sync();
  updateGeometry();
  place();
}

void RadioGroup::sync() {
  for (int i = 0; i < m_radios.size(); ++i) {
    const bool checked = m_options[i].value == m_value;
    m_radios[i]->setChecked(checked);
    m_radios[i]->setHasCursor(m_cursor == i || (hasFocus() && checked));
  }
}

// A Column positioner: rows lg apart at their exact heights, as wide as the group.
void RadioGroup::place() {
  const qreal gap = t().spacing("lg");
  qreal y = 0;
  for (Radio *r : m_radios) {
    const qreal h = r->implicitSize().height();
    r->setGeometryF(QRectF(0, y, width(), h));
    y += h + gap;
  }
}

QSize RadioGroup::sizeHint() const {
  qreal w = 0, h = 0;
  for (Radio *r : m_radios) {
    const QSizeF s = r->implicitSize();
    w = qMax(w, s.width());
    h += s.height();
  }
  if (m_radios.size() > 1) h += (m_radios.size() - 1) * t().spacing("lg");
  return QSize(ceilPx(w), ceilPx(h));
}

void RadioGroup::keyPressEvent(QKeyEvent *e) {
  const int i = indexOfValue(m_value);
  const int n = int(m_options.size());
  if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Right || e->text() == QLatin1String("j")) choose(qMin(n - 1, i + 1));
  else if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Left || e->text() == QLatin1String("k")) choose(qMax(0, i - 1));
  else return Control::keyPressEvent(e);
  e->accept();
}

void RadioGroup::focusInEvent(QFocusEvent *e) {
  Control::focusInEvent(e);
  sync();
}

void RadioGroup::focusOutEvent(QFocusEvent *e) {
  Control::focusOutEvent(e);
  sync();
}

}  // namespace Omarchy
