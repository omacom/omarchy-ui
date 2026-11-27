// ListRow: qt/qml/Omarchy/Ui/ListRow.qml on CursorSurface. Fractional geometry as Qt Quick's
// Column and Row positioners give it; the leading, trailing and expansion widgets are children.

#include <QEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QVariant>
#include <cmath>

#include "Omarchy/Lists.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

// A child's exact implicit size: the kit's `implicitSize` property when it has one (Badge,
// Avatar, ...), else its size hint.
static QSizeF implicitOf(const QWidget *w) {
  if (!w) return QSizeF();
  const QVariant v = w->property("implicitSize");
  if (v.isValid() && v.canConvert<QSizeF>()) return v.toSizeF();
  return QSizeF(w->sizeHint());
}

static void placeChild(QWidget *w, const QRectF &r) {
  if (auto *c = qobject_cast<Control *>(w)) c->setGeometryF(r);
  else w->setGeometry(QRect(int(std::lround(r.x())), int(std::lround(r.y())), int(std::ceil(r.width() - 1e-6)), int(std::ceil(r.height() - 1e-6))));
}

static bool shownChild(const QWidget *w) { return w && !w->isHidden(); }

struct ListRow::Geometry {
  qreal width = 0, height = 0;
  QRectF body;  // in the row
  // In the body:
  qreal leadW = 0, leadH = 0, leadY = 0;
  QSizeF glyph;
  QRectF leading;
  qreal infoY = 0, infoW = 0, infoImplicitW = 0;
  QList<qreal> lineY, lineH;
  qreal trailX = 0, trailY = 0, trailW = 0, trailH = 0;
  QRectF slot, trailing, value;
  QRectF expansion;  // in the row
};

ListRow::ListRow(const QString &glyph, const QString &title, QWidget *parent) : Control(parent), m_glyph(glyph), m_title(title) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ListRow::setGlyph(const QString &g) {
  m_glyph = g;
  updateGeometry();
  place();
  update();
}
void ListRow::setGlyphSize(int px) {
  m_glyphSize = px;
  updateGeometry();
  place();
  update();
}
void ListRow::setGlyphColor(const QColor &c) {
  m_glyphColor = c;
  update();
}
void ListRow::adopt(QWidget *&slot, QWidget *w) {
  if (slot == w) return;
  if (slot) slot->deleteLater();
  slot = w;
  if (w) {
    w->setParent(this);
    w->show();
  }
  updateGeometry();
  place();
  update();
}
void ListRow::setLeading(QWidget *w) {
  QWidget *slot = m_leading;
  adopt(slot, w);
  m_leading = slot;
  place();
}
void ListRow::setTitle(const QString &t) {
  m_title = t;
  updateGeometry();
  update();
}
void ListRow::setSubtitle(const QString &t) {
  m_subtitle = t;
  updateGeometry();
  place();
  update();
}
void ListRow::setStatus(const QString &s, const QString &tone) {
  m_status = s;
  m_statusTone = tone;
  updateGeometry();
  place();
  update();
}
void ListRow::setStatusTone(const QString &tone) {
  m_statusTone = tone;
  update();
}
void ListRow::setValue(const QString &v) {
  m_value = v;
  updateGeometry();
  place();
  update();
}
void ListRow::setTrailing(QWidget *w) {
  QWidget *slot = m_trailing;
  adopt(slot, w);
  m_trailing = slot;
  place();
}
void ListRow::setTrailingGlyph(const QString &g, const QString &tone) {
  m_trailingGlyph = g;
  m_trailingTone = tone;
  updateGeometry();
  place();
  update();
}
void ListRow::setActionGlyph(const QString &g, bool urgent) {
  m_actionGlyph = g;
  m_actionUrgent = urgent;
  updateGeometry();
  place();
  update();
}
void ListRow::setActionUrgent(bool on) {
  m_actionUrgent = on;
  update();
}
void ListRow::setActionTip(const QString &tip) { m_actionTip = tip; }
void ListRow::setActionCursor(bool on) {
  m_actionCursor = on;
  update();
}
void ListRow::setCurrent(bool on) {
  m_current = on;
  update();
}
void ListRow::setExpansion(QWidget *w) {
  QWidget *slot = m_expansion;
  adopt(slot, w);
  m_expansion = slot;
  QSizePolicy sp = sizePolicy();
  sp.setHeightForWidth(w != nullptr);
  setSizePolicy(sp);
  place();
}

bool ListRow::isActionShown() const {
  return !m_actionGlyph.isEmpty() && isEnabled() && (hasCursor() || m_actionCursor || isHovered());
}

ListRow::Geometry ListRow::geometry(qreal width, qreal height) const {
  const Tokens &tk = t();
  Geometry g;
  const qreal padX = tk.spacing("controlPaddingX");
  const qreal rowPad = tk.spacing("rowPaddingX");
  const int iconLarge = tk.fontSize("iconLarge");

  // The leading slot.
  const bool leadShown = shownChild(m_leading) || !m_glyph.isEmpty();
  g.leadW = leadShown ? iconLarge + tk.space(10) : 0;
  const bool glyphShown = !shownChild(m_leading) && !m_glyph.isEmpty();
  if (glyphShown) g.glyph = Paint::textSize(tk.fontPx(m_glyphSize > 0 ? m_glyphSize : tk.fontSize("title")), m_glyph);
  QSizeF lead;
  if (shownChild(m_leading)) lead = implicitOf(m_leading);
  g.leadH = qMax(glyphShown ? g.glyph.height() : 0.0, lead.height());

  // The info column.
  const qreal titleH = Paint::lineHeight(tk.font("body", true));
  const qreal capH = Paint::lineHeight(tk.font("caption"));
  qreal infoH = 0;
  g.lineY << 0;
  g.lineH << titleH;
  infoH = titleH;
  g.infoImplicitW = Paint::textWidth(tk.font("body", true), m_title);
  for (const QString *s : {&m_subtitle, &m_status}) {
    if (s->isEmpty()) {
      g.lineY << 0;
      g.lineH << 0;
      continue;
    }
    infoH += tk.space(1);
    g.lineY << infoH;
    g.lineH << capH;
    infoH += capH;
    g.infoImplicitW = qMax(g.infoImplicitW, Paint::textWidth(tk.font("caption"), *s));
  }

  // The trailing row, right to left: the end slot, the trailing widget, the value.
  const bool slotShown = !m_actionGlyph.isEmpty() || !m_trailingGlyph.isEmpty();
  const bool hasValue = !m_value.isEmpty() && !m_trailing;
  const qreal gap = tk.space(8);
  const qreal slot = tk.space(22);
  QSizeF trail = shownChild(m_trailing) ? implicitOf(m_trailing) : QSizeF();
  QSizeF value = hasValue ? Paint::textSize(tk.font("bodySmall"), m_value) : QSizeF();
  struct Part {
    QSizeF size;
    QRectF *out;
  };
  QList<Part> parts;
  if (slotShown) parts << Part{QSizeF(slot, slot), &g.slot};
  if (trail.width() > 0 && trail.height() > 0) parts << Part{trail, &g.trailing};
  if (hasValue && value.width() > 0) parts << Part{value, &g.value};
  qreal implicitW = 0;
  for (const Part &p : parts) {
    implicitW += p.size.width();
    g.trailH = qMax(g.trailH, p.size.height());
  }
  if (parts.size() > 1) implicitW += gap * (parts.size() - 1);
  const bool hasAny = hasValue || m_trailing || slotShown;
  g.trailW = hasAny ? implicitW + gap : 0;

  // The body: tallest of the three; width the row's less its padding.
  const qreal bodyH = qMax(g.leadH, qMax(infoH, g.trailH));
  const qreal bodyImplicitW = g.leadW + g.infoImplicitW + g.trailW;
  g.width = width >= 0 ? width : bodyImplicitW + padX * 2;
  const qreal bodyW = g.width - padX * 2;

  qreal expH = 0;
  if (shownChild(m_expansion)) {
    const int ew = qMax(0, int(std::lround(g.width - padX * 2)));
    expH = m_expansion->hasHeightForWidth() ? m_expansion->heightForWidth(ew) : implicitOf(m_expansion).height();
  }
  g.height = bodyH + rowPad + (shownChild(m_expansion) ? expH + tk.space(4) : 0);
  const qreal bodyY = shownChild(m_expansion) ? rowPad / 2 : std::round(((height >= 0 ? height : g.height) - bodyH) / 2);
  g.body = QRectF(padX, bodyY, bodyW, bodyH);
  if (shownChild(m_expansion)) g.expansion = QRectF(padX, bodyY + bodyH + tk.space(4), g.width - padX * 2, expH);

  g.leadY = Paint::centered(bodyH, g.leadH);
  if (shownChild(m_leading)) g.leading = QRectF(std::round((iconLarge - lead.width()) / 2), g.leadY + Paint::centered(g.leadH, lead.height()), lead.width(), lead.height());
  g.trailX = bodyW - g.trailW;
  g.trailY = Paint::centered(bodyH, g.trailH);
  qreal x = g.trailW;
  for (const Part &p : parts) {
    x -= p.size.width();
    *p.out = QRectF(g.trailX + x, g.trailY + Paint::centered(g.trailH, p.size.height()), p.size.width(), p.size.height());
    x -= gap;
  }
  g.infoW = g.trailX - g.leadW;
  g.infoY = Paint::centered(bodyH, infoH);
  return g;
}

QSizeF ListRow::implicitSize(qreal width) const {
  const Geometry g = geometry(width);
  return QSizeF(g.width, g.height);
}

QSize ListRow::sizeHint() const {
  const QSizeF s = implicitSize();
  return QSize(int(std::ceil(s.width() - 1e-6)), int(std::ceil(s.height() - 1e-6)));
}

int ListRow::heightForWidth(int w) const { return int(std::ceil(implicitSize(w).height() - 1e-6)); }

QRectF ListRow::actionRect() const {
  const QRectF box = paintRect();
  const Geometry g = geometry(box.width(), box.height());
  if (g.slot.isEmpty()) return QRectF();
  return g.slot.translated(box.topLeft() + g.body.topLeft());
}

void ListRow::place() {
  const QRectF box = paintRect();
  const Geometry g = geometry(box.width(), box.height());
  const QPointF body = box.topLeft() + g.body.topLeft();
  if (shownChild(m_leading)) placeChild(m_leading, g.leading.translated(body));
  if (shownChild(m_trailing)) placeChild(m_trailing, g.trailing.translated(body));
  if (shownChild(m_expansion)) placeChild(m_expansion, g.expansion.translated(box.topLeft()));
}

QColor ListRow::toneColor(const QString &tone) const {
  const Tokens &tk = t();
  if (!isEnabled()) return tk.textFaint();
  if (tone.isEmpty()) return tk.textDim();
  return Badge::toneColor(tk, tone, tk.textDim());
}

void ListRow::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF box = paintRect();
  const Geometry g = geometry(box.width(), box.height());
  const bool enabled = isEnabled();

  // CursorSurface: the cursor wins over current; never hover.
  if (hasCursor()) Paint::surface(p, box, tk.radius(), tk.fill(State::Hover), tk.border(State::Hover));
  else if (m_current) Paint::surface(p, box, tk.radius(), tk.fill(State::Selected), tk.border(State::Selected));

  const QPointF o = box.topLeft() + g.body.topLeft();
  const QColor fg = tk.foreground();
  const QColor faint = tk.textFaint();
  const QColor dim = enabled ? tk.textDim() : faint;

  if (!shownChild(m_leading) && !m_glyph.isEmpty()) {
    const QFont f = tk.fontPx(m_glyphSize > 0 ? m_glyphSize : tk.fontSize("title"));
    const qreal x = std::round((tk.fontSize("iconLarge") - g.glyph.width()) / 2);
    const QColor c = !enabled ? faint : (m_glyphColor.isValid() ? m_glyphColor : fg);
    Paint::text(p, o + QPointF(x, g.leadY + Paint::centered(g.leadH, g.glyph.height())), f, c, m_glyph);
  }

  const qreal ix = o.x() + g.leadW, iy = o.y() + g.infoY;
  const QFont title = tk.font("body", true), cap = tk.font("caption");
  Paint::text(p, QPointF(ix, iy), title, enabled ? fg : faint, Paint::elided(title, m_title, g.infoW));
  if (!m_subtitle.isEmpty()) Paint::text(p, QPointF(ix, iy + g.lineY[1]), cap, dim, Paint::elided(cap, m_subtitle, g.infoW));
  if (!m_status.isEmpty()) Paint::text(p, QPointF(ix, iy + g.lineY[2]), cap, toneColor(m_statusTone), Paint::elided(cap, m_status, g.infoW));

  if (!g.value.isEmpty()) Paint::text(p, o + g.value.topLeft(), tk.font("bodySmall"), dim, m_value);

  if (!g.slot.isEmpty()) {
    const QRectF slot = g.slot.translated(o);
    if (m_actionCursor && enabled) {
      const QColor ink = m_actionUrgent ? tk.urgent() : fg;
      const Border hb = tk.border(State::Hover);
      Paint::surface(p, slot, tk.smallRadius(), Paint::alpha(ink, tk.fill(State::Hover).alphaF()),
                     Border::flat(Paint::alpha(ink, hb.color.alphaF()), hb.top));
    }
    const QFont f = tk.font("subtitle");
    const bool action = isActionShown();
    const QString glyph = action ? m_actionGlyph : m_trailingGlyph;
    if (!glyph.isEmpty()) {
      const QColor c = action ? (m_actionUrgent ? tk.urgent() : tk.textDim()) : toneColor(m_trailingTone);
      const QSizeF s = Paint::textSize(f, glyph);
      Paint::text(p, slot.topLeft() + QPointF(Paint::centered(slot.width(), s.width()), Paint::centered(slot.height(), s.height())), f, c, glyph);
    }
  }
}

void ListRow::mousePressEvent(QMouseEvent *e) {
  m_pressAt = e->position();
  Control::mousePressEvent(e);
}

void ListRow::mouseReleaseEvent(QMouseEvent *e) {
  const bool wasPressed = isPressed();
  Control::mouseReleaseEvent(e);
  if (!wasPressed || e->button() != Qt::LeftButton || !rect().contains(e->position().toPoint())) return;
  const QRectF slot = actionRect();
  if (!m_actionGlyph.isEmpty() && slot.contains(m_pressAt) && slot.contains(e->position())) emit actionClicked();
  else emit clicked();
}

void ListRow::mouseMoveEvent(QMouseEvent *e) {
  emit pointerMoved(e->position());
  Control::mouseMoveEvent(e);
}

bool ListRow::event(QEvent *e) {
  if (e->type() == QEvent::LayoutRequest) {
    updateGeometry();
    place();
    update();
  } else if (e->type() == QEvent::ToolTip && !m_actionTip.isEmpty()) {
    auto *he = static_cast<QHelpEvent *>(e);
    if (actionRect().contains(he->pos())) QToolTip::showText(he->globalPos(), m_actionTip, this, actionRect().toAlignedRect());
    else QToolTip::hideText();
    return true;
  }
  return Control::event(e);
}

void ListRow::changeEvent(QEvent *e) {
  if (e->type() == QEvent::EnabledChange) setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
  Control::changeEvent(e);
}

}  // namespace Omarchy
