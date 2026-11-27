// SplitView, Sidebar and Calendar (spec/components/data.md), drawn as SplitView.qml,
// Sidebar.qml and Calendar.qml.

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitterHandle>
#include <cmath>

#include "Omarchy/Button.h"
#include "Omarchy/Data.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

namespace {

QString cp(uint c) { return QString::fromUcs4(reinterpret_cast<const char32_t *>(&c), 1); }

QColor rowFill(const Tokens &tk, bool cursor, bool selected) {
  if (cursor) return tk.fill(State::Hover);
  if (selected) return tk.fill(State::Selected);
  return Qt::transparent;
}

int ceilPx(qreal v) { return int(std::ceil(v - 1e-6)); }

// SplitView.qml's handle: a strip holding a hairline in `track`, lit with the hover fill under
// the pointer, while dragged, or with the view's cursor flag.
class Handle : public QSplitterHandle {
public:
  Handle(Qt::Orientation o, SplitView *view) : QSplitterHandle(o, view), m_view(view) {
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);
    follow(this);
  }

protected:
  void enterEvent(QEnterEvent *e) override {
    QSplitterHandle::enterEvent(e);
    m_hovered = true;
    update();
  }
  void leaveEvent(QEvent *e) override {
    QSplitterHandle::leaveEvent(e);
    m_hovered = false;
    update();
  }
  void mousePressEvent(QMouseEvent *e) override {
    if (e->button() == Qt::LeftButton) m_pressed = true;
    update();
    QSplitterHandle::mousePressEvent(e);
  }
  void mouseReleaseEvent(QMouseEvent *e) override {
    if (e->button() == Qt::LeftButton) m_pressed = false;
    update();
    QSplitterHandle::mouseReleaseEvent(e);
  }
  void paintEvent(QPaintEvent *) override {
    const Tokens &tk = shown();
    QPainter p(this);
    const bool hot = m_view->hasCursor() || m_hovered || m_pressed;
    if (hot) Paint::surface(p, QRectF(rect()), tk.smallRadius(), tk.fill(State::Hover));
    const int hair = tk.spacing("hairline");
    if (orientation() == Qt::Horizontal) p.fillRect(QRect((width() - hair) / 2, 0, hair, height()), tk.track());
    else p.fillRect(QRect(0, (height() - hair) / 2, width(), hair), tk.track());
  }

private:
  SplitView *m_view;
  bool m_hovered = false, m_pressed = false;
};

}  // namespace

// ------------------------------------------------------------------------------------- SplitView

SplitView::SplitView(Qt::Orientation orientation, QWidget *parent) : QSplitter(orientation, parent) {
  setChildrenCollapsible(false);
  setOpaqueResize(true);
  setHandleWidth(handleThickness());
  connect(Theme::instance(), &Theme::metricsChanged, this, [this] { setHandleWidth(handleThickness()); });
}

int SplitView::handleThickness() const { return shown().spacing("md"); }

QSplitterHandle *SplitView::createHandle() { return new Handle(orientation(), this); }

void SplitView::setHasCursor(bool on) {
  m_hasCursor = on;
  for (int i = 0; i < count(); ++i)
    if (QSplitterHandle *h = handle(i)) h->update();
}

void SplitView::addPane(QWidget *pane, int preferred, int minimum, int maximum, bool fill) {
  addWidget(pane);
  const bool across = orientation() == Qt::Horizontal;
  if (minimum >= 0) across ? pane->setMinimumWidth(minimum) : pane->setMinimumHeight(minimum);
  if (maximum >= 0) across ? pane->setMaximumWidth(maximum) : pane->setMaximumHeight(maximum);
  const int i = indexOf(pane);
  setCollapsible(i, false);
  setStretchFactor(i, fill ? 1 : 0);
  m_panes.append(Pane{pane, preferred, fill});
  m_distributed = false;
  if (width() > 0 && height() > 0) distribute();
}

// The preferred sizes, the fill pane taking what is left (QQC.SplitView's layout).
void SplitView::distribute() {
  const bool across = orientation() == Qt::Horizontal;
  const int total = across ? width() : height();
  QList<int> sizes;
  int used = 0, fillAt = -1, handles = 0;
  for (int i = 0; i < count(); ++i) {
    QWidget *w = widget(i);
    int s = 0;
    bool fill = false;
    for (const Pane &p : m_panes)
      if (p.widget == w) {
        fill = p.fill;
        s = p.preferred >= 0 ? p.preferred : (across ? w->sizeHint().width() : w->sizeHint().height());
      }
    if (w->isHidden()) s = 0;
    else if (used > 0 || fillAt >= 0 || i > 0) handles += handleWidth();
    if (fill && fillAt < 0) fillAt = i;
    sizes << (fill ? 0 : s);
    used += fill ? 0 : s;
  }
  if (fillAt < 0) fillAt = count() - 1;
  if (fillAt >= 0) sizes[fillAt] = qMax(0, total - used - handles);
  setSizes(sizes);
  m_distributed = true;
}

void SplitView::resizeEvent(QResizeEvent *e) {
  QSplitter::resizeEvent(e);
  if (!m_distributed && width() > 0 && height() > 0) distribute();
}

// --------------------------------------------------------------------------------------- Sidebar

Sidebar::Sidebar(QWidget *parent) : Control(parent) { setFocusPolicy(Qt::StrongFocus); }

void Sidebar::setSections(const QList<SidebarSection> &sections) {
  m_sections = sections;
  updateGeometry();
  update();
}
void Sidebar::setSelectedId(const QString &id) {
  m_selected = id;
  update();
}
void Sidebar::setCursorId(const QString &id) {
  if (m_cursor == id) return;
  m_cursor = id;
  update();
  emit cursorIdChanged(id);
}
void Sidebar::setCollapsed(bool on) {
  m_collapsed = on;
  updateGeometry();
  update();
}

int Sidebar::railWidth() const {
  const Tokens &tk = t();
  return tk.spacing("sm") * 2 + tk.spacing("controlPaddingX") * 2 + tk.fontSize("icon");
}

// Sidebar.qml's Column of section Columns: positions stay fractional (the header's line box).
void Sidebar::place(QList<Placed> *rows, QList<Header> *headers, qreal *height) const {
  const Tokens &tk = t();
  const int pad = tk.spacing("sm"), rh = tk.spacing("popupRowHeight"), gap = tk.spacing("md");
  const qreal colW = width() - pad * 2;
  const int caption = tk.fontSize("caption");
  const qreal headerH = std::ceil(caption * 0.15) + Paint::lineHeight(tk.font("caption", true)) + tk.spacing("xxs");
  qreal y = pad;
  for (int s = 0; s < m_sections.size(); ++s) {
    const SidebarSection &sec = m_sections.at(s);
    if (s > 0) y += gap;
    if (!m_collapsed && !sec.title.isEmpty()) {
      if (headers) headers->append(Header{sec.title, QRectF(pad, y, colW, headerH)});
      y += headerH;
    }
    for (const SidebarItem &it : sec.items) {
      if (rows) rows->append(Placed{it.id, it.label, it.glyph, QRectF(pad, y, colW, rh)});
      y += rh;
    }
  }
  if (height) *height = y + pad;
}

QSize Sidebar::sizeHint() const {
  qreal h = 0;
  place(nullptr, nullptr, &h);
  return QSize(m_collapsed ? railWidth() : t().space(200), ceilPx(h));
}

QRectF Sidebar::rowRect(const QString &id) const {
  QList<Placed> rows;
  place(&rows, nullptr, nullptr);
  for (const Placed &r : rows)
    if (r.id == id) return r.rect;
  return QRectF();
}

QString Sidebar::rowAt(const QPointF &pos) const {
  QList<Placed> rows;
  place(&rows, nullptr, nullptr);
  for (const Placed &r : rows)
    if (r.rect.contains(pos)) return r.id;
  return QString();
}

QStringList Sidebar::ids() const {
  QStringList out;
  for (const SidebarSection &s : m_sections)
    for (const SidebarItem &it : s.items) out << it.id;
  return out;
}

void Sidebar::moveCursor(int delta) {
  const QStringList all = ids();
  if (all.isEmpty()) return;
  const int at = int(all.indexOf(m_cursor));
  const int from = at < 0 ? (delta > 0 ? -1 : int(all.size())) : at;
  setCursorId(all.at(qBound(0, from + delta, int(all.size()) - 1)));
}

void Sidebar::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
  case Qt::Key_Up: moveCursor(-1); break;
  case Qt::Key_Down: moveCursor(1); break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space:
    if (!m_cursor.isEmpty()) emit activated(m_cursor);
    break;
  default: Control::keyPressEvent(e); return;
  }
  e->accept();
}

void Sidebar::hover(const QPointF &pos) {
  const QString id = rowAt(pos);
  const bool was = m_bodyHover;
  m_bodyHover = !id.isEmpty();
  if (m_bodyHover && id != m_hoverId) setCursorId(id);
  m_hoverId = id;
  if (was != m_bodyHover) update();
}

void Sidebar::mouseMoveEvent(QMouseEvent *e) {
  hover(e->position());
  Control::mouseMoveEvent(e);
}

void Sidebar::leaveEvent(QEvent *e) {
  m_bodyHover = false;
  m_hoverId.clear();
  Control::leaveEvent(e);
}

void Sidebar::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) m_pressId = rowAt(e->position());
  Control::mousePressEvent(e);
}

void Sidebar::mouseReleaseEvent(QMouseEvent *e) {
  const QString id = m_pressId;
  m_pressId.clear();
  Control::mouseReleaseEvent(e);
  if (e->button() == Qt::LeftButton && !id.isEmpty() && rowAt(e->position()) == id) {
    setFocus(Qt::MouseFocusReason);
    emit activated(id);
  }
}

void Sidebar::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  QList<Placed> rows;
  QList<Header> headers;
  place(&rows, &headers, nullptr);
  const int inset = tk.spacing("controlPaddingX"), rh = tk.spacing("popupRowHeight");
  const int caption = tk.fontSize("caption");
  const QFont headerFont = tk.font("caption", true);
  for (const Header &h : headers) {
    const qreal w = h.rect.width() - inset;
    Paint::text(p, QPointF(h.rect.x() + inset, h.rect.y() + std::ceil(caption * 0.15)), headerFont, tk.textDim(),
                Paint::elided(headerFont, h.title, w));
  }
  const bool showCursor = hasCursor() || hasFocus() || m_bodyHover;
  const QFont glyphFont = tk.font("icon"), body = tk.font("body"), bold = tk.font("body", true);
  const int labelOffset = tk.fontSize("icon") + tk.spacing("controlGap");
  for (const Placed &r : rows) {
    const bool selected = m_selected == r.id, cursor = showCursor && m_cursor == r.id;
    Paint::surface(p, r.rect, tk.radius(), rowFill(tk, cursor, selected));
    const QColor ink = selected ? tk.selectedText() : tk.foreground();
    if (!r.glyph.isEmpty()) {
      Paint::opticalGlyph(p, QRectF(r.rect.x() + inset, r.rect.y(), tk.fontSize("icon"), rh), glyphFont, selected ? ink : tk.textDim(), r.glyph);
    }
    if (m_collapsed) continue;
    const QFont &f = selected ? bold : body;
    const qreal lh = Paint::lineHeight(f);
    const qreal x = inset + labelOffset;
    Paint::textIn(p, QRectF(r.rect.x() + x, r.rect.y() + Paint::centered(rh, lh), qMax<qreal>(0, r.rect.width() - x - inset), lh),
                  Qt::AlignLeft, f, ink, r.label);
  }
}

// -------------------------------------------------------------------------------------- Calendar

Calendar::Calendar(QWidget *parent) : Control(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_monthNames = {QStringLiteral("January"), QStringLiteral("February"), QStringLiteral("March"),     QStringLiteral("April"),
                  QStringLiteral("May"),     QStringLiteral("June"),     QStringLiteral("July"),      QStringLiteral("August"),
                  QStringLiteral("September"), QStringLiteral("October"), QStringLiteral("November"), QStringLiteral("December")};
  m_weekdayNames = {QStringLiteral("Mo"), QStringLiteral("Tu"), QStringLiteral("We"), QStringLiteral("Th"),
                    QStringLiteral("Fr"), QStringLiteral("Sa"), QStringLiteral("Su")};
  m_prev = new IconButton(cp(0xF0141), this);
  m_next = new IconButton(cp(0xF0142), this);
  auto names = [this] {
    m_prev->setTooltipText(Theme::instance()->string(QStringLiteral("clock.previousMonth"), QStringLiteral("Previous month")));
    m_next->setTooltipText(Theme::instance()->string(QStringLiteral("clock.nextMonth"), QStringLiteral("Next month")));
  };
  names();
  connect(Theme::instance(), &Theme::changed, this, names);
  connect(m_prev, &IconButton::clicked, this, [this] { stepMonth(-1); });
  connect(m_next, &IconButton::clicked, this, [this] { stepMonth(1); });
  placeButtons();
}

void Calendar::setYear(int year) { setMonth(year, m_month); }
void Calendar::setMonth(int month) { setMonth(m_year, month); }
void Calendar::setMonth(int year, int month) {
  month = qBound(1, month, 12);
  if (year == m_year && month == m_month) return;
  m_year = year;
  m_month = month;
  update();
  emit monthChanged(year, month);
}

void Calendar::stepMonth(int delta) {
  const int m = (m_month - 1) + delta;
  const int years = int(std::floor(m / 12.0));
  setMonth(m_year + years, ((m % 12) + 12) % 12 + 1);
}

void Calendar::setToday(const QDate &d) {
  m_today = d;
  update();
}
void Calendar::setSelected(const QDate &d) {
  m_selected = d;
  update();
}
void Calendar::setCursorDate(const QDate &d) {
  if (m_cursor == d) return;
  m_cursor = d;
  update();
  emit cursorDateChanged(d);
}
void Calendar::setMonthNames(const QStringList &names) {
  if (names.size() == 12) m_monthNames = names;
  update();
}
void Calendar::setWeekdayNames(const QStringList &names) {
  if (names.size() == 7) m_weekdayNames = names;
  update();
}

int Calendar::cellWidth() const { return t().space(36); }
int Calendar::cellHeight() const { return t().spacing("controlHeight"); }
int Calendar::cellSpacing() const { return t().spacing("xxs"); }
int Calendar::gridWidth() const { return cellWidth() * 7 + cellSpacing() * 6; }
int Calendar::headerHeight() const { return m_prev->sizeHint().height(); }
// The header, `sm`, the weekday row, then the weeks `xxs` below it.
int Calendar::gridTop() const { return headerHeight() + t().spacing("sm") + t().space(16) + cellSpacing(); }

QSize Calendar::sizeHint() const { return QSize(gridWidth(), gridTop() + cellHeight() * 6 + cellSpacing() * 5); }

QDate Calendar::firstShown() const {
  const QDate first(m_year, m_month, 1);
  return first.addDays(-(first.dayOfWeek() - 1));
}

QRect Calendar::dayRect(const QDate &d) const {
  const qint64 i = firstShown().daysTo(d);
  if (i < 0 || i >= 42) return QRect();
  const int col = int(i % 7), row = int(i / 7);
  return QRect(col * (cellWidth() + cellSpacing()), gridTop() + row * (cellHeight() + cellSpacing()), cellWidth(), cellHeight());
}

QDate Calendar::dayAt(const QPointF &pos) const {
  const QDate first = firstShown();
  for (int i = 0; i < 42; ++i) {
    const QDate d = first.addDays(i);
    if (QRectF(dayRect(d)).contains(pos)) return d;
  }
  return QDate();
}

void Calendar::placeButtons() {
  const int s = headerHeight();
  const int cw = cellWidth();
  const int inset = int(std::floor((cw - s) / 2.0 + 0.5));
  m_prev->setGeometry(inset, 0, s, s);
  m_next->setGeometry(gridWidth() - cw + inset, 0, s, s);
}

void Calendar::resizeEvent(QResizeEvent *e) {
  Control::resizeEvent(e);
  placeButtons();
}

void Calendar::themeMetricsChanged() { placeButtons(); }

void Calendar::moveCursor(int days) {
  QDate from = m_cursor.isValid() ? m_cursor : m_selected.isValid() ? m_selected : m_today.isValid() ? m_today : QDate(m_year, m_month, 1);
  const QDate d = from.addDays(days);
  setCursorDate(d);
  if (d.year() != m_year || d.month() != m_month) setMonth(d.year(), d.month());
}

void Calendar::pick(const QDate &d) {
  m_selected = d;
  update();
  emit picked(d);
}

void Calendar::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
  case Qt::Key_Left: moveCursor(-1); break;
  case Qt::Key_Right: moveCursor(1); break;
  case Qt::Key_Up: moveCursor(-7); break;
  case Qt::Key_Down: moveCursor(7); break;
  case Qt::Key_PageUp: stepMonth(-1); break;
  case Qt::Key_PageDown: stepMonth(1); break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space:
    if (m_cursor.isValid()) pick(m_cursor);
    break;
  default: Control::keyPressEvent(e); return;
  }
  e->accept();
}

void Calendar::hover(const QPointF &pos) {
  const QDate d = dayAt(pos);
  const bool was = m_bodyHover;
  m_bodyHover = d.isValid();
  if (d.isValid() && d != m_hoverDay) setCursorDate(d);
  m_hoverDay = d;
  if (was != m_bodyHover) update();
}

void Calendar::mouseMoveEvent(QMouseEvent *e) {
  hover(e->position());
  Control::mouseMoveEvent(e);
}

void Calendar::leaveEvent(QEvent *e) {
  m_bodyHover = false;
  m_hoverDay = QDate();
  Control::leaveEvent(e);
}

void Calendar::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) m_pressDay = dayAt(e->position());
  Control::mousePressEvent(e);
}

void Calendar::mouseReleaseEvent(QMouseEvent *e) {
  const QDate d = m_pressDay;
  m_pressDay = QDate();
  Control::mouseReleaseEvent(e);
  if (e->button() == Qt::LeftButton && d.isValid() && dayAt(e->position()) == d) {
    setFocus(Qt::MouseFocusReason);
    pick(d);
  }
}

void Calendar::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const int gw = gridWidth(), hh = headerHeight(), cw = cellWidth(), ch = cellHeight(), cs = cellSpacing();

  // Month and year, centered across the grid between the steppers.
  const QFont body = tk.font("body"), bold = tk.font("body", true);
  const QString title = (m_monthNames.value(m_month - 1) + QLatin1Char(' ') + QString::number(m_year)).toUpper();
  const QSizeF ts = Paint::textSize(body, title);
  Paint::text(p, QPointF((gw - ts.width()) / 2, Paint::centered(hh, ts.height())), body, tk.textDim(), title);

  // Weekdays, each centered in a cell-wide box space(16) tall.
  const QFont caption = tk.font("caption", true);
  const int wy = hh + tk.spacing("sm"), wh = tk.space(16);
  for (int i = 0; i < 7; ++i) {
    const QString name = m_weekdayNames.value(i).toUpper();
    const QSizeF s = Paint::textSize(caption, name);
    Paint::text(p, QPointF(i * (cw + cs) + (cw - s.width()) / 2, wy + (wh - s.height()) / 2), caption, tk.textDim(), name);
  }

  // Six weeks.
  const bool showCursor = hasCursor() || hasFocus() || m_bodyHover;
  const QDate first = firstShown();
  const int hair = tk.spacing("hairline");
  for (int i = 0; i < 42; ++i) {
    const QDate d = first.addDays(i);
    const QRectF r(dayRect(d));
    const bool inMonth = d.month() == m_month, weekend = i % 7 >= 5;
    const bool today = d == m_today, selected = d == m_selected, cursor = showCursor && d == m_cursor;
    Paint::surface(p, r, tk.radius(), rowFill(tk, cursor, selected), today ? Border::flat(tk.accent(), hair) : Border());
    const QColor ink = !inMonth ? tk.textFaint() : selected ? tk.selectedText() : weekend ? tk.textDim() : tk.foreground();
    const QFont &f = selected || today ? bold : body;
    const QString n = QString::number(d.day());
    const QSizeF s = Paint::textSize(f, n);
    Paint::text(p, QPointF(r.x() + Paint::centered(cw, s.width()), r.y() + Paint::centered(ch, s.height())), f, ink, n);
  }
}

}  // namespace Omarchy
