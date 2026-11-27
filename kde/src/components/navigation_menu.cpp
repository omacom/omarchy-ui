// Menu, MenuItem, MenuSeparator, MenuSection and ContextMenu, drawn as Menu.qml and its rows.

#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <cmath>

#include "Omarchy/Navigation.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

namespace NavDetail {

// OpticalGlyph (paintedSize 0): the glyph centered horizontally on its ink (its outline's middle,
// measured a hundred times larger so whole-pixel bounds don't shift it), its line box centered
// vertically on whole pixels.
void glyphByInk(QPainter &p, const QRectF &box, const QString &text, qreal fontSize, const QColor &color) {
  if (text.isEmpty()) return;
  const Tokens &tk = shown();
  const int px = qMax(1, int(std::lround(fontSize)));
  Paint::opticalGlyph(p, box, tk.fontPx(px), color, text);
}

int ceilPx(qreal v) { return int(std::ceil(v - 1e-6)); }

}  // namespace NavDetail

using NavDetail::ceilPx;
using NavDetail::glyphByInk;

static const Qt::WindowFlags kPopupFlags = Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;

static bool isActivateKey(int key) { return key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space; }

// ------------------------------------------------------------------------------------- MenuItem

MenuItem::MenuItem(const QString &text, QWidget *parent) : Control(parent), m_text(text) {
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MenuItem::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  update();
}
void MenuItem::setIconText(const QString &t) {
  m_icon = t;
  updateGeometry();
  if (Menu *m = menu()) m->update();
  update();
}
void MenuItem::setShortcut(const QString &s) {
  m_shortcut = s;
  updateGeometry();
  update();
}
void MenuItem::setCheckable(bool on) {
  m_checkable = on;
  updateGeometry();
  update();
}
void MenuItem::setChecked(bool on) {
  if (m_checked == on) return;
  m_checked = on;
  update();
  emit toggled(on);
}
void MenuItem::setExclusive(bool on) {
  m_exclusive = on;
  update();
}
void MenuItem::setDestructive(bool on) {
  m_destructive = on;
  update();
}
void MenuItem::setSubmenu(Menu *m) {
  m_submenu = m;
  if (m && !m->parentWidget()) m->setParent(this, m->windowFlags());
  updateGeometry();
  update();
}
void MenuItem::setHasSubmenu(bool on) {
  m_hasSubmenu = on;
  m_hasSubmenuSet = true;
  updateGeometry();
  update();
}

Menu *MenuItem::menu() const { return qobject_cast<Menu *>(parentWidget()); }
bool MenuItem::isHighlighted() const {
  const Menu *m = menu();
  return m && m->currentItem() == this;
}
bool MenuItem::isHot() const { return isEnabled() && (isHovered() || hasCursor() || isHighlighted()); }
bool MenuItem::reserveIcon() const {
  const Menu *m = menu();
  return m ? m->hasIcons() : !m_icon.isEmpty();
}
bool MenuItem::reserveMark() const {
  const Menu *m = menu();
  return m ? m->hasMarks() : (m_checkable || hasSubmenu());
}

QSizeF MenuItem::implicitSize() const {
  const Tokens &tk = t();
  const qreal pad = tk.spacing("controlPaddingX"), gap = tk.spacing("controlGap"), box = tk.fontSize("icon");
  qreal w = pad * 2 + (reserveIcon() ? box + gap : 0) + ceilPx(Paint::textWidth(tk.font("body"), m_text));
  if (!m_shortcut.isEmpty()) w += tk.spacing("huge") + ceilPx(Paint::textWidth(tk.font("bodySmall"), m_shortcut));
  if (reserveMark()) w += gap + box;
  return QSizeF(w, tk.spacing("popupRowHeight"));
}

QSize MenuItem::sizeHint() const {
  const QSizeF s = implicitSize();
  return QSize(ceilPx(s.width()), ceilPx(s.height()));
}

void MenuItem::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  p.setClipRect(rect());
  const QRectF r = paintRect();
  const bool enabled = isEnabled();
  const bool hot = isHot();
  if (isPressed() && enabled) Paint::surface(p, r, tk.smallRadius(), tk.fill(State::Pressed));
  else if (hot) Paint::surface(p, r, tk.smallRadius(), tk.color(QStringLiteral("surfaces.menu.selectedBackground")));

  const QColor ink = !enabled ? tk.textFaint()
                     : m_destructive ? tk.urgent()
                     : hot ? tk.color(QStringLiteral("surfaces.menu.selectedText"))
                           : tk.color(QStringLiteral("surfaces.menu.text"));
  const qreal pad = tk.spacing("controlPaddingX"), gap = tk.spacing("controlGap");
  const qreal box = tk.fontSize("icon");
  if (!m_icon.isEmpty()) glyphByInk(p, QRectF(r.x() + pad, r.y(), box, r.height()), m_icon, tk.fontSize("icon"), ink);

  const QFont body = tk.font("body");
  const qreal lx = r.x() + pad + (reserveIcon() ? box + gap : 0);
  const qreal labelY = r.y() + Paint::centered(r.height(), Paint::lineHeight(body));
  Paint::text(p, QPointF(lx, labelY), body, ink, m_text);

  if (!m_shortcut.isEmpty()) {
    const QFont small = tk.font("bodySmall");
    const QSizeF s = Paint::textSize(small, m_shortcut);
    const qreal right = pad + (reserveMark() ? box + gap : 0);
    const qreal hintY = labelY + Paint::ascent(body) - Paint::ascent(small);
    Paint::text(p, QPointF(r.right() - right - s.width(), hintY), small,
                enabled ? tk.textDim() : tk.textFaint(), m_shortcut);
  }

  const bool mark = m_checkable && m_checked;
  if (mark && m_exclusive) {
    const QRectF markBox(r.right() - pad - box, r.y(), box, r.height());
    const qreal dot = tk.space(6);
    const QRectF dotRect(markBox.x() + std::floor((box - dot) / 2.0 + 0.5), r.y() + Paint::centered(r.height(), dot), dot, dot);
    Paint::surface(p, dotRect, dot / 2.0, enabled ? tk.accent() : tk.textFaint());
  } else if (mark || hasSubmenu()) {
    const QRectF markBox(r.right() - pad - box, r.y(), box, r.height());
    const QString g = mark ? QStringLiteral("✓") : QString(QChar(0xF054));
    const int size = mark ? tk.fontSize("body") : tk.fontSize("iconSmall");
    glyphByInk(p, markBox, g, size, !enabled ? tk.textFaint() : mark ? tk.accent() : tk.textDim());
  }
}

void MenuItem::enterEvent(QEnterEvent *e) {
  Control::enterEvent(e);
  if (Menu *m = menu(); m && isEnabled()) m->setCurrentIndex(int(m->rows().indexOf(this)));
}

void MenuItem::activate(Qt::MouseButton b) {
  if (b == Qt::LeftButton) activate();
}

void MenuItem::activate() {
  if (!isEnabled()) return;
  Menu *m = menu();
  if (hasSubmenu()) {
    if (m) m->openSubmenu(this);
    return;
  }
  emit triggered();
  if (m) m->itemTriggered(this);
}

// -------------------------------------------------------------------------------- MenuSeparator

MenuSeparator::MenuSeparator(QWidget *parent) : Control(parent) {
  setAttribute(Qt::WA_Hover, false);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSizeF MenuSeparator::implicitSize() const { return QSizeF(0, t().spacing("sm") * 2 + 1); }
QSize MenuSeparator::sizeHint() const { return QSize(0, ceilPx(implicitSize().height())); }

void MenuSeparator::paintEvent(QPaintEvent *) {
  QPainter p(this);
  const QRectF r = paintRect();
  p.fillRect(QRectF(r.x(), r.y() + t().spacing("sm"), r.width(), 1), t().track());
}

// ---------------------------------------------------------------------------------- MenuSection

MenuSection::MenuSection(const QString &text, QWidget *parent) : Control(parent), m_text(text) {
  setAttribute(Qt::WA_Hover, false);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MenuSection::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  update();
}

QSizeF MenuSection::implicitSize() const {
  const Tokens &tk = t();
  const int size = tk.fontSize("caption");
  const QSizeF s = Paint::textSize(tk.fontPx(size, true), m_text);
  return QSizeF(ceilPx(s.width()) + tk.spacing("controlPaddingX") * 2, s.height() + std::ceil(size * 0.15) + tk.spacing("sm") + tk.spacing("xxs"));
}

QSize MenuSection::sizeHint() const {
  const QSizeF s = implicitSize();
  return QSize(ceilPx(s.width()), ceilPx(s.height()));
}

void MenuSection::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF r = paintRect();
  const int size = tk.fontSize("caption");
  Paint::text(p, QPointF(r.x() + tk.spacing("controlPaddingX"), r.y() + tk.spacing("sm") + std::ceil(size * 0.15)), tk.fontPx(size, true), tk.textDim(),
              m_text);
}

// ----------------------------------------------------------------------------------------- Menu

Menu::Menu(QWidget *parent) : QWidget(parent, kPopupFlags) {
  setAttribute(Qt::WA_TranslucentBackground);
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  follow(this);
  connect(Theme::instance(), &Theme::metricsChanged, this, [this] {
    updateGeometry();
    relayout();
    if (!m_held && isVisible()) resize(sizeHint());
  });
}

void Menu::addRow(QWidget *w) {
  w->setParent(this);
  w->show();
  updateGeometry();
  relayout();
}

MenuItem *Menu::addItem(const QString &text, const QString &shortcut, const QString &iconText) {
  auto *item = new MenuItem(text);
  item->setShortcut(shortcut);
  item->setIconText(iconText);
  addRow(item);
  return item;
}
void Menu::addItem(MenuItem *item) { addRow(item); }
MenuSeparator *Menu::addSeparator() {
  auto *s = new MenuSeparator;
  addRow(s);
  return s;
}
MenuSection *Menu::addSection(const QString &text) {
  auto *s = new MenuSection(text);
  addRow(s);
  return s;
}

void Menu::setPopupFlags(bool popup) {
  setWindowFlags(popup ? kPopupFlags : Qt::WindowFlags(Qt::Widget));
  setAttribute(Qt::WA_TranslucentBackground, popup);
  if (!popup) setAttribute(Qt::WA_NoSystemBackground, false);
  // Held, it shows with its parent (a parentless one waits for a layout to adopt it); a popup
  // waits for open().
  if (popup) hide();
  else if (parentWidget()) show();
}

void Menu::setHeld(bool on) {
  if (m_held == on) return;
  m_held = on;
  m_opened = false;
  setPopupFlags(!on);
}

void Menu::setTrigger(QWidget *w) { m_trigger = w; }
int Menu::gap() const { return m_gap >= 0 ? m_gap : shown().spacing("xs"); }
void Menu::setGap(int px) { m_gap = px; }
int Menu::minimumMenuWidth() const { return m_minWidth >= 0 ? m_minWidth : shown().space(160); }
void Menu::setMinimumMenuWidth(int px) {
  m_minWidth = px;
  updateGeometry();
  relayout();
}

void Menu::setCurrentIndex(int i) {
  if (i == m_current) return;
  m_current = i;
  for (QObject *o : children())
    if (auto *w = qobject_cast<QWidget *>(o)) w->update();
  emit currentIndexChanged(i);
}

QList<MenuItem *> Menu::rows() const {
  QList<MenuItem *> out;
  for (QObject *o : children())
    if (auto *it = qobject_cast<MenuItem *>(o); it && !it->isHidden()) out << it;
  return out;
}

MenuItem *Menu::currentItem() const {
  const QList<MenuItem *> r = rows();
  return m_current >= 0 && m_current < r.size() ? r[m_current] : nullptr;
}

bool Menu::hasIcons() const {
  for (MenuItem *it : rows())
    if (!it->iconText().isEmpty()) return true;
  return false;
}

bool Menu::hasMarks() const {
  for (MenuItem *it : rows())
    if (it->isCheckable() || it->hasSubmenu()) return true;
  return false;
}

QMarginsF Menu::insets() const {
  const Tokens &tk = shown();
  const Border b = tk.border(QStringLiteral("surfaces.menu.border"));
  const qreal pad = tk.spacing("sm");
  return QMarginsF(b.left + pad, b.top + pad, b.right + pad, b.bottom + pad);
}

// The rows in order, at their implicit sizes (the separator and section are the other kinds).
static QSizeF rowSize(QWidget *w) {
  if (auto *it = qobject_cast<MenuItem *>(w)) return it->implicitSize();
  if (auto *s = qobject_cast<MenuSeparator *>(w)) return s->implicitSize();
  if (auto *s = qobject_cast<MenuSection *>(w)) return s->implicitSize();
  return QSizeF(-1, -1);
}

QSizeF Menu::implicitSize() const {
  qreal widest = 0, h = 0;
  for (QObject *o : children()) {
    auto *w = qobject_cast<QWidget *>(o);
    if (!w || w->isHidden() || w->isWindow()) continue;
    const QSizeF s = rowSize(w);
    if (s.width() < 0) continue;
    widest = qMax(widest, s.width());
    h += s.height();
  }
  const QMarginsF m = insets();
  return QSizeF(qMax<qreal>(minimumMenuWidth(), widest) + m.left() + m.right(), h + m.top() + m.bottom());
}

QSize Menu::sizeHint() const {
  const QSizeF s = implicitSize();
  return QSize(ceilPx(s.width()), ceilPx(s.height()));
}

// Qt Quick's Column: each row the content's width, at its exact height, at fractional positions.
void Menu::relayout() {
  const QMarginsF m = insets();
  const qreal w = width() - m.left() - m.right();
  qreal y = m.top();
  for (QObject *o : children()) {
    auto *c = qobject_cast<Control *>(o);
    if (!c || c->isHidden()) continue;
    const QSizeF s = rowSize(c);
    if (s.width() < 0) continue;
    c->setGeometryF(QRectF(m.left(), y, w, s.height()));
    y += s.height();
  }
}

bool Menu::event(QEvent *e) {
  switch (e->type()) {
  case QEvent::ChildAdded:
  case QEvent::ChildRemoved:
  case QEvent::LayoutRequest:
    if (e->type() != QEvent::LayoutRequest) {
      QCoreApplication::postEvent(this, new QEvent(QEvent::LayoutRequest));
      break;
    }
    updateGeometry();
    if (!m_held && isVisible() && size() != sizeHint()) resize(sizeHint());
    relayout();
    update();
    break;
  case QEvent::Resize: relayout(); break;
  default: break;
  }
  return QWidget::event(e);
}

void Menu::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  Paint::surface(p, QRectF(rect()), tk.radius(), tk.color(QStringLiteral("surfaces.menu.background")), tk.border(QStringLiteral("surfaces.menu.border")));
}

Menu *Menu::root() {
  Menu *m = this;
  while (m->m_parentMenu) m = m->m_parentMenu;
  return m;
}

void Menu::open() {
  if (m_trigger) {
    const QPoint at = m_trigger->mapToGlobal(QPoint(0, m_trigger->height() + gap()));
    openAt(&at);
  } else {
    openAt(nullptr);
  }
}

void Menu::popup(const QPoint &globalPos) { openAt(&globalPos); }

// Kept on the screen it opens on, as the QML keeps it inside its parent.
void Menu::place(QPoint pos) {
  QScreen *screen = QGuiApplication::screenAt(pos);
  if (!screen) screen = m_trigger ? m_trigger->screen() : parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
  if (screen) {
    const QRect a = screen->availableGeometry();
    pos.setX(qMax(a.left(), qMin(pos.x(), a.right() + 1 - width())));
    pos.setY(qMax(a.top(), qMin(pos.y(), a.bottom() + 1 - height())));
  }
  move(pos);
}

void Menu::openAt(const QPoint *globalPos) {
  setCurrentIndex(-1);
  const bool was = m_opened;
  m_opened = true;
  if (!m_held) {
    resize(sizeHint());
    relayout();
    if (globalPos) place(*globalPos);
    QWidget::show();
    raise();
    activateWindow();
  }
  setFocus(Qt::PopupFocusReason);
  if (!was) emit openedChanged(true);
}

void Menu::close() {
  closeSubmenus();
  if (!m_opened) return;
  m_opened = false;
  setCurrentIndex(-1);
  if (!m_held && isVisible()) {
    m_closing = true;
    hide();
    m_closing = false;
  }
  emit openedChanged(false);
  emit closed();
}

// Hidden by something other than close() (the window system): the menu is closed.
void Menu::hideEvent(QHideEvent *e) {
  QWidget::hideEvent(e);
  if (m_held || m_closing || !m_opened) return;
  close();
}

// While a popup is open it has the pointer, so a press anywhere arrives here. One outside it
// closes the chain, back to the menu it landed on, if it landed on one.
void Menu::mousePressEvent(QMouseEvent *e) {
  if (m_held || !isWindow() || rect().contains(e->position().toPoint())) {
    QWidget::mousePressEvent(e);
    return;
  }
  e->accept();
  const QPoint at = e->globalPosition().toPoint();
  for (Menu *m = m_parentMenu; m; m = m->m_parentMenu) {
    if (m->isVisible() && m->rect().contains(m->mapFromGlobal(at))) {
      m->closeSubmenus();
      m->activateWindow();
      m->setFocus(Qt::PopupFocusReason);
      return;
    }
  }
  closeAll();
}

void Menu::closeSubmenus() {
  for (MenuItem *it : rows())
    if (Menu *sub = it->submenu(); sub && sub->isOpened()) sub->close();
}

void Menu::closeAll() {
  Menu *r = root();
  if (!r->m_held) {
    r->close();
  } else {
    // A held menu stays: the keys come back to it from the submenu that just closed.
    r->closeSubmenus();
    r->activateWindow();
    r->setFocus(Qt::PopupFocusReason);
  }
}

void Menu::moveCursor(int step) {
  const QList<MenuItem *> r = rows();
  const int n = int(r.size());
  if (n == 0) return;
  int i = m_current < 0 ? (step > 0 ? -1 : n) : m_current;
  for (int tries = 0; tries < n; ++tries) {
    i = (i + step + n) % n;
    if (r[i]->isEnabled()) {
      setCurrentIndex(i);
      return;
    }
  }
}

void Menu::jump(bool toEnd) {
  setCurrentIndex(-1);
  moveCursor(toEnd ? -1 : 1);
}

void Menu::openSubmenu(MenuItem *item) {
  Menu *sub = item ? item->submenu() : nullptr;
  if (!sub) return;
  closeSubmenus();
  setCurrentIndex(int(rows().indexOf(item)));
  sub->m_parentMenu = this;
  if (sub->m_held) sub->setHeld(false);
  relayout();
  const QRectF r = item->paintRect();
  const QPoint at = item->mapToGlobal(QPoint(int(std::lround(r.right() + insets().right())), int(std::lround(r.top() - sub->insets().top()))));
  sub->openAt(&at);
  sub->moveCursor(1);
}

void Menu::itemTriggered(MenuItem *item) {
  emit root()->triggered(item);
  closeAll();
}

bool Menu::handleKey(QKeyEvent *e) {
  const QString t = e->text();
  const int key = e->key();
  if (key == Qt::Key_Down || t == QLatin1String("j")) {
    moveCursor(1);
    return true;
  }
  if (key == Qt::Key_Up || t == QLatin1String("k")) {
    moveCursor(-1);
    return true;
  }
  if (key == Qt::Key_Home) {
    jump(false);
    return true;
  }
  if (key == Qt::Key_End) {
    jump(true);
    return true;
  }
  if (isActivateKey(key)) {
    if (MenuItem *it = currentItem()) it->activate();
    return true;
  }
  if (key == Qt::Key_Right || t == QLatin1String("l")) {
    if (MenuItem *it = currentItem(); it && it->hasSubmenu()) openSubmenu(it);
    return true;
  }
  if (key == Qt::Key_Left || t == QLatin1String("h") || key == Qt::Key_Escape) {
    if (Menu *back = m_parentMenu) {
      close();
      back->activateWindow();
      back->setFocus(Qt::PopupFocusReason);
    } else if (key == Qt::Key_Escape) {
      close();
    }
    return true;
  }
  return false;
}

void Menu::keyPressEvent(QKeyEvent *e) {
  if (handleKey(e)) e->accept();
  else QWidget::keyPressEvent(e);
}

// ---------------------------------------------------------------------------------- ContextMenu

ContextMenu::ContextMenu(QWidget *parent) : Menu(parent) {}

void ContextMenu::setTarget(QWidget *w) {
  if (m_target) m_target->removeEventFilter(this);
  m_target = w;
  if (w) {
    w->setContextMenuPolicy(Qt::DefaultContextMenu);
    w->installEventFilter(this);
  }
}

bool ContextMenu::eventFilter(QObject *o, QEvent *e) {
  if (o == m_target && e->type() == QEvent::ContextMenu) {
    popup(static_cast<QContextMenuEvent *>(e)->globalPos());
    return true;
  }
  return Menu::eventFilter(o, e);
}

}  // namespace Omarchy
