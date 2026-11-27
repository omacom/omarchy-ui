// Table and Tree (spec/components/data.md), drawn as Table.qml and Tree.qml: one widget paints
// the header and every row, so the cells land where the Qt Quick kit's Repeaters put them.

#include "Omarchy/Data.h"

#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <functional>
#include <cmath>

#include "Omarchy/Paint.h"

namespace Omarchy {

namespace {

QString cp(uint c) { return QString::fromUcs4(reinterpret_cast<const char32_t *>(&c), 1); }

// The row ladder every data component shares: the hover fill on the cursor, else the selected
// fill, else nothing.
QColor rowFill(const Tokens &tk, bool cursor, bool selected) {
  if (cursor) return tk.fill(State::Hover);
  if (selected) return tk.fill(State::Selected);
  return Qt::transparent;
}

int ceilPx(qreal v) { return int(std::ceil(v - 1e-6)); }

}  // namespace

// ----------------------------------------------------------------------------------------- Table

Table::Table(QWidget *parent) : Control(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void Table::setColumns(const QList<Column> &columns) {
  m_columns = columns;
  updateGeometry();
  update();
}

QList<Table::Column> Table::columns() const {
  if (!m_model) return m_columns;
  // A model fills the titles the app left empty, and gives the columns when there are none.
  QList<Column> out = m_columns;
  for (int i = 0; i < m_modelTitles.size(); ++i) {
    if (i >= out.size()) out.append(Column{m_modelTitles.at(i)});
    else if (out[i].title.isEmpty()) out[i].title = m_modelTitles.at(i);
  }
  return out;
}

void Table::setRows(const QList<QStringList> &rows) {
  m_rows = rows;
  if (m_model) return;
  updateGeometry();
  update();
}

QList<QStringList> Table::rows() const { return m_model ? m_modelRows : m_rows; }
int Table::rowCount() const { return int(m_model ? m_modelRows.size() : m_rows.size()); }

void Table::setModel(QAbstractItemModel *model) {
  for (const auto &c : m_modelConnections) disconnect(c);
  m_modelConnections.clear();
  m_model = model;
  if (model) {
    auto r = [this] { reload(); };
    m_modelConnections << connect(model, &QAbstractItemModel::dataChanged, this, r)
                       << connect(model, &QAbstractItemModel::rowsInserted, this, r)
                       << connect(model, &QAbstractItemModel::rowsRemoved, this, r)
                       << connect(model, &QAbstractItemModel::rowsMoved, this, r)
                       << connect(model, &QAbstractItemModel::columnsInserted, this, r)
                       << connect(model, &QAbstractItemModel::columnsRemoved, this, r)
                       << connect(model, &QAbstractItemModel::modelReset, this, r)
                       << connect(model, &QAbstractItemModel::layoutChanged, this, r)
                       << connect(model, &QAbstractItemModel::headerDataChanged, this, r)
                       << connect(model, &QObject::destroyed, this, [this] { setModel(nullptr); });
  }
  reload();
}

void Table::reload() {
  m_modelRows.clear();
  m_modelTitles.clear();
  if (m_model) {
    const int cols = m_model->columnCount();
    for (int c = 0; c < cols; ++c) m_modelTitles << m_model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
    for (int r = 0, n = m_model->rowCount(); r < n; ++r) {
      QStringList row;
      for (int c = 0; c < cols; ++c) row << m_model->index(r, c).data(Qt::DisplayRole).toString();
      m_modelRows << row;
    }
  }
  setScroll(m_scroll);
  updateGeometry();
  update();
}

void Table::setSortColumn(int column) {
  m_sortColumn = column;
  update();
}
void Table::setSortAscending(bool on) {
  m_sortAscending = on;
  update();
}
void Table::setSelectedRow(int row) {
  if (m_selected == row) return;
  m_selected = row;
  update();
  emit selectedRowChanged(row);
}
void Table::setCursorRow(int row) {
  if (m_cursor == row) return;
  m_cursor = row;
  update();
  emit cursorRowChanged(row);
}
void Table::setSeparators(bool on) {
  m_separators = on;
  update();
}

int Table::rowHeight() const { return t().spacing("popupRowHeight"); }

// Fixed columns as given, the rest share what is left (floored).
QList<int> Table::columnWidths() const {
  const QList<Column> cols = columns();
  int fixed = 0, flex = 0;
  for (const Column &c : cols) {
    if (c.width > 0) fixed += c.width;
    else ++flex;
  }
  const int share = flex > 0 ? qMax(0, int(std::floor(qreal(width() - fixed) / flex))) : 0;
  QList<int> out;
  for (const Column &c : cols) out << (c.width > 0 ? c.width : share);
  return out;
}

QSize Table::sizeHint() const { return QSize(400, rowHeight() * (1 + rowCount())); }

qreal Table::bodyHeight() const { return qMax(0, height() - rowHeight()); }

void Table::setScroll(qreal y) {
  const qreal most = qMax<qreal>(0, rowCount() * rowHeight() - bodyHeight());
  y = qBound<qreal>(0, y, most);
  if (y == m_scroll) return;
  m_scroll = y;
  update();
}

// ListView.Contain: the least scroll that shows the row.
void Table::ensureVisible(int row) {
  if (row < 0) return;
  const qreal top = row * rowHeight(), bottom = top + rowHeight();
  if (top < m_scroll) setScroll(top);
  else if (bottom > m_scroll + bodyHeight()) setScroll(bottom - bodyHeight());
}

QRect Table::headerRect(int column) const {
  const QList<int> w = columnWidths();
  if (column < 0 || column >= w.size()) return QRect();
  int x = 0;
  for (int i = 0; i < column; ++i) x += w.at(i);
  return QRect(x, 0, w.at(column), rowHeight());
}

QRect Table::rowRect(int row) const {
  if (row < 0 || row >= rowCount()) return QRect();
  return QRect(0, int(rowHeight() + row * rowHeight() - m_scroll), width(), rowHeight());
}

int Table::rowAt(const QPointF &pos) const {
  const int rh = rowHeight();
  if (pos.y() < rh || pos.x() < 0 || pos.x() >= width() || pos.y() >= height()) return -1;
  const int row = int(std::floor((pos.y() - rh + m_scroll) / rh));
  return row >= 0 && row < rowCount() ? row : -1;
}

int Table::headerAt(const QPointF &pos) const {
  if (pos.y() < 0 || pos.y() >= rowHeight()) return -1;
  const QList<int> w = columnWidths();
  qreal x = 0;
  for (int i = 0; i < w.size(); ++i) {
    if (pos.x() >= x && pos.x() < x + w.at(i)) return i;
    x += w.at(i);
  }
  return -1;
}

void Table::moveCursor(int delta) {
  const int n = rowCount();
  if (n == 0) return;
  const int from = m_cursor < 0 ? (delta > 0 ? -1 : n) : m_cursor;
  setCursorRow(qBound(0, from + delta, n - 1));
  ensureVisible(m_cursor);
}

void Table::activateRow(int row) {
  if (row < 0 || row >= rowCount()) return;
  setSelectedRow(row);
  emit rowActivated(row);
}

void Table::keyPressEvent(QKeyEvent *e) {
  const int page = qMax(1, int(std::floor(bodyHeight() / rowHeight())));
  switch (e->key()) {
  case Qt::Key_Up: moveCursor(-1); break;
  case Qt::Key_Down: moveCursor(1); break;
  case Qt::Key_Home:
    setCursorRow(rowCount() > 0 ? 0 : -1);
    ensureVisible(m_cursor);
    break;
  case Qt::Key_End:
    setCursorRow(rowCount() - 1);
    ensureVisible(m_cursor);
    break;
  case Qt::Key_PageDown: moveCursor(page); break;
  case Qt::Key_PageUp: moveCursor(-page); break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space: activateRow(m_cursor); break;
  default: Control::keyPressEvent(e); return;
  }
  e->accept();
}

void Table::hover(const QPointF &pos) {
  const int row = rowAt(pos);
  const bool was = m_bodyHover;
  m_bodyHover = row >= 0;
  // The pointer moves the cursor as it enters a row (a row it sits on keeps what keys did).
  if (row >= 0 && row != m_hoverRow) setCursorRow(row);
  m_hoverRow = row;
  if (was != m_bodyHover) update();
}

void Table::mouseMoveEvent(QMouseEvent *e) {
  hover(e->position());
  Control::mouseMoveEvent(e);
}

void Table::leaveEvent(QEvent *e) {
  m_bodyHover = false;
  m_hoverRow = -1;
  Control::leaveEvent(e);
}

void Table::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) return Control::mousePressEvent(e);
  m_pressHeader = headerAt(e->position());
  m_pressRow = rowAt(e->position());
  Control::mousePressEvent(e);
}

void Table::mouseReleaseEvent(QMouseEvent *e) {
  const int header = m_pressHeader, row = m_pressRow;
  m_pressHeader = m_pressRow = -1;
  Control::mouseReleaseEvent(e);
  if (e->button() != Qt::LeftButton) return;
  if (header >= 0 && headerAt(e->position()) == header) emit sortRequested(header);
  else if (row >= 0 && rowAt(e->position()) == row) {
    setFocus(Qt::MouseFocusReason);
    activateRow(row);
  }
}

void Table::wheelEvent(QWheelEvent *e) {
  const qreal before = m_scroll;
  setScroll(m_scroll - e->angleDelta().y() / 120.0 * rowHeight() * 3);
  if (m_scroll != before) e->accept();
  else e->ignore();
}

void Table::resizeEvent(QResizeEvent *e) {
  Control::resizeEvent(e);
  setScroll(m_scroll);
}

void Table::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const int rh = rowHeight(), pad = tk.spacing("controlPaddingX"), hair = tk.spacing("hairline");
  const qreal W = width();
  const QList<Column> cols = columns();
  const QList<int> widths = columnWidths();

  // ---- Header: a Row of cells, each a Row of the title and the sort chevron.
  const QFont caption = tk.font("caption", true), chevronFont = tk.font("caption");
  const qreal gap = tk.spacing("xs");
  const QColor dim = tk.textDim();
  int cx = 0;
  for (int i = 0; i < cols.size(); ++i) {
    const Column &c = cols.at(i);
    const int cw = widths.at(i);
    const bool sorted = m_sortColumn == i;
    const QString chevron = cp(m_sortAscending ? 0xF0143 : 0xF0140);
    const QSizeF gs = sorted ? Paint::textSize(chevronFont, chevron) : QSizeF();
    const QSizeF ts = Paint::textSize(caption, c.title);
    const qreal titleW = qMax<qreal>(0, qMin(ts.width(), cw - pad * 2 - (sorted ? gs.width() + gap : 0)));
    const qreal implicitW = titleW + (sorted ? gap + gs.width() : 0);
    const qreal contentW = qMin(implicitW, qreal(cw - pad * 2));
    const qreal contentX = c.alignRight ? cw - pad - contentW : pad;
    const qreal contentH = qMax(ts.height(), gs.height());
    const qreal contentY = Paint::centered(rh, contentH);
    // RightToLeft lays the title at the right end, the chevron before it.
    const qreal titleX = c.alignRight ? contentW - titleW : 0;
    const qreal chevronX = c.alignRight ? contentW - titleW - gap - gs.width() : titleW + gap;
    Paint::text(p, QPointF(cx + contentX + titleX, contentY + Paint::centered(contentH, ts.height())), caption, dim,
                Paint::elided(caption, c.title, titleW));
    if (sorted)
      Paint::text(p, QPointF(cx + contentX + chevronX, contentY + Paint::centered(contentH, gs.height())), chevronFont, dim, chevron);
    cx += cw;
  }
  p.fillRect(QRectF(0, rh - hair, W, hair), tk.track());

  // ---- Body, under the header, scrolled.
  p.save();
  p.setClipRect(QRect(0, rh, width(), qMax(0, height() - rh)));
  const QList<QStringList> data = rows();
  const int n = int(data.size());
  const bool showCursor = hasCursor() || hasFocus() || m_bodyHover;
  const QFont body = tk.font("body"), bold = tk.font("body", true);
  const qreal lh = Paint::lineHeight(body), lhBold = Paint::lineHeight(bold);
  const int first = qMax(0, int(m_scroll / rh)), last = qMin(n - 1, int((m_scroll + bodyHeight()) / rh));
  for (int r = first; r <= last; ++r) {
    const qreal y = rh + r * rh - m_scroll;
    const bool selected = m_selected == r, cursor = showCursor && m_cursor == r;
    Paint::surface(p, QRectF(0, y, W, rh), tk.radius(), rowFill(tk, cursor, selected));
    const QFont &f = selected ? bold : body;
    const qreal h = selected ? lhBold : lh;
    const QColor ink = selected ? tk.selectedText() : tk.foreground();
    const QStringList &cells = data.at(r);
    int x = 0;
    for (int i = 0; i < cols.size(); ++i) {
      const int cw = widths.at(i);
      const QRectF box(x + pad, y + Paint::centered(rh, h), qMax(0, cw - pad * 2), h);
      Paint::textIn(p, box, cols.at(i).alignRight ? Qt::AlignRight : Qt::AlignLeft, f, ink, cells.value(i));
      x += cw;
    }
    const bool nextHighlighted = (m_selected == r + 1) || (showCursor && m_cursor == r + 1);
    if (m_separators && r < n - 1 && !selected && !cursor && !nextHighlighted)
      p.fillRect(QRectF(0, y + rh - hair, W, hair), tk.track());
  }
  p.restore();
}

// ------------------------------------------------------------------------------------------ Tree

Tree::Tree(QWidget *parent) : Control(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void Tree::setNodes(const QList<TreeNode> &nodes) {
  m_nodes = nodes;
  rebuild();
}

void Tree::setExpanded(const QStringList &ids) {
  m_expanded = ids;
  rebuild();
}

void Tree::setSelectedId(const QString &id) {
  m_selected = id;
  update();
}

void Tree::setCursorId(const QString &id) {
  if (m_cursor == id) return;
  m_cursor = id;
  update();
  emit cursorIdChanged(id);
}

void Tree::rebuild() {
  m_rows.clear();
  std::function<void(const QList<TreeNode> &, int)> walk = [&](const QList<TreeNode> &list, int depth) {
    for (const TreeNode &n : list) {
      const bool branch = !n.children.isEmpty();
      const bool open = branch && m_expanded.contains(n.id);
      m_rows.append(Row{n.id, n.label, n.glyph, depth, branch, open});
      if (open) walk(n.children, depth + 1);
    }
  };
  walk(m_nodes, 0);
  setScroll(m_scroll);
  updateGeometry();
  update();
}

int Tree::rowHeight() const { return t().spacing("popupRowHeight"); }

QSize Tree::sizeHint() const { return QSize(240, int(m_rows.size()) * rowHeight()); }

int Tree::indexOf(const QString &id) const {
  for (int i = 0; i < m_rows.size(); ++i)
    if (m_rows.at(i).id == id) return i;
  return -1;
}

QRect Tree::rowRect(const QString &id) const {
  const int i = indexOf(id);
  if (i < 0) return QRect();
  return QRect(0, int(i * rowHeight() - m_scroll), width(), rowHeight());
}

QRect Tree::chevronRect(const QString &id) const {
  const int i = indexOf(id);
  if (i < 0) return QRect();
  const Tokens &tk = t();
  const int x = tk.spacing("md") + m_rows.at(i).depth * tk.spacing("huge");
  return QRect(x, int(i * rowHeight() - m_scroll), tk.spacing("xxxl"), rowHeight());
}

int Tree::rowAt(const QPointF &pos) const {
  if (pos.x() < 0 || pos.x() >= width() || pos.y() < 0 || pos.y() >= height()) return -1;
  const int i = int(std::floor((pos.y() + m_scroll) / rowHeight()));
  return i >= 0 && i < m_rows.size() ? i : -1;
}

bool Tree::onChevron(int row, const QPointF &pos) const {
  return row >= 0 && m_rows.at(row).branch && chevronRect(m_rows.at(row).id).contains(pos.toPoint());
}

void Tree::setScroll(qreal y) {
  const qreal most = qMax<qreal>(0, m_rows.size() * rowHeight() - height());
  y = qBound<qreal>(0, y, most);
  if (y == m_scroll) return;
  m_scroll = y;
  update();
}

void Tree::ensureVisible(int row) {
  if (row < 0) return;
  const qreal top = row * rowHeight(), bottom = top + rowHeight();
  if (top < m_scroll) setScroll(top);
  else if (bottom > m_scroll + height()) setScroll(bottom - height());
}

void Tree::moveCursor(int delta) {
  const int n = int(m_rows.size());
  if (n == 0) return;
  const int at = indexOf(m_cursor);
  const int from = at < 0 ? (delta > 0 ? -1 : n) : at;
  const int next = qBound(0, from + delta, n - 1);
  setCursorId(m_rows.at(next).id);
  ensureVisible(next);
}

void Tree::keyPressEvent(QKeyEvent *e) {
  const int at = indexOf(m_cursor);
  const Row *r = at >= 0 ? &m_rows.at(at) : nullptr;
  switch (e->key()) {
  case Qt::Key_Up: moveCursor(-1); break;
  case Qt::Key_Down: moveCursor(1); break;
  case Qt::Key_Right:
    if (r && r->branch && !r->open) emit toggled(r->id);
    break;
  case Qt::Key_Left:
    if (r && r->branch && r->open) emit toggled(r->id);
    break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space:
    if (!m_cursor.isEmpty()) emit activated(m_cursor);
    break;
  default: Control::keyPressEvent(e); return;
  }
  e->accept();
}

void Tree::hover(const QPointF &pos) {
  const int row = rowAt(pos);
  const bool was = m_bodyHover;
  m_bodyHover = row >= 0;
  if (row >= 0 && row != m_hoverRow) setCursorId(m_rows.at(row).id);
  m_hoverRow = row;
  if (was != m_bodyHover) update();
}

void Tree::mouseMoveEvent(QMouseEvent *e) {
  hover(e->position());
  Control::mouseMoveEvent(e);
}

void Tree::leaveEvent(QEvent *e) {
  m_bodyHover = false;
  m_hoverRow = -1;
  Control::leaveEvent(e);
}

void Tree::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) {
    m_pressRow = rowAt(e->position());
    m_pressChevron = onChevron(m_pressRow, e->position());
  }
  Control::mousePressEvent(e);
}

void Tree::mouseReleaseEvent(QMouseEvent *e) {
  const int row = m_pressRow;
  const bool chevron = m_pressChevron, swallow = m_swallowRelease;
  m_pressRow = -1;
  m_pressChevron = m_swallowRelease = false;
  Control::mouseReleaseEvent(e);
  if (e->button() != Qt::LeftButton || row < 0 || rowAt(e->position()) != row || swallow) return;
  // The rows can change under the signal: copy the id first.
  const QString id = m_rows.at(row).id;
  if (chevron) {
    if (onChevron(row, e->position())) emit toggled(id);
    return;
  }
  setFocus(Qt::MouseFocusReason);
  emit activated(id);
}

// A double click on a branch row folds it (the second click doesn't choose again); on the
// chevron it is two chevron clicks, as Qt Quick's MouseArea without a double-click handler.
void Tree::mouseDoubleClickEvent(QMouseEvent *e) {
  mousePressEvent(e);
  if (e->button() != Qt::LeftButton || m_pressRow < 0 || m_pressChevron) return;
  m_swallowRelease = true;
  if (m_rows.at(m_pressRow).branch) emit toggled(m_rows.at(m_pressRow).id);
}

void Tree::wheelEvent(QWheelEvent *e) {
  const qreal before = m_scroll;
  setScroll(m_scroll - e->angleDelta().y() / 120.0 * rowHeight() * 3);
  if (m_scroll != before) e->accept();
  else e->ignore();
}

void Tree::resizeEvent(QResizeEvent *e) {
  Control::resizeEvent(e);
  setScroll(m_scroll);
}

void Tree::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const int rh = rowHeight();
  const qreal W = width();
  const int md = tk.spacing("md"), huge = tk.spacing("huge"), slotW = tk.spacing("xxxl"), sm = tk.spacing("sm");
  const int iconPx = tk.fontSize("icon");
  const QFont chevronFont = tk.font("iconSmall"), glyphFont = tk.font("icon"), body = tk.font("body"), bold = tk.font("body", true);
  const bool showCursor = hasCursor() || hasFocus() || m_bodyHover;
  const int n = int(m_rows.size());
  const int first = qMax(0, int(m_scroll / rh)), last = qMin(n - 1, int((m_scroll + height()) / rh));
  for (int i = first; i <= last; ++i) {
    const Row &r = m_rows.at(i);
    const qreal y = i * rh - m_scroll;
    const bool selected = m_selected == r.id, cursor = showCursor && m_cursor == r.id;
    Paint::surface(p, QRectF(0, y, W, rh), tk.radius(), rowFill(tk, cursor, selected));
    const qreal slotX = md + r.depth * huge;
    if (r.branch) {
      const QString chevron = cp(r.open ? 0xF0140 : 0xF0142);
      Paint::opticalGlyph(p, QRectF(slotX, y, slotW, rh), chevronFont, tk.textDim(), chevron);
    }
    const qreal glyphX = slotX + slotW + sm;
    if (!r.glyph.isEmpty()) {
      Paint::opticalGlyph(p, QRectF(glyphX, y, iconPx, rh), glyphFont, selected ? tk.selectedText() : tk.textDim(), r.glyph);
    }
    const qreal labelX = glyphX + (r.glyph.isEmpty() ? 0 : iconPx + md);
    const QFont &f = selected ? bold : body;
    const qreal lh = Paint::lineHeight(f);
    Paint::textIn(p, QRectF(labelX, y + Paint::centered(rh, lh), qMax<qreal>(0, W - labelX - md), lh), Qt::AlignLeft, f,
                  selected ? tk.selectedText() : tk.foreground(), r.label);
  }
}

}  // namespace Omarchy
