#include "Omarchy/Surfaces.h"

#include <QKeyEvent>
#include <QPainter>
#include <cmath>

#include "Omarchy/Paint.h"

namespace Omarchy {

static int ceilPx(qreal v) { return int(std::ceil(v - 1e-6)); }

// --------------------------------------------------------------------------------------- Surface

Surface::Surface(Look look, QWidget *parent) : QWidget(parent), m_look(std::move(look)) { follow(this); }

void Surface::setLook(Look look) {
  m_look = std::move(look);
  update();
}

void Surface::paintEvent(QPaintEvent *) {
  if (!m_look) return;
  QColor fill = Qt::transparent;
  Border border;
  qreal radius = shown().radius();
  m_look(shown(), fill, border, radius);
  QPainter p(this);
  Paint::surface(p, QRectF(rect()), radius, fill, border);
}

// --------------------------------------------------------------------------------- SectionHeader

SectionHeader::SectionHeader(const QString &text, QWidget *parent) : QWidget(parent), m_text(text) {
  follow(this);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SectionHeader::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  update();
}

QSize SectionHeader::sizeHint() const {
  const Tokens &tk = shown();
  const int size = tk.fontSize("caption");
  const QSizeF s = Paint::textSize(tk.fontPx(size, true), m_text);
  // Glyphs paint above the line box (JetBrainsMono's outlines run 10% of the em past its
  // ascent): the header reserves that overshoot above itself.
  return QSize(ceilPx(s.width()), ceilPx(std::ceil(size * 0.15) + s.height()));
}

void SectionHeader::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  const int size = tk.fontSize("caption");
  QPainter p(this);
  const QFont f = tk.fontPx(size, true);
  Paint::text(p, QPointF(0, std::ceil(size * 0.15)), f, tk.textDim(), Paint::elided(f, m_text, width()));
}

// ------------------------------------------------------------------------------------- Separator

Separator::Separator(QWidget *parent) : QWidget(parent) {
  follow(this);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Separator::setStrength(qreal s) {
  m_strength = s;
  update();
}

void Separator::setVertical(bool on) {
  m_vertical = on;
  setSizePolicy(on ? QSizePolicy::Fixed : QSizePolicy::Expanding, on ? QSizePolicy::Expanding : QSizePolicy::Fixed);
  updateGeometry();
}

QSize Separator::sizeHint() const { return m_vertical ? QSize(1, 100) : QSize(100, 1); }

void Separator::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.fillRect(m_vertical ? QRect(0, 0, 1, height()) : QRect(0, 0, width(), 1), Paint::alpha(shown().foreground(), m_strength));
}

// ----------------------------------------------------------------------------------------- Label

Label::Label(const QString &text, const char *font, Role role, QWidget *parent) : QWidget(parent), m_text(text), m_font(font), m_role(role) {
  follow(this);
}

void Label::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  update();
}
void Label::setBold(bool on) {
  m_bold = on;
  updateGeometry();
  update();
}
void Label::setRole(Role r) {
  m_role = r;
  m_color = QColor();
  update();
}
void Label::setColor(const QColor &c) {
  m_color = c;
  update();
}
void Label::setFontToken(const char *token) {
  m_font = token;
  updateGeometry();
  update();
}
void Label::setAlignment(Qt::Alignment a) {
  m_align = a;
  update();
}
void Label::setWordWrap(bool on) {
  m_wrap = on;
  QSizePolicy sp = sizePolicy();
  sp.setHeightForWidth(on);
  setSizePolicy(sp);
  updateGeometry();
}

QFont Label::font() const { return shown().font(m_font.constData(), m_bold); }

QSize Label::sizeHint() const {
  const QSizeF s = Paint::textSize(font(), m_text);
  return QSize(ceilPx(s.width()), ceilPx(s.height()));
}

int Label::heightForWidth(int w) const { return ceilPx(Paint::wrappedHeight(font(), m_text, w)); }

void Label::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QColor c = m_color;
  if (!c.isValid()) {
    switch (m_role) {
    case Foreground: c = tk.foreground(); break;
    case Dim: c = tk.textDim(); break;
    case Faint: c = tk.textFaint(); break;
    case Accent: c = tk.accent(); break;
    case Urgent: c = tk.urgent(); break;
    case Success: c = tk.success(); break;
    case Warning: c = tk.warning(); break;
    }
  }
  QPainter p(this);
  if (m_wrap) Paint::wrapped(p, QRectF(rect()), font(), c, m_text, m_align);
  else Paint::textIn(p, QRectF(rect()), int(m_align), font(), c, m_text);
}

// --------------------------------------------------------------------------------------- Heading

Heading::Heading(const QString &text, const QString &subtitle, QWidget *parent) : QWidget(parent), m_text(text), m_subtitle(subtitle) {
  follow(this);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  connect(Theme::instance(), &Theme::metricsChanged, this, [this] {
    if (m_trailingRow) m_trailingRow->setSpacing(shown().spacing("xxs"));
    place();
  });
}

void Heading::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  update();
}
void Heading::setSubtitle(const QString &t) {
  m_subtitle = t;
  updateGeometry();
  update();
}
void Heading::setLevel(const QString &level) {
  m_level = level;
  updateGeometry();
  update();
}
void Heading::setCentered(bool on) {
  m_centered = on;
  update();
}

void Heading::addTrailing(QWidget *w) {
  if (!m_trailing) {
    m_trailing = new QWidget(this);
    m_trailingRow = new Row(m_trailing);
    m_trailingRow->setSpacing(shown().spacing("xxs"));
  }
  w->setParent(m_trailing);
  m_trailingRow->addWidget(w);
  w->show();
  m_trailing->show();
  updateGeometry();
  place();
}

int Heading::titleSize() const {
  const Tokens &tk = shown();
  if (m_level == QLatin1String("display")) return tk.fontSize("display");
  if (m_level == QLatin1String("heading")) return tk.fontSize("heading");
  return tk.fontSize("title");
}

int Heading::subtitleSize() const {
  const Tokens &tk = shown();
  if (m_level == QLatin1String("display")) return tk.fontSize("subtitle");
  if (m_level == QLatin1String("heading")) return tk.fontSize("body");
  return tk.fontSize("bodySmall");
}

qreal Heading::labelsHeight() const {
  const Tokens &tk = shown();
  qreal h = Paint::lineHeight(tk.fontPx(titleSize(), true));
  if (!m_subtitle.isEmpty()) h += tk.spacing("xxs") + Paint::lineHeight(tk.fontPx(subtitleSize()));
  return h;
}

int Heading::trailingInset() const {
  if (!m_trailing) return 0;
  const int w = m_trailingRow->sizeHint().width();
  return w > 0 ? w + shown().spacing("controlGap") : 0;
}

QSize Heading::sizeHint() const {
  const Tokens &tk = shown();
  qreal w = Paint::textWidth(tk.fontPx(titleSize(), true), m_text);
  if (!m_subtitle.isEmpty()) w = qMax(w, Paint::textWidth(tk.fontPx(subtitleSize()), m_subtitle));
  const int th = m_trailing ? m_trailingRow->sizeHint().height() : 0;
  return QSize(ceilPx(w) + trailingInset(), ceilPx(qMax<qreal>(labelsHeight(), th)));
}

void Heading::place() {
  if (!m_trailing) return;
  const QSize s = m_trailingRow->sizeHint();
  m_trailing->setGeometry(width() - s.width(), int(std::lround((height() - s.height()) / 2.0)), s.width(), s.height());
}

void Heading::resizeEvent(QResizeEvent *) { place(); }

void Heading::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  const qreal w = qMax(0, width() - trailingInset());
  const qreal y = std::lround((height() - labelsHeight()) / 2);
  const QFont title = tk.fontPx(titleSize(), true);
  const int align = (m_centered ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignTop;
  const qreal th = Paint::lineHeight(title);
  Paint::textIn(p, QRectF(0, y, w, th), align, title, tk.foreground(), m_text);
  if (!m_subtitle.isEmpty()) {
    const QFont sub = tk.fontPx(subtitleSize());
    Paint::textIn(p, QRectF(0, y + th + tk.spacing("xxs"), w, Paint::lineHeight(sub)), align, sub, tk.textDim(), m_subtitle);
  }
}

// ------------------------------------------------------------------------------------------ Card

Card::Card(const QString &title, QWidget *parent) : QWidget(parent) {
  follow(this);
  m_column = new Column(this);
  m_header = new SectionHeader(title, this);
  m_header->setVisible(!title.isEmpty());
  m_column->addWidget(m_header);
  auto *bodyWidget = new QWidget(this);
  setFill(bodyWidget);
  m_body = new Column(bodyWidget);
  m_column->addWidget(bodyWidget);
  relayout();
  connect(Theme::instance(), &Theme::metricsChanged, this, &Card::relayout);
}

Border Card::border() const { return shown().border(QStringLiteral("surfaces.popups.border")); }

void Card::relayout() {
  const Tokens &tk = shown();
  const int pad = tk.spacing("panelPadding");
  const Border b = border();
  m_column->setContentsMargins(b.left + pad, b.top + pad, b.right + pad, b.bottom + pad);
  m_column->setSpacing(tk.spacing("panelGap"));
  m_body->setSpacing(tk.spacing("panelGap"));
}

void Card::setTitle(const QString &t) {
  m_header->setText(t);
  m_header->setVisible(!t.isEmpty());
}

void Card::addWidget(QWidget *w) { m_body->addWidget(w); }

QSize Card::sizeHint() const { return m_column->sizeHint(); }

void Card::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  Paint::surface(p, QRectF(rect()), tk.radius(), tk.color(QStringLiteral("surfaces.popups.background")), border());
}

// ------------------------------------------------------------------------------------------ Pane

Pane::Pane(const QString &title, const QString &subtitle, QWidget *parent) : QWidget(parent) {
  follow(this);
  m_column = new Column(this);
  m_heading = new Heading(title, subtitle, this);
  setFill(m_heading);
  m_heading->setVisible(!title.isEmpty());
  m_column->addWidget(m_heading);
  auto *bodyWidget = new QWidget(this);
  setFill(bodyWidget);
  m_body = new Column(bodyWidget);
  m_column->addWidget(bodyWidget);
  relayout();
  connect(Theme::instance(), &Theme::metricsChanged, this, &Pane::relayout);
}

void Pane::relayout() {
  const Tokens &tk = shown();
  const int pad = tk.spacing("panelPadding");
  const int rule = tk.spacing("hairline");
  const Border b = m_bordered ? tk.border(State::Normal) : Border();
  m_column->setContentsMargins(b.left + pad + (m_divider == QLatin1String("left") ? rule : 0),
                               b.top + pad + (m_divider == QLatin1String("top") ? rule : 0),
                               b.right + pad + (m_divider == QLatin1String("right") ? rule : 0),
                               b.bottom + pad + (m_divider == QLatin1String("bottom") ? rule : 0));
  m_column->setSpacing(tk.spacing("panelGap"));
  m_body->setSpacing(tk.spacing("panelGap"));
}

void Pane::setTitle(const QString &t) {
  m_heading->setText(t);
  m_heading->setVisible(!t.isEmpty());
}
void Pane::setSubtitle(const QString &t) { m_heading->setSubtitle(t); }
void Pane::setBordered(bool on) {
  m_bordered = on;
  relayout();
  update();
}
void Pane::setDivider(const QString &side) {
  m_divider = side;
  relayout();
  update();
}
void Pane::addAction(QWidget *w) { m_heading->addTrailing(w); }
void Pane::addWidget(QWidget *w) { m_body->addWidget(w); }

void Pane::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  if (m_bordered) Paint::surface(p, QRectF(rect()), tk.radius(), tk.fill(State::Normal), tk.border(State::Normal));
  if (!m_divider.isEmpty()) {
    const int rule = tk.spacing("hairline");
    QRect r;
    if (m_divider == QLatin1String("left")) r = QRect(0, 0, rule, height());
    else if (m_divider == QLatin1String("right")) r = QRect(width() - rule, 0, rule, height());
    else if (m_divider == QLatin1String("top")) r = QRect(0, 0, width(), rule);
    else r = QRect(0, height() - rule, width(), rule);
    p.fillRect(r, tk.track());
  }
}

// --------------------------------------------------------------------------------------- MenuRow

MenuRow::MenuRow(const QString &text, QWidget *parent) : Control(parent), m_text(text) {
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MenuRow::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  update();
}
void MenuRow::setChecked(bool on) {
  m_checked = on;
  update();
}
void MenuRow::setHighlighted(bool on) {
  m_highlighted = on;
  update();
}

QSize MenuRow::sizeHint() const {
  const Tokens &tk = t();
  const qreal pad = tk.spacing("controlPaddingX");
  const QFont f = tk.font("body");
  return QSize(ceilPx(Paint::textWidth(f, m_text) + pad * 3 + Paint::textWidth(f, QStringLiteral("✓"))), tk.spacing("popupRowHeight"));
}

void MenuRow::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  if (isHovered() || m_highlighted || hasCursor()) Paint::surface(p, QRectF(rect()), tk.radius(), tk.fill(State::Hover));
  const QFont f = tk.font("body");
  const qreal pad = tk.spacing("controlPaddingX");
  const qreal lh = Paint::lineHeight(f);
  const qreal y = Paint::centered(height(), lh);
  Paint::text(p, QPointF(pad, y), f, tk.color(QStringLiteral("surfaces.popups.text")), m_text);
  if (m_checked) {
    const QString check = QStringLiteral("✓");
    Paint::text(p, QPointF(width() - pad - Paint::textWidth(f, check), y), f, tk.accent(), check);
  }
}

void MenuRow::activate(Qt::MouseButton b) {
  if (b == Qt::LeftButton) emit triggered();
}

// ---------------------------------------------------------------------------------- TooltipFrame

TooltipFrame::TooltipFrame(const QString &text, QWidget *parent) : QWidget(parent), m_text(text) {
  follow(this);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void TooltipFrame::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  update();
}

QSize TooltipFrame::sizeHint() const {
  const Tokens &tk = shown();
  const Border b = tk.border(QStringLiteral("surfaces.tooltip.border"));
  const QSizeF s = Paint::textSize(tk.font("bodySmall"), m_text);
  const qreal w = s.width();
  return QSize(ceilPx(w + b.left + b.right + 2 * tk.spacing("controlPaddingX")),
               ceilPx(s.height() + b.top + b.bottom + 2 * tk.spacing("controlPaddingY")));
}

void TooltipFrame::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  const Border b = tk.border(QStringLiteral("surfaces.tooltip.border"));
  Paint::surface(p, QRectF(rect()), tk.radius(), tk.color(QStringLiteral("surfaces.tooltip.background")), b);
  const QPointF at(b.left + tk.spacing("controlPaddingX"), b.top + tk.spacing("controlPaddingY"));
  const QFont f = tk.font("bodySmall");
  Paint::text(p, at, f, tk.color(QStringLiteral("surfaces.tooltip.text")), m_text);
}

// --------------------------------------------------------------------------------- ConfirmDialog

ConfirmDialog::ConfirmDialog(const QString &message, QWidget *parent) : QWidget(parent), m_message(message) {
  follow(this);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  if (parent) {
    parent->installEventFilter(this);
    setGeometry(parent->rect());
  }
}

void ConfirmDialog::setMessage(const QString &m) {
  m_message = m;
  update();
}
void ConfirmDialog::setConfirmText(const QString &t) {
  m_confirm = t;
  update();
}
void ConfirmDialog::setCancelText(const QString &t) {
  m_cancel = t;
  update();
}
void ConfirmDialog::setSelectedIndex(int i) {
  m_selected = i ? 1 : 0;
  update();
}

QString ConfirmDialog::cancelText() const { return m_cancel.isEmpty() ? Theme::instance()->string(QStringLiteral("common.cancel"), QStringLiteral("Cancel")) : m_cancel; }
QString ConfirmDialog::confirmText() const { return m_confirm.isEmpty() ? Theme::instance()->string(QStringLiteral("common.confirm"), QStringLiteral("Confirm")) : m_confirm; }

void ConfirmDialog::open() {
  if (parentWidget()) setGeometry(parentWidget()->rect());
  m_selected = 1;
  show();
  raise();
  setFocus(Qt::OtherFocusReason);
}

bool ConfirmDialog::eventFilter(QObject *o, QEvent *e) {
  if (o == parentWidget() && e->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
  return QWidget::eventFilter(o, e);
}

QRect ConfirmDialog::cardRect() const {
  const Tokens &tk = shown();
  const int bw = tk.border(State::Normal).top;
  const int pad = tk.space(18);
  const qreal w = qMin<qreal>(width() - tk.space(32), tk.space(370));
  const qreal msg = Paint::wrappedHeight(tk.font("title"), m_message, w - 2 * (bw + pad));
  const qreal h = 2 * (bw + pad) + msg + tk.space(20) + tk.space(34);
  return QRectF(std::round(Paint::centered(width(), w)), std::round(Paint::centered(height(), h)), w, h).toAlignedRect();
}

QRect ConfirmDialog::buttonRect(int index) const {
  const Tokens &tk = shown();
  const int bw = tk.border(State::Normal).top;
  const int pad = tk.space(18);
  const QRect card = cardRect();
  const QFont f = tk.font("caption");
  const int minW = tk.space(88), padX = tk.spacing("controlPaddingX");
  const int w0 = qMax(minW, ceilPx(Paint::textWidth(f, cancelText())) + padX * 2);
  const int w1 = qMax(minW, ceilPx(Paint::textWidth(f, confirmText())) + padX * 2);
  const int bh = tk.space(34), gap = tk.space(10);
  const int right = card.right() + 1 - bw - pad;
  const int x = index == 1 ? right - w1 : right - w1 - gap - w0;
  return QRect(x, card.bottom() + 1 - bw - pad - bh, index == 1 ? w1 : w0, bh);
}

void ConfirmDialog::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  p.fillRect(rect(), Paint::alpha(tk.background(), 0.7));
  const QRect card = cardRect();
  const int bw = tk.border(State::Normal).top;
  const int pad = tk.space(18);
  const QColor accent = tk.accent(), fg = tk.foreground(), urgent = tk.urgent();
  Paint::surface(p, QRectF(card), tk.radius(), tk.background(), Border::flat(accent, bw));
  const QRectF inner = QRectF(card).adjusted(bw + pad, bw + pad, -(bw + pad), -(bw + pad));
  Paint::wrapped(p, QRectF(inner.topLeft(), QSizeF(inner.width(), 1e6)), tk.font("title"), fg, m_message);
  const QString labels[2] = {cancelText(), confirmText()};
  for (int i = 0; i < 2; ++i) {
    const bool selected = m_selected == i, destructive = i == 1;
    const QColor fill = selected ? (destructive ? Paint::alpha(urgent, 0.22) : Paint::alpha(fg, 0.08)) : QColor(Qt::transparent);
    const QColor line = destructive ? (selected ? urgent : Paint::alpha(urgent, 0.56)) : (selected ? accent : Paint::alpha(fg, 0.38));
    const QRect r = buttonRect(i);
    Paint::surface(p, QRectF(r), tk.radius(), fill, Border::flat(line, bw));
    const QFont f = tk.font("caption");
    const QSizeF s = Paint::textSize(f, labels[i]);
    const QColor text = destructive ? (selected ? urgent : fg) : (selected ? accent : fg);
    Paint::text(p, QPointF(r.x() + Paint::centered(r.width(), s.width()), r.y() + Paint::centered(r.height(), s.height())), f, text, labels[i]);
  }
}

void ConfirmDialog::keyPressEvent(QKeyEvent *e) {
  const int k = e->key();
  if (k == Qt::Key_Escape) {
    emit canceled();
  } else if (k == Qt::Key_Left || k == Qt::Key_Right || e->text() == QLatin1String("h") || e->text() == QLatin1String("l") ||
             k == Qt::Key_Tab || k == Qt::Key_Backtab) {
    setSelectedIndex(m_selected == 0 ? 1 : 0);
  } else if (k == Qt::Key_Return || k == Qt::Key_Enter) {
    if (m_selected == 0) emit canceled();
    else emit confirmed();
  } else {
    QWidget::keyPressEvent(e);
    return;
  }
  e->accept();
}

bool ConfirmDialog::event(QEvent *e) {
  // Tab moves between the buttons rather than out of the dialog.
  if (e->type() == QEvent::KeyPress) {
    auto *k = static_cast<QKeyEvent *>(e);
    if (k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) {
      keyPressEvent(k);
      return true;
    }
  }
  return QWidget::event(e);
}

void ConfirmDialog::mouseMoveEvent(QMouseEvent *e) {
  for (int i = 0; i < 2; ++i)
    if (buttonRect(i).contains(e->position().toPoint()) && m_selected != i) setSelectedIndex(i);
}

void ConfirmDialog::mouseReleaseEvent(QMouseEvent *e) {
  const QPoint pt = e->position().toPoint();
  if (buttonRect(0).contains(pt)) emit canceled();
  else if (buttonRect(1).contains(pt)) emit confirmed();
  else if (!cardRect().contains(pt)) emit canceled();
}

}  // namespace Omarchy
