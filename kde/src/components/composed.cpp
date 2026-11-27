// SearchList: qt/qml/Omarchy/Ui/SearchList.qml on FilterInput.qml, with the kit's ListRow,
// CursorModel, ScrollArea and EmptyState.

#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QTextBoundaryFinder>
#include <cmath>

#include "Omarchy/Composed.h"
#include "Omarchy/Feedback.h"
#include "Omarchy/Lists.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

static const QString kResults = QStringLiteral("results");

// ----------------------------------------------------------------------------------- FilterInput

FilterInput::FilterInput(QWidget *parent) : QWidget(parent) {
  follow(this);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_InputMethodEnabled);
  setCursor(Qt::IBeamCursor);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FilterInput::setText(const QString &t) {
  if (t == m_text) return;
  m_text = t;
  update();
  emit textChanged(m_text);
}

void FilterInput::setPlaceholderText(const QString &t) {
  m_placeholder = t;
  update();
}

void FilterInput::setFontToken(const char *token) {
  m_font = token;
  updateGeometry();
  update();
}

void FilterInput::setColorPath(const QString &path) {
  m_colorPath = path;
  update();
}

QFont FilterInput::textFont() const { return shown().font(m_font.constData()); }

QSize FilterInput::sizeHint() const {
  const QFont f = textFont();
  return QSize(int(std::ceil(Paint::textWidth(f, m_text + m_preedit) - 1e-6)), int(std::ceil(Paint::lineHeight(f) - 1e-6)));
}

void FilterInput::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  const QFont f = textFont();
  QPainter p(this);
  p.setClipRect(rect());
  const qreal lh = Paint::lineHeight(f);
  if (m_text.isEmpty() && m_preedit.isEmpty()) {
    // The placeholder Text fills the input, vertically centered.
    if (!m_placeholder.isEmpty())
      Paint::textIn(p, QRectF(0, (height() - lh) / 2, width(), lh), Qt::AlignLeft | Qt::AlignTop, f, tk.textFaint(), m_placeholder);
    return;
  }
  // TextInput's autoScroll: the end of the line stays in view.
  const qreal textW = Paint::textWidth(f, m_text);
  const qreal preW = Paint::textWidth(f, m_preedit);
  const qreal x = qMin<qreal>(0, width() - textW - preW);
  const QColor c = tk.color(m_colorPath);
  Paint::text(p, QPointF(x, 0), f, c, m_text);
  if (!m_preedit.isEmpty()) {
    Paint::text(p, QPointF(x + textW, 0), f, c, m_preedit);
    p.fillRect(QRectF(x + textW, Paint::ascent(f) + 2, preW, 1), c);
  }
}

// What a key types here: its text without Ctrl, Alt or Meta, when it holds no control character.
static QString typed(const QKeyEvent *e) {
  if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return QString();
  const QString t = e->text();
  for (const QChar c : t)
    if (c.unicode() < 0x20 || c.unicode() == 0x7f) return QString();
  return t;
}

void FilterInput::keyPressEvent(QKeyEvent *e) {
  if (m_handler && m_handler(e)) {
    e->accept();
    return;
  }
  // Alt and Super chords are shortcuts, never text.
  if (e->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) {
    e->accept();
    return;
  }
  if (e->key() == Qt::Key_Backspace) {
    if (m_text.isEmpty()) return;
    QTextBoundaryFinder finder(e->modifiers() & Qt::ControlModifier ? QTextBoundaryFinder::Word : QTextBoundaryFinder::Grapheme, m_text);
    finder.setPosition(int(m_text.size()));
    qsizetype at = finder.toPreviousBoundary();
    // A word: back over the spaces before it too.
    if (e->modifiers() & Qt::ControlModifier)
      while (at > 0 && !(finder.boundaryReasons() & QTextBoundaryFinder::StartOfItem)) at = finder.toPreviousBoundary();
    setText(m_text.left(qMax<qsizetype>(0, at)));
    e->accept();
    return;
  }
  if (e->matches(QKeySequence::Paste)) {
    QString t = QApplication::clipboard()->text();
    t = t.section(QLatin1Char('\n'), 0, 0);
    if (!t.isEmpty()) setText(m_text + t);
    e->accept();
    return;
  }
  const QString t = typed(e);
  if (!t.isEmpty()) {
    setText(m_text + t);
    e->accept();
    return;
  }
  e->ignore();
}

bool FilterInput::event(QEvent *e) {
  // Keys that type or edit here win over application shortcuts, as a line edit's do.
  if (e->type() == QEvent::ShortcutOverride) {
    auto *k = static_cast<QKeyEvent *>(e);
    if (!typed(k).isEmpty() || k->key() == Qt::Key_Backspace || k->matches(QKeySequence::Paste)) {
      e->accept();
      return true;
    }
  }
  return QWidget::event(e);
}

void FilterInput::inputMethodEvent(QInputMethodEvent *e) {
  const QString preedit = e->preeditString();
  const bool changed = preedit != m_preedit;
  m_preedit = preedit;
  if (!e->commitString().isEmpty()) setText(m_text + e->commitString());
  if (changed) update();
  e->accept();
}

QVariant FilterInput::inputMethodQuery(Qt::InputMethodQuery q) const {
  switch (q) {
  case Qt::ImEnabled: return true;
  case Qt::ImCursorRectangle: {
    const QFont f = textFont();
    const qreal x = qMin<qreal>(width(), Paint::textWidth(f, m_text + m_preedit));
    return QRectF(x, 0, 1, Paint::lineHeight(f)).toAlignedRect();
  }
  case Qt::ImFont: return textFont();
  case Qt::ImSurroundingText: return m_text;
  case Qt::ImCursorPosition:
  case Qt::ImAnchorPosition: return int(m_text.size());
  case Qt::ImCurrentSelection: return QString();
  case Qt::ImHints: return int(Qt::ImhNoPredictiveText);
  default: return QWidget::inputMethodQuery(q);
  }
}

// ------------------------------------------------------------------------------------ SearchList

bool SearchList::matches(const QString &text, const QString &query) { return text.toLower().contains(query.toLower()); }

SearchList::SearchList(QWidget *parent) : QWidget(parent) {
  follow(this);
  m_emptyGlyph = QString::fromUcs4(U"\U000F0349");

  m_cursor = new CursorModel(this);
  m_cursor->setSections({{kResults, 0}});
  // The shell's overlays open with the cursor on the first row.
  m_cursor->set(kResults, 0);
  m_cursor->setReferenceWidget(this);
  connect(m_cursor, &CursorModel::changed, this, [this] {
    update();
    const int i = cursorIndex();
    if (i != m_lastCursor) {
      m_lastCursor = i;
      emit cursorIndexChanged(i);
    }
  });
  connect(m_cursor, &CursorModel::moved, this, [this](const QString &, int index, bool byKey) {
    if (byKey) m_scroll->ensureVisible(m_rows.value(index));
  });
  connect(m_cursor, &CursorModel::activated, this, [this](const QString &, int index) {
    if (!m_items.isEmpty()) emit activated(index);
  });
  connect(m_cursor, &CursorModel::deleteRequested, this, [this](const QString &, int index) {
    if (!m_items.isEmpty()) emit deleteRequested(index);
  });

  m_scroll = new ScrollArea(this);
  m_input = new FilterInput(this);
  m_input->setFontToken("heading");
  m_input->setColorPath(QStringLiteral("surfaces.menu.text"));
  m_input->setKeyHandler([this](QKeyEvent *e) { return handleKey(e); });
  connect(m_input, &FilterInput::textChanged, this, &SearchList::setQuery);
  setFocusProxy(m_input);
  m_empty = new EmptyState(QString(), this);
  m_empty->setOverlay(true);
  m_lastCursor = cursorIndex();

  connect(Theme::instance(), &Theme::changed, this, [this] { sync(); });
  connect(Theme::instance(), &Theme::metricsChanged, this, [this] {
    updateGeometry();
    place();
  });
  sync();
  place();
}

void SearchList::setQuery(const QString &q) {
  if (q == m_query) return;
  m_query = q;
  m_input->setText(q);
  // A new query starts the cursor at the first match again, as the shell's overlays do.
  m_cursor->set(kResults, 0);
  sync();
  emit queryChanged(q);
}

QString SearchList::placeholderText() const {
  return m_placeholderSet ? m_placeholder : Theme::instance()->string(QStringLiteral("common.search"), QStringLiteral("Search..."));
}

void SearchList::setPlaceholderText(const QString &t) {
  m_placeholder = t;
  m_placeholderSet = true;
  sync();
}

void SearchList::setItems(const QList<SearchItem> &items) {
  m_items = items;
  while (m_rows.size() < m_items.size()) {
    const int i = int(m_rows.size());
    auto *row = new ListRow;
    m_scroll->addWidget(row);
    m_cursor->attach(row, kResults, i);
    connect(row, &ListRow::clicked, this, [this, i] {
      m_cursor->set(kResults, i);
      emit activated(i);
    });
    m_rows << row;
  }
  for (int i = 0; i < m_rows.size(); ++i) {
    ListRow *row = m_rows[i];
    if (i < m_items.size()) {
      const SearchItem &it = m_items[i];
      row->setGlyph(it.glyph);
      row->setTitle(it.title);
      row->setSubtitle(it.subtitle);
      row->setValue(it.value);
      row->show();
    } else {
      row->hide();
    }
  }
  m_cursor->setCount(kResults, int(m_items.size()));
  sync();
  place();
  const int c = cursorIndex();
  if (c != m_lastCursor) {
    m_lastCursor = c;
    emit cursorIndexChanged(c);
  }
}

void SearchList::setShowDetail(bool on) {
  m_showDetail = on;
  place();
  update();
}

void SearchList::setDetail(QWidget *w) {
  if (m_detail == w) return;
  if (m_detail) m_detail->deleteLater();
  m_detail = w;
  if (w) w->setParent(this);
  place();
  update();
}

qreal SearchList::detailWidth() const { return m_detailWidth >= 0 ? m_detailWidth : std::floor(bodyRect().width() * 0.45); }

void SearchList::setDetailWidth(qreal w) {
  m_detailWidth = w;
  place();
  update();
}

void SearchList::setEmptyGlyph(const QString &g) {
  m_emptyGlyph = g;
  sync();
}

void SearchList::setEmptyText(const QString &t) {
  m_emptyText = t;
  sync();
}

void SearchList::setNoMatchesText(const QString &t) {
  m_noMatches = t;
  m_noMatchesSet = true;
  sync();
}

QString SearchList::emptyTitle() const {
  if (m_query.isEmpty()) return m_emptyText;
  const QString text = m_noMatchesSet ? m_noMatches
                                      : Theme::instance()->string(QStringLiteral("common.noMatchesFor"), QStringLiteral("No matches for “%1”"));
  return QString(text).replace(QStringLiteral("%1"), m_query);
}

int SearchList::cursorIndex() const { return m_cursor->isCursorActive() && !m_items.isEmpty() ? m_cursor->selectedIndex() : -1; }

SearchItem SearchList::currentItem() const {
  const int i = cursorIndex();
  return i >= 0 && i < m_items.size() ? m_items[i] : SearchItem();
}

// Sees every key before the filter line; what it leaves is typed there.
bool SearchList::handleKey(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape) {
    if (!m_query.isEmpty()) setQuery(QString());
    else emit closeRequested();
    e->accept();
    return true;
  }
  if (e->text() == QLatin1String(" ") || !CursorModel::printable(e).isEmpty()) return false;
  if (e->key() == Qt::Key_Backspace) return false;
  return m_cursor->handleKey(e);
}

void SearchList::focusInEvent(QFocusEvent *e) {
  m_input->setFocus(e->reason());
  QWidget::focusInEvent(e);
}

// ----------------------------------------------------------------------------------- geometry

QMarginsF SearchList::insets() const {
  const Tokens &tk = shown();
  const Border b = tk.border(QStringLiteral("surfaces.menu.border"));
  const int pad = tk.spacing("panelPadding");
  return QMarginsF(b.left + pad, b.top + pad, b.right + pad, b.bottom + pad);
}

int SearchList::headerHeight() const {
  const Tokens &tk = shown();
  return qMax(tk.space(34), tk.fontSize("heading") + tk.spacing("controlPaddingY") * 2);
}

QRectF SearchList::headerRect() const {
  const QMarginsF m = insets();
  return QRectF(m.left(), m.top(), width() - m.left() - m.right(), headerHeight());
}

QRectF SearchList::bodyRect() const {
  const QRectF h = headerRect();
  const qreal y = h.bottom() + shown().spacing("md");
  return QRectF(h.x(), y, h.width(), height() - insets().bottom() - y);
}

qreal SearchList::listWidth() const {
  const QRectF b = bodyRect();
  return detailShown() ? b.width() - detailWidth() - shown().spacing("xxl") * 2 - 1 : b.width();
}

QRectF SearchList::detailRect() const {
  const QRectF b = bodyRect();
  const qreal x = listWidth() + shown().spacing("xxl") * 2 + 1;
  return QRectF(b.x() + x, b.y(), b.width() - x, b.height());
}

QSize SearchList::sizeHint() const { return QSize(shown().space(480), shown().space(360)); }

void SearchList::sync() {
  m_input->setPlaceholderText(placeholderText());
  m_empty->setGlyph(m_emptyGlyph);
  m_empty->setTitle(emptyTitle());
  m_empty->setVisible(m_items.isEmpty());
  m_scroll->setVisible(!m_items.isEmpty());
  place();
  update();
}

void SearchList::place() {
  const Tokens &tk = shown();
  const QRectF header = headerRect();
  const int inputH = m_input->sizeHint().height();
  m_input->setGeometry(QRectF(header.x(), header.y() + Paint::centered(header.height(), inputH), header.width(), inputH).toRect());

  const QRectF body = bodyRect();
  m_scroll->setSpacing(tk.spacing("xxs"));
  m_scroll->setMaxHeight(qMax(0, int(body.height())));
  m_scroll->setGeometry(QRectF(body.x(), body.y(), qMax<qreal>(0, listWidth()), qMax<qreal>(0, body.height())).toRect());

  if (m_detail) {
    const QRectF d = detailRect();
    const int h = m_detail->hasHeightForWidth() ? m_detail->heightForWidth(int(d.width())) : m_detail->sizeHint().height();
    m_detail->setGeometry(QRectF(d.x(), d.y(), d.width(), qMin<qreal>(h, d.height())).toRect());
    m_detail->setVisible(detailShown());
  }

  const qreal eh = m_empty->implicitHeightFor(body.width());
  m_empty->setGeometryF(QRectF(body.x(), body.y() + Paint::centered(body.height(), eh), body.width(), eh));
}

void SearchList::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  Paint::surface(p, QRectF(rect()), tk.radius(), tk.color(QStringLiteral("surfaces.menu.background")),
                 tk.border(QStringLiteral("surfaces.menu.border")));
  if (!detailShown()) return;

  // The detail pane: a 1px track rule, then the cursor item.
  const QRectF body = bodyRect();
  p.fillRect(QRectF(body.x() + listWidth() + tk.spacing("xxl"), body.y(), 1, body.height()), tk.track());
  if (m_detail) return;
  const QRectF d = detailRect();
  p.save();
  p.setClipRect(d);
  const SearchItem item = currentItem();
  const QFont title = tk.font("title", true);
  const qreal titleH = Paint::lineHeight(title);
  const qreal topPad = tk.spacing("rowPaddingX") / 2.0;
  Paint::textIn(p, QRectF(d.x(), d.y() + topPad, d.width(), titleH), Qt::AlignLeft | Qt::AlignTop, title,
                tk.color(QStringLiteral("surfaces.menu.text")), item.title);
  Paint::wrapped(p, QRectF(d.x(), d.y() + topPad + titleH + tk.spacing("sm"), d.width(), d.height()), tk.font("body"), tk.textDim(), item.detail);
  p.restore();
}

}  // namespace Omarchy
