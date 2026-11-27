// MultiSelect: the trigger, and its popup in the window's overlay (as Qt Quick's Popup draws in
// the window's overlay): a search field, a rule, a checkbox row per option.

#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>

#include "Omarchy/Forms.h"
#include "Omarchy/Inputs.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

static QString glyph(char32_t cp) { return QString::fromUcs4(&cp, 1); }

// The popups' text color, the one the trigger and rows are drawn in.
static QColor popupText(const Tokens &tk) { return tk.color(QStringLiteral("surfaces.popups.text")); }

// Style.hoverStateColor(fg): the hover state's text color, re-tinted to fg when the theme draws
// it in the foreground.
static QColor hoverText(const Tokens &tk, const QColor &fg) {
  const QColor themed = tk.color(QStringLiteral("controls.hover.text"));
  if (!themed.isValid()) return fg;
  const QColor f = tk.foreground();
  if (themed.rgb() == f.rgb()) {
    QColor c = fg;
    c.setAlpha(themed.alpha());
    return c;
  }
  return themed;
}

// ------------------------------------------------------------------------------ MultiSelectPopup

class MultiSelectPopup : public QWidget {
public:
  MultiSelectPopup(MultiSelect *owner, QWidget *window) : QWidget(window), m_owner(owner) {
    follow(this);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    m_search = new TextField(this);
    m_search->setCursor(Qt::IBeamCursor);
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this] {
      filter();
      // Qt Quick: a new search puts the cursor on the first match.
      if (!m_filtered.isEmpty()) index = 0;
      m_scroll = 0;
      update();
    });
    window->installEventFilter(this);
    qApp->installEventFilter(this);
  }
  ~MultiSelectPopup() override { qApp->removeEventFilter(this); }

  int index = -1;
  TextField *search() const { return m_search; }

  void filter() {
    const QString q = m_search->text().toLower();
    m_filtered.clear();
    for (const Choice &o : m_owner->m_options) {
      if (o.value.isEmpty()) continue;
      if (q.isEmpty() || o.text().toLower().contains(q) || o.description.toLower().contains(q) || o.value.toLower().contains(q)) m_filtered << o;
    }
    if (index >= m_filtered.size()) index = int(m_filtered.size()) - 1;
  }
  const QList<Choice> &filtered() const { return m_filtered; }

  Border border() const {
    const Tokens &tk = shown();
    return tk.border(QStringLiteral("surfaces.popups.border")).withWidth(tk.border(State::Normal).top);
  }
  QMargins insets() const {
    const Border b = border();
    const int h = shown().spacing("hairline");
    return QMargins(b.left + h, b.top + h, b.right + h, b.bottom + h);
  }
  int headerHeight() const { return shown().spacing("popupRowHeight") + shown().spacing("controlPaddingX"); }
  qreal rowHeight(const Choice &o) const {
    const Tokens &tk = shown();
    return qMax<qreal>(tk.spacing("popupRowHeight"), contentHeight(o) + tk.spacing("rowPaddingX"));
  }
  qreal contentHeight(const Choice &o) const {
    const Tokens &tk = shown();
    qreal words = Paint::lineHeight(tk.font("body"));
    if (!o.description.isEmpty()) words += tk.spacing("xxs") + Paint::lineHeight(tk.font("caption"));
    return qMax<qreal>(tk.space(16), words);
  }
  // The list's content height (ListView.contentHeight): rows labelGap apart.
  qreal listHeight() const {
    qreal h = 0;
    for (const Choice &o : m_filtered) h += rowHeight(o);
    if (m_filtered.size() > 1) h += (m_filtered.size() - 1) * shown().spacing("labelGap");
    return h;
  }
  // The list's viewport, under the header and its rule (availableHeight − header − 1 tall).
  QRectF listRect() const {
    const QMargins m = insets();
    const int top = m.top() + headerHeight() + 1;
    return QRectF(m.left(), top, width() - m.left() - m.right(), qMax(0, height() - m.top() - m.bottom() - headerHeight() - 1));
  }
  QRectF rowRect(int i) const {
    const QRectF list = listRect();
    const int gap = shown().spacing("labelGap");
    qreal y = list.y() - m_scroll;
    for (int k = 0; k < i; ++k) y += rowHeight(m_filtered[k]) + gap;
    return QRectF(list.x(), y, list.width(), rowHeight(m_filtered[i]));
  }
  int rowAt(const QPointF &p) const {
    if (!listRect().contains(p)) return -1;
    for (int i = 0; i < m_filtered.size(); ++i)
      if (rowRect(i).contains(p)) return i;
    return -1;
  }

  // Below the trigger, xxs under it, its width; as tall as the rows and the header, at most six
  // rows, and no further down than the window allows.
  void reposition() {
    const Tokens &tk = shown();
    QWidget *win = parentWidget();
    const QRectF tr = m_owner->triggerRect();
    const QPoint at = m_owner->mapTo(win, QPoint(int(tr.x()), int(std::lround(tr.bottom())) + tk.spacing("xxs")));
    const QMargins m = insets();
    const int rh = tk.spacing("popupRowHeight"), gap = tk.spacing("labelGap");
    const qreal chrome = headerHeight() + 1 + m.top() + m.bottom();
    const qreal ideal = qMax<qreal>(rh, listHeight()) + chrome;
    const qreal most = rh * 6 + 5 * gap + chrome;
    const qreal below = qMax(0, win->height() - at.y() - tk.space(12));
    setGeometry(at.x(), at.y(), int(tr.width()), int(std::lround(qMin(qMin(below, ideal), most))));
    const int md = tk.spacing("md");
    const int sh = headerHeight() - md * 2;
    const int refresh = m_owner->m_refreshable ? sh + tk.spacing("rowGap") : 0;
    m_search->setGeometry(m.left() + md, m.top() + md, width() - m.left() - m.right() - md * 2 - refresh, sh);
    clampScroll();
  }

  void clampScroll() { m_scroll = qBound<qreal>(0, m_scroll, qMax<qreal>(0, listHeight() - listRect().height())); }
  void ensureVisible() {
    if (index < 0 || index >= m_filtered.size()) return;
    const QRectF list = listRect(), r = rowRect(index);
    if (r.top() < list.top()) m_scroll -= list.top() - r.top();
    else if (r.bottom() > list.bottom()) m_scroll += r.bottom() - list.bottom();
    clampScroll();
    update();
  }
  void toggleCurrent() {
    if (index < 0 || index >= m_filtered.size()) return;
    m_owner->toggleValue(m_filtered[index].value);
  }
  QRectF refreshRect() const {
    if (!m_owner->m_refreshable) return QRectF();
    const QRect s = m_search->geometry();
    return QRectF(s.right() + 1 + shown().spacing("rowGap"), s.y(), s.height(), s.height());
  }

protected:
  void paintEvent(QPaintEvent *) override {
    const Tokens &tk = shown();
    QPainter p(this);
    Paint::surface(p, QRectF(rect()), tk.radius(), tk.color(QStringLiteral("surfaces.popups.background")), border());
    const QColor fg = popupText(tk);
    const QMargins m = insets();
    const QFont body = tk.font("body");
    const qreal lh = Paint::lineHeight(body);

    if (m_owner->m_refreshable) {
      const QRectF r = refreshRect();
      const State st = m_refreshHot ? State::Hover : State::Normal;
      Paint::surface(p, r, tk.radius(), tk.fill(st, fg), tk.border(st, fg));
      const QString g = m_owner->m_loading ? glyph(U'\U000F0996') : glyph(U'\U000F0450');
      const QSizeF s = Paint::textSize(body, g);
      Paint::text(p, QPointF(r.x() + Paint::centered(r.width(), s.width()), r.y() + Paint::centered(r.height(), s.height())), body, fg, g);
    }

    // The rule under the header.
    p.fillRect(QRectF(m.left(), m.top() + headerHeight(), width() - m.left() - m.right(), 1), Paint::alpha(fg, 0.10));

    const QRectF list = listRect();
    p.save();
    p.setClipRect(list);
    if (m_filtered.isEmpty()) {
      const QString t = m_owner->m_loading ? Theme::instance()->string(QStringLiteral("common.loading"), QStringLiteral("Loading..."))
                        : !m_owner->m_error.isEmpty() ? m_owner->m_error
                                                      : m_owner->emptyText();
      const QSizeF s = Paint::textSize(body, t);
      Paint::text(p, QPointF(list.x() + Paint::centered(list.width(), s.width()), list.y() + Paint::centered(list.height(), s.height())), body,
                  tk.textFaint(), t);
    }
    const int pad = tk.spacing("controlPaddingX"), gap = tk.spacing("rowGap");
    const int box = tk.space(16);
    const QFont check = tk.fontPx(std::lround(box * 0.85), true);
    const QFont caption = tk.font("caption");
    for (int i = 0; i < m_filtered.size(); ++i) {
      const QRectF r = rowRect(i);
      if (r.bottom() < list.top() || r.top() > list.bottom()) continue;
      const Choice &o = m_filtered[i];
      if (i == index) Paint::surface(p, r, tk.smallRadius(), tk.fill(State::Hover, fg));
      const qreal ch = contentHeight(o);
      const qreal cy = r.y() + Paint::centered(r.height(), ch);
      const bool selected = m_owner->isSelected(o.value);
      const QRectF b(r.x() + pad, cy + Paint::centered(ch, box), box, box);
      const State st = selected ? State::Selected : State::Normal;
      Paint::surface(p, b, tk.smallRadius(), selected ? tk.fill(State::Selected, fg) : QColor(Qt::transparent), tk.border(st, fg));
      if (selected) {
        const QString mark = QStringLiteral("✓");
        const QSizeF s = Paint::textSize(check, mark);
        Paint::text(p, QPointF(b.x() + Paint::centered(box, s.width()), b.y() + Paint::centered(box, s.height())), check, tk.selectedText(fg), mark);
      }
      qreal words = lh;
      if (!o.description.isEmpty()) words += tk.spacing("xxs") + Paint::lineHeight(caption);
      const qreal x = b.right() + gap;
      const qreal w = r.right() - pad - x;
      const qreal y = cy + Paint::centered(ch, words);
      Paint::textIn(p, QRectF(x, y, w, lh), Qt::AlignLeft | Qt::AlignTop, body, i == index ? hoverText(tk, fg) : fg, o.text());
      if (!o.description.isEmpty())
        Paint::textIn(p, QRectF(x, y + lh + tk.spacing("xxs"), w, Paint::lineHeight(caption)), Qt::AlignLeft | Qt::AlignTop, caption, tk.textDim(),
                      o.description);
    }
    p.restore();
  }

  void mouseMoveEvent(QMouseEvent *e) override {
    const int i = rowAt(e->position());
    if (i >= 0 && i != index) {
      index = i;
      update();
    }
    const bool hot = refreshRect().contains(e->position());
    if (hot != m_refreshHot) {
      m_refreshHot = hot;
      update();
    }
  }
  void mousePressEvent(QMouseEvent *e) override {
    m_pressedRow = rowAt(e->position());
    e->accept();
  }
  void mouseReleaseEvent(QMouseEvent *e) override {
    if (refreshRect().contains(e->position()) && !m_owner->m_loading) {
      emit m_owner->refreshRequested();
      return;
    }
    const int i = rowAt(e->position());
    if (i >= 0 && i == m_pressedRow) m_owner->toggleValue(m_filtered[i].value);
    m_pressedRow = -1;
  }
  void leaveEvent(QEvent *e) override {
    m_refreshHot = false;
    update();
    QWidget::leaveEvent(e);
  }
  void wheelEvent(QWheelEvent *e) override {
    m_scroll -= e->angleDelta().y() / 4.0;
    clampScroll();
    update();
    e->accept();
  }
  // The list's keys (it holds focus after Down from the search).
  void keyPressEvent(QKeyEvent *e) override {
    const int k = e->key();
    const int n = int(m_filtered.size());
    if (k == Qt::Key_Escape) {
      m_owner->close();
    } else if (k == Qt::Key_Down || e->text() == QLatin1String("j")) {
      if (index < n - 1) index = index + 1;
      ensureVisible();
    } else if (k == Qt::Key_Up || e->text() == QLatin1String("k")) {
      if (index <= 0) m_search->setFocus(Qt::OtherFocusReason);
      else index = index - 1;
      ensureVisible();
    } else if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
      toggleCurrent();
    } else {
      QWidget::keyPressEvent(e);
      return;
    }
    e->accept();
  }
  bool focusNextPrevChild(bool) override { return false; }

  bool eventFilter(QObject *o, QEvent *e) override {
    if (o == m_search && e->type() == QEvent::KeyPress) {
      auto *k = static_cast<QKeyEvent *>(e);
      if (k->key() == Qt::Key_Escape) {
        m_owner->close();
        return true;
      }
      if (k->key() == Qt::Key_Down) {
        if (!m_filtered.isEmpty()) {
          index = 0;
          ensureVisible();
          setFocus(Qt::OtherFocusReason);
        }
        return true;
      }
      if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
        if (!m_filtered.isEmpty()) {
          index = 0;
          toggleCurrent();
        }
        return true;
      }
      if (k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) return true;
    } else if (o == parentWidget() && e->type() == QEvent::Resize) {
      reposition();
    } else if (e->type() == QEvent::MouseButtonPress && isVisible()) {
      // CloseOnPressOutsideParent: a press outside the popup and the trigger closes it.
      // Seen as the window (QWidgetWindow) gets it and again for each widget it goes to; the
      // global position decides, so seeing it twice is harmless.
      auto *me = static_cast<QMouseEvent *>(e);
      if (o->isWindowType() || o->isWidgetType()) {
        const QPoint g = me->globalPosition().toPoint();
        const bool inPopup = rect().contains(mapFromGlobal(g));
        const bool inTrigger = m_owner->triggerRect().contains(m_owner->mapFromGlobal(me->globalPosition()));
        if (!inPopup && !inTrigger) m_owner->close();
      }
    }
    return QWidget::eventFilter(o, e);
  }

private:
  MultiSelect *m_owner;
  TextField *m_search;
  QList<Choice> m_filtered;
  qreal m_scroll = 0;
  int m_pressedRow = -1;
  bool m_refreshHot = false;
};

// ----------------------------------------------------------------------------------- MultiSelect

MultiSelect::MultiSelect(const QString &label, QWidget *parent) : Control(parent), m_label(label) {
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

MultiSelect::~MultiSelect() { delete m_popup; }

void MultiSelect::setLabel(const QString &t) {
  m_label = t;
  updateGeometry();
  update();
}
void MultiSelect::setShowLabel(bool on) {
  m_showLabel = on;
  updateGeometry();
  update();
}
void MultiSelect::setOptions(const QList<Choice> &options) {
  // Normalized: no empty values, no repeats.
  m_options.clear();
  QStringList seen;
  for (const Choice &o : options) {
    if (o.value.isEmpty() || seen.contains(o.value)) continue;
    seen << o.value;
    m_options << o;
  }
  if (m_popup) {
    m_popup->filter();
    m_popup->reposition();
    m_popup->update();
  }
  update();
}
void MultiSelect::setValues(const QStringList &values) {
  m_values = values;
  update();
  if (m_popup) m_popup->update();
}
void MultiSelect::toggleValue(const QString &value) {
  const qsizetype i = m_values.indexOf(value);
  if (i < 0) m_values << value;
  else m_values.removeAt(i);
  update();
  if (m_popup) m_popup->update();
  emit changed(m_values);
}
void MultiSelect::setNoSelectionText(const QString &t) {
  m_none = t;
  update();
}
void MultiSelect::setTriggerLabel(const QString &t) {
  m_triggerLabel = t;
  update();
}
void MultiSelect::setRefreshable(bool on) {
  m_refreshable = on;
  if (m_popup) m_popup->reposition();
}
void MultiSelect::setLoadingOptions(bool on) {
  m_loading = on;
  if (m_popup) m_popup->update();
}
void MultiSelect::setOptionsError(const QString &message) {
  m_error = message;
  if (m_popup) m_popup->update();
}

QString MultiSelect::placeholderText() const {
  return m_placeholder.isEmpty() ? Theme::instance()->string(QStringLiteral("common.search"), QStringLiteral("Search...")) : m_placeholder;
}
QString MultiSelect::emptyText() const {
  return m_empty.isEmpty() ? Theme::instance()->string(QStringLiteral("common.noOptions"), QStringLiteral("No options")) : m_empty;
}
QString MultiSelect::noSelectionText() const {
  return m_none.isEmpty() ? Theme::instance()->string(QStringLiteral("common.noneSelected"), QStringLiteral("None selected")) : m_none;
}

// The chosen labels joined with ", ", or "N selected" past three.
QString MultiSelect::selectionLabel() const {
  if (m_values.isEmpty()) return QString();
  if (m_values.size() > 3)
    return Theme::instance()->string(QStringLiteral("multiSelect.selectedCount"), QStringLiteral("%1 selected")).arg(m_values.size());
  QStringList labels;
  for (const QString &v : m_values) {
    QString l = v;
    for (const Choice &o : m_options)
      if (o.value == v) l = o.text();
    labels << l;
  }
  return labels.join(QStringLiteral(", "));
}

bool MultiSelect::isPopupOpen() const { return m_popup && m_popup->isVisible(); }
QWidget *MultiSelect::popup() const { return m_popup; }
TextField *MultiSelect::searchField() const { return m_popup ? m_popup->search() : nullptr; }
int MultiSelect::popupIndex() const { return m_popup ? m_popup->index : -1; }
QRectF MultiSelect::popupRowRect(int index) const {
  if (!isPopupOpen() || index < 0 || index >= m_popup->filtered().size()) return QRectF();
  return m_popup->rowRect(index);
}

qreal MultiSelect::labelHeight() const { return Paint::lineHeight(t().font("caption", true)); }

QRectF MultiSelect::triggerRect() const {
  const Tokens &tk = t();
  const bool labeled = m_showLabel && !m_label.isEmpty();
  return QRectF(0, labeled ? labelHeight() + tk.spacing("labelGap") : 0, width(), tk.spacing("controlHeight"));
}

QSize MultiSelect::sizeHint() const {
  const Tokens &tk = t();
  const bool labeled = m_showLabel && !m_label.isEmpty();
  const qreal h = labeled ? labelHeight() + tk.spacing("labelGap") + tk.spacing("controlHeight") : tk.spacing("controlHeight");
  return QSize(tk.spacing("searchableDropdownWidth"), int(std::ceil(h - 1e-6)));
}

void MultiSelect::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  if (m_showLabel && !m_label.isEmpty()) Paint::text(p, QPointF(0, 0), tk.font("caption", true), tk.textDim(), m_label);
  const QRectF r = triggerRect();
  const State st = hasFocus() ? State::Focus : isHot() ? State::Hover : State::Normal;
  const QColor fg = popupText(tk);
  const Border b = tk.border(st, fg);
  Paint::surface(p, r, tk.radius(), tk.fill(st, fg), b);
  const QFont f = tk.font("body");
  const QString chev = glyph(U'\U000F0140');
  const QSizeF cs = Paint::textSize(f, chev);
  const qreal cx = r.right() - b.right - tk.spacing("controlGap") - cs.width();
  Paint::text(p, QPointF(cx, r.y() + Paint::centered(r.height(), cs.height())), f, tk.textDim(), chev);
  const QString chosen = selectionLabel();
  const QString shownText = !chosen.isEmpty() ? chosen : !m_triggerLabel.isEmpty() ? m_triggerLabel : noSelectionText();
  const qreal lx = r.x() + b.left + tk.spacing("controlPaddingX");
  const qreal lh = Paint::lineHeight(f);
  Paint::textIn(p, QRectF(lx, r.y() + Paint::centered(r.height(), lh), cx - tk.spacing("md") - lx, lh), Qt::AlignLeft | Qt::AlignTop, f,
                chosen.isEmpty() ? tk.textDim() : fg, shownText);
}

void MultiSelect::open() {
  if (isPopupOpen()) return;
  QWidget *win = window();
  if (!m_popup || m_popup->parentWidget() != win) {
    delete m_popup;
    m_popup = new MultiSelectPopup(this, win);
  }
  MultiSelectPopup *pop = m_popup;
  pop->search()->setPlaceholderText(placeholderText());
  pop->search()->clear();
  pop->index = -1;
  pop->filter();
  pop->reposition();
  pop->show();
  pop->raise();
  pop->search()->setFocus(Qt::PopupFocusReason);
  update();
}

void MultiSelect::close() {
  if (!isPopupOpen()) return;
  const bool hadFocus = m_popup->isAncestorOf(QApplication::focusWidget()) || m_popup->hasFocus();
  m_popup->hide();
  m_popup->search()->clear();
  if (hadFocus) setFocus(Qt::PopupFocusReason);
  update();
}

void MultiSelect::toggle() { isPopupOpen() ? close() : open(); }

void MultiSelect::keyPressEvent(QKeyEvent *e) {
  const int k = e->key();
  if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space || k == Qt::Key_Down || e->text() == QLatin1String("j")) {
    toggle();
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

void MultiSelect::mousePressEvent(QMouseEvent *e) {
  if (!triggerRect().contains(e->position())) {
    e->ignore();
    return;
  }
  Control::mousePressEvent(e);
}

void MultiSelect::activate(Qt::MouseButton b) {
  if (b != Qt::LeftButton) return;
  setFocus(Qt::MouseFocusReason);
  toggle();
}

bool MultiSelect::eventFilter(QObject *o, QEvent *e) { return Control::eventFilter(o, e); }

}  // namespace Omarchy
