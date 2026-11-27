// The pieces rows and grids are made of: KeyValue, Hero, Badge, StatusDot, Avatar and TileGrid
// (qt/qml/Omarchy/Ui/<Name>.qml).

#include <QFileInfo>
#include <QImageReader>
#include <QMouseEvent>
#include <QPainter>
#include <QTextLayout>
#include <QUrl>
#include <cmath>

#include "Omarchy/Lists.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

static int ceilPx(qreal v) { return int(std::ceil(v - 1e-6)); }
static QSize ceilSize(const QSizeF &s) { return QSize(ceilPx(s.width()), ceilPx(s.height())); }

// ----------------------------------------------------------------------------------------- tones

QColor Badge::toneColor(const Tokens &tk, const QString &tone, const QColor &neutral) {
  if (tone == QLatin1String("accent")) return tk.accent();
  if (tone == QLatin1String("success")) return tk.success();
  if (tone == QLatin1String("warning")) return tk.warning();
  if (tone == QLatin1String("urgent")) return tk.urgent();
  if (tone == QLatin1String("muted")) return tk.muted();
  return neutral.isValid() ? neutral : tk.foreground();
}

// -------------------------------------------------------------------------------------- KeyValue

struct KeyValue::Cells {
  QList<qreal> colX, colW, rowY, rowH;
  QSizeF size;
  int columns = 2;
};

KeyValue::KeyValue(QWidget *parent) : QWidget(parent) {
  follow(this);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void KeyValue::setPairs(const QList<Pair> &pairs) {
  m_pairs = pairs;
  updateGeometry();
  update();
}

void KeyValue::setPairColumns(int n) {
  m_columns = qMax(1, n);
  updateGeometry();
  update();
}

// Qt Quick's Grid: each column as wide as its widest cell, each line as tall as its tallest.
KeyValue::Cells KeyValue::cells() const {
  const Tokens &tk = shown();
  const QFont f = tk.font("bodySmall");
  Cells c;
  const int n = int(m_pairs.size()) * 2;
  c.columns = m_columns * 2;
  const int cols = qMin(c.columns, qMax(1, n));
  const int rows = n > 0 ? (n + c.columns - 1) / c.columns : 0;
  c.colW = QList<qreal>(cols, 0);
  c.rowH = QList<qreal>(rows, 0);
  for (int i = 0; i < n; ++i) {
    const Pair &p = m_pairs[i / 2];
    const QSizeF s = Paint::textSize(f, i % 2 == 0 ? p.label : p.value);
    c.colW[i % c.columns] = qMax(c.colW[i % c.columns], s.width());
    c.rowH[i / c.columns] = qMax(c.rowH[i / c.columns], s.height());
  }
  const qreal colGap = tk.space(20), rowGap = tk.spacing("labelGap");
  qreal x = 0, y = 0;
  for (int i = 0; i < cols; ++i) {
    c.colX << x;
    x += c.colW[i] + (i + 1 < cols ? colGap : 0);
  }
  for (int i = 0; i < rows; ++i) {
    c.rowY << y;
    y += c.rowH[i] + (i + 1 < rows ? rowGap : 0);
  }
  c.size = QSizeF(x, y);
  return c;
}

QSizeF KeyValue::implicitSize() const { return cells().size; }
QSize KeyValue::sizeHint() const { return ceilSize(implicitSize()); }

void KeyValue::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  const Cells c = cells();
  const QFont f = tk.font("bodySmall");
  for (int i = 0; i < m_pairs.size() * 2; ++i) {
    const Pair &pair = m_pairs[i / 2];
    const bool label = i % 2 == 0;
    const QColor color = label ? tk.textDim() : Badge::toneColor(tk, pair.tone == QLatin1String("muted") ? QString() : pair.tone);
    Paint::text(p, QPointF(c.colX[i % c.columns], c.rowY[i / c.columns]), f, color, label ? pair.label : pair.value);
  }
}

// ------------------------------------------------------------------------------------------ Hero

Hero::Hero(const QString &glyph, const QString &title, QWidget *parent) : QWidget(parent), m_glyph(glyph), m_title(title) {
  follow(this);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  connect(Theme::instance(), &Theme::metricsChanged, this, &Hero::place);
}

void Hero::setGlyph(const QString &g) {
  m_glyph = g;
  updateGeometry();
  update();
}
void Hero::setTitle(const QString &t) {
  m_title = t;
  updateGeometry();
  update();
}
void Hero::setStatus(const QString &s) {
  m_status = s;
  updateGeometry();
  place();
  update();
}
void Hero::setError(bool on) {
  m_error = on;
  update();
}
void Hero::setGlyphOpacity(qreal o) {
  m_glyphOpacity = o;
  update();
}
void Hero::setForeground(const QColor &c) {
  m_fg = c;
  update();
}
void Hero::setTrailing(QWidget *w) {
  if (m_trailing == w) return;
  if (m_trailing) m_trailing->deleteLater();
  m_trailing = w;
  if (w) {
    w->setParent(this);
    w->show();
  }
  updateGeometry();
  place();
}

QFont Hero::statusFont() const {
  QFont f = shown().font("caption", true);
  f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
  return f;
}

qreal Hero::labelsHeight() const {
  const Tokens &tk = shown();
  qreal h = Paint::lineHeight(tk.font("title", true));
  if (!m_status.isEmpty()) h += tk.space(2) + Paint::lineHeight(statusFont());
  return h;
}

qreal Hero::trailingInset() const {
  if (!m_trailing || m_trailing->isHidden()) return 0;
  return m_trailing->sizeHint().width() + shown().space(12);
}

QSizeF Hero::implicitSize() const {
  const Tokens &tk = shown();
  const qreal slotW = m_glyph.isEmpty() ? 0 : tk.fontSize("display");
  const qreal gap = m_glyph.isEmpty() ? 0 : tk.space(14);
  const qreal glyphH = qMax<qreal>(tk.fontSize("displayLarge"), std::ceil(tk.fontSize("display") * 1.35));
  qreal labelsW = Paint::textWidth(tk.font("title", true), m_title);
  if (!m_status.isEmpty()) labelsW = qMax(labelsW, Paint::textWidth(statusFont(), m_status.toUpper()));
  const qreal th = m_trailing && !m_trailing->isHidden() ? m_trailing->sizeHint().height() : 0;
  return QSizeF(slotW + gap + labelsW + trailingInset(), qMax(glyphH, qMax(labelsHeight(), th)));
}

QSize Hero::sizeHint() const { return ceilSize(implicitSize()); }

void Hero::place() {
  if (!m_trailing) return;
  const QSize s = m_trailing->sizeHint();
  m_trailing->setGeometry(width() - s.width(), int(Paint::centered(height(), s.height())), s.width(), s.height());
}

bool Hero::event(QEvent *e) {
  if (e->type() == QEvent::LayoutRequest) {
    updateGeometry();
    place();
  }
  return QWidget::event(e);
}

void Hero::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  const QColor fg = m_fg.isValid() ? m_fg : tk.foreground();
  const qreal slotW = m_glyph.isEmpty() ? 0 : tk.fontSize("display");
  const qreal gap = m_glyph.isEmpty() ? 0 : tk.space(14);
  if (!m_glyph.isEmpty()) {
    const qreal glyphH = qMax<qreal>(tk.fontSize("displayLarge"), std::ceil(tk.fontSize("display") * 1.35));
    const QRectF slot(0, Paint::centered(height(), glyphH), slotW, glyphH);
    p.save();
    p.setOpacity(m_glyphOpacity);
    Paint::opticalGlyph(p, slot, tk.font("display"), fg, m_glyph);
    p.restore();
  }
  const qreal x = slotW + gap;
  const qreal w = qMax<qreal>(0, width() - x - trailingInset());
  const qreal y = Paint::centered(height(), labelsHeight());
  const QFont title = tk.font("title", true);
  Paint::text(p, QPointF(x, y), title, fg, Paint::elided(title, m_title, w));
  if (!m_status.isEmpty()) {
    const QFont sf = statusFont();
    Paint::text(p, QPointF(x, y + Paint::lineHeight(title) + tk.space(2)), sf, m_error ? tk.urgent() : tk.textDim(),
                Paint::elided(sf, m_status.toUpper(), w));
  }
}

// ----------------------------------------------------------------------------------------- Badge

Badge::Badge(const QString &text, const QString &tone, bool filled, QWidget *parent)
    : Control(parent), m_text(text), m_tone(tone), m_filled(filled) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Badge::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  update();
}
void Badge::setTone(const QString &tone) {
  m_tone = tone;
  update();
}
void Badge::setFilled(bool on) {
  m_filled = on;
  update();
}

QSizeF Badge::implicitSize() const {
  const Tokens &tk = t();
  const int line = tk.border(State::Normal).maxWidth();
  const QSizeF s = Paint::textSize(tk.font("caption", true), m_text);
  const qreal h = s.height() + (tk.space(1) + line) * 2;
  return QSizeF(qMax(h, s.width() + (tk.space(6) + line) * 2), h);
}

QSize Badge::sizeHint() const { return ceilSize(implicitSize()); }

void Badge::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF box = paintRect();
  const QColor tone = toneColor(tk, m_tone == QLatin1String("muted") ? QString() : m_tone);
  const bool neutral = m_tone.isEmpty() && !m_filled;
  const QColor fill = m_filled ? tone : (neutral ? tk.fill(State::Normal) : Paint::alpha(tone, tk.fill(State::Selected).alphaF()));
  Paint::surface(p, box, tk.smallRadius(), fill, neutral ? tk.border(State::Normal) : Border());
  const QFont f = tk.font("caption", true);
  const QSizeF s = Paint::textSize(f, m_text);
  Paint::text(p, box.topLeft() + QPointF(Paint::centered(box.width(), s.width()), Paint::centered(box.height(), s.height())), f,
              m_filled ? tk.background() : tone, m_text);
}

// ------------------------------------------------------------------------------------- StatusDot

StatusDot::StatusDot(const QString &tone, QWidget *parent) : Control(parent), m_tone(tone) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void StatusDot::setTone(const QString &tone) {
  m_tone = tone;
  update();
}
void StatusDot::setDotSize(qreal px) {
  m_size = px;
  updateGeometry();
  update();
}
void StatusDot::setRing(qreal px) {
  m_ring = px;
  updateGeometry();
  update();
}
void StatusDot::setGround(const QColor &c) {
  m_ground = c;
  update();
}
qreal StatusDot::dotSize() const { return m_size > 0 ? m_size : t().space(8); }
qreal StatusDot::ring() const { return m_ring >= 0 ? m_ring : qMax(1, t().space(2)); }
QSizeF StatusDot::implicitSize() const {
  const qreal s = dotSize() + ring() * 2;
  return QSizeF(s, s);
}
QSize StatusDot::sizeHint() const { return ceilSize(implicitSize()); }

void StatusDot::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF box = paintRect();
  const qreal r = ring(), s = dotSize();
  if (r > 0) Paint::surface(p, box, box.width() / 2, m_ground.isValid() ? m_ground : tk.color(QStringLiteral("surfaces.popups.background")));
  Paint::surface(p, QRectF(box.x() + r, box.y() + r, s, s), s / 2, Badge::toneColor(tk, m_tone));
}

// ---------------------------------------------------------------------------------------- Avatar

Avatar::Avatar(const QString &name, QWidget *parent) : Control(parent), m_name(name) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  // A switch to another icon theme re-resolves.
  connect(Theme::instance(), &Theme::changed, this, [this] { update(); });
}

Avatar::~Avatar() { delete m_dot.data(); }

void Avatar::setName(const QString &n) {
  m_name = n;
  update();
}
void Avatar::setSource(const QString &source) {
  m_source = source;
  update();
}
void Avatar::setIconName(const QString &name) {
  m_iconName = name;
  update();
}
void Avatar::setGlyph(const QString &g) {
  m_glyph = g;
  update();
}
void Avatar::setSize(Size s) {
  m_size = s;
  updateGeometry();
  dock();
  update();
}
void Avatar::setPixelSize(qreal px) {
  m_pixelSize = px;
  updateGeometry();
  dock();
  update();
}
void Avatar::setRound(bool on) {
  m_round = on;
  update();
}
void Avatar::setStatus(const QString &tone) {
  m_status = tone;
  dock();
}
void Avatar::setGround(const QColor &c) {
  m_ground = c;
  if (m_dot) m_dot->setGround(c);
}

qreal Avatar::pixelSize() const {
  if (m_pixelSize > 0) return m_pixelSize;
  const Tokens &tk = t();
  if (m_size == Small) return tk.fontSize("iconLarge");
  if (m_size == Large) return tk.fontSize("display") * 2;
  return tk.fontSize("displayLarge");
}

qreal Avatar::radius() const {
  const qreal px = pixelSize();
  return m_round ? px / 2 : qMin<qreal>(t().radius(), px / 2);
}

int Avatar::devicePixels() const { return int(std::ceil(pixelSize() * devicePixelRatioF() - 1e-6)); }

QSizeF Avatar::implicitSize() const { return QSizeF(pixelSize(), pixelSize()); }
QSize Avatar::sizeHint() const { return ceilSize(implicitSize()); }

QString Avatar::initial(const QString &name) {
  const QString text = name.trimmed();
  if (text.isEmpty()) return QString();
  return (text.at(0).isHighSurrogate() ? text.left(2) : text.left(1)).toUpper();
}

static QString localPath(const QString &source) {
  if (source.startsWith(QLatin1String("qrc:"))) return QLatin1Char(':') + QUrl(source).path();
  if (source.startsWith(QLatin1String("file:"))) return QUrl(source).toLocalFile();
  return source;
}

// Qt Quick's Image with sourceSize: the reader scales to `size` keeping the aspect (covering it
// for crop, fitting in it otherwise), rounded as QQuickImageProviderWithOptions::loadSize does.
static QImage readScaled(const QString &path, int size, bool crop) {
  QImageReader reader(path);
  if (!reader.canRead()) return QImage();
  const QSize orig = reader.size();
  if (orig.isValid() && !orig.isEmpty()) {
    const qreal wr = qreal(size) / orig.width(), hr = qreal(size) / orig.height();
    const qreal ratio = crop ? qMax(wr, hr) : qMin(wr, hr);
    reader.setScaledSize(QSize(qRound(orig.width() * ratio), qRound(orig.height() * ratio)));
  }
  return reader.read();
}

const QImage &Avatar::picture() const {
  const int px = devicePixels();
  const QString key = m_source + QLatin1Char('@') + QString::number(px) + (radius() > 0 ? QLatin1String("r") : QLatin1String(""));
  if (key == m_pictureKey) return m_picture;
  m_pictureKey = key;
  m_picture = QImage();
  if (m_source.isEmpty()) return m_picture;
  const QString path = localPath(m_source);
  if (radius() <= 0) {
    m_picture = readScaled(path, px, true);
  } else {
    // Qt Quick's Canvas: the full image drawn to cover the square, clipped to the corners.
    QImage full(path);
    if (full.isNull()) return m_picture;
    QImage out(px, px, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const qreal r = radius() * devicePixelRatioF();
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, px, px), r, r);
    p.setClipPath(clip);
    const qreal k = qMax(qreal(px) / full.width(), qreal(px) / full.height());
    const qreal w = full.width() * k, h = full.height() * k;
    p.drawImage(QRectF((px - w) / 2, (px - h) / 2, w, h), full);
    p.end();
    m_picture = out;
  }
  return m_picture;
}

const QImage &Avatar::icon() const {
  const int px = devicePixels();
  const QString key = m_iconName + QLatin1Char('@') + QString::number(px) + QLatin1Char('@') + Theme::instance()->iconTheme();
  if (key == m_iconKey) return m_icon;
  m_iconKey = key;
  m_icon = QImage();
  if (m_iconName.isEmpty()) return m_icon;
  const QString path = Theme::instance()->iconPath(m_iconName, px);
  if (!path.isEmpty()) m_icon = readScaled(path, px, false);
  return m_icon;
}

QString Avatar::drawn() const {
  if (!m_source.isEmpty() && !picture().isNull()) return QStringLiteral("picture");
  if (!icon().isNull()) return QStringLiteral("icon");
  return QStringLiteral("fallback");
}

void Avatar::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF box = paintRect();
  const qreal px = pixelSize();
  const QRectF r(box.topLeft(), QSizeF(px, px));
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  if (!m_source.isEmpty() && !picture().isNull()) {
    const QImage &img = picture();
    const qreal dpr = devicePixelRatioF();
    // Cropped to the square (PreserveAspectCrop), centered.
    const QSizeF dev(px * dpr, px * dpr);
    const QRectF src((img.width() - dev.width()) / 2, (img.height() - dev.height()) / 2, dev.width(), dev.height());
    p.drawImage(r, img, radius() > 0 ? QRectF(img.rect()) : src.intersected(QRectF(img.rect())));
    return;
  }
  const QImage &ic = icon();
  if (!ic.isNull()) {
    const qreal dpr = devicePixelRatioF();
    const QSizeF s = QSizeF(ic.size()).scaled(QSizeF(px, px), Qt::KeepAspectRatio);
    Q_UNUSED(dpr);
    p.drawImage(QRectF(r.x() + (px - s.width()) / 2, r.y() + (px - s.height()) / 2, s.width(), s.height()), ic);
    return;
  }
  Paint::surface(p, r, radius(), tk.fill(State::Selected));
  const int fontPx = int(std::round(px * 0.5));
  if (!m_glyph.isEmpty()) {
    Paint::opticalGlyph(p, r, tk.fontPx(fontPx), tk.foreground(), m_glyph);
  } else {
    const QString mark = initial(m_name);
    const QFont f = tk.fontPx(fontPx, true);
    const QSizeF s = Paint::textSize(f, mark);
    Paint::text(p, r.topLeft() + QPointF(Paint::centered(px, s.width()), Paint::centered(px, s.height())), f, tk.foreground(), mark);
  }
}

// The docked StatusDot overhangs the corner, so it is a sibling drawn over the avatar.
void Avatar::dock() {
  if (m_status.isEmpty()) {
    if (m_dot) m_dot->hide();
    return;
  }
  if (!parentWidget()) return;
  if (!m_dot || m_dot->parentWidget() != parentWidget()) {
    delete m_dot.data();
    m_dot = new StatusDot(m_status, parentWidget());
    if (m_ground.isValid()) m_dot->setGround(m_ground);
  }
  m_dot->setTone(m_status);
  const qreal px = pixelSize();
  m_dot->setDotSize(qMax<qreal>(t().space(6), std::round(px * 0.28)));
  const QSizeF s = m_dot->implicitSize();
  const qreal ring = m_dot->ring();
  const QPointF at = QPointF(pos()) + paintRect().topLeft() + QPointF(px - s.width() + ring, px - s.height() + ring);
  m_dot->setGeometryF(QRectF(at, s));
  m_dot->setVisible(!isHidden());
  m_dot->raise();
}

bool Avatar::event(QEvent *e) {
  switch (e->type()) {
  case QEvent::Move:
  case QEvent::Resize:
  case QEvent::Show:
  case QEvent::ParentChange: dock(); break;
  case QEvent::Hide:
    if (m_dot) m_dot->hide();
    break;
  default: break;
  }
  return Control::event(e);
}

// -------------------------------------------------------------------------------------- TileGrid

TileGrid::TileGrid(QWidget *parent) : QWidget(parent) {
  follow(this);
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  connect(Theme::instance(), &Theme::metricsChanged, this, &TileGrid::place);
}

void TileGrid::setTiles(const QList<Tile> &tiles) {
  m_tiles = tiles;
  rebuild();
}
void TileGrid::setColumns(int n) {
  m_columns = qMax(1, n);
  updateGeometry();
  place();
  update();
}
void TileGrid::setCellWidth(qreal w) {
  m_cellWidth = w;
  updateGeometry();
  place();
  update();
}
qreal TileGrid::cellWidth() const { return m_cellWidth > 0 ? m_cellWidth : shown().space(88); }
void TileGrid::setCursorIndex(int i) {
  if (m_cursor == i) return;
  m_cursor = i;
  update();
}
void TileGrid::setCurrentIndex(int i) {
  m_current = i;
  update();
}

qreal TileGrid::iconHeight() const {
  const Tokens &tk = shown();
  return qMax<qreal>(Paint::lineHeight(tk.font("display")), tk.fontSize("displayLarge"));
}

qreal TileGrid::cellHeight() const {
  const Tokens &tk = shown();
  const qreal md = tk.spacing("md");
  return md * 2 + iconHeight() + md + Paint::lineHeight(tk.font("bodySmall")) * 2;
}

QRectF TileGrid::tileRect(int index) const {
  if (index < 0 || index >= m_tiles.size()) return QRectF();
  const qreal w = cellWidth(), h = cellHeight();
  return QRectF((index % m_columns) * w, (index / m_columns) * h, w, h);
}

int TileGrid::tileAt(const QPointF &pos) const {
  for (int i = 0; i < m_tiles.size(); ++i)
    if (tileRect(i).contains(pos)) return i;
  return -1;
}

QSizeF TileGrid::implicitSize() const {
  const int n = int(m_tiles.size());
  if (n == 0) return QSizeF();
  const int cols = qMin(m_columns, n);
  const int rows = (n + m_columns - 1) / m_columns;
  return QSizeF(cols * cellWidth(), rows * cellHeight());
}

QSize TileGrid::sizeHint() const { return ceilSize(implicitSize()); }

void TileGrid::rebuild() {
  qDeleteAll(m_avatars);
  m_avatars.clear();
  for (const Tile &t : m_tiles) {
    Avatar *a = nullptr;
    if (t.avatar) {
      a = new Avatar(t.name.isEmpty() ? t.label : t.name, this);
      a->setSource(t.source);
      a->setGlyph(t.glyph);
      a->show();
    }
    m_avatars << a;
  }
  updateGeometry();
  place();
  update();
}

// Wrapped height of a tile label, capped at 2 lines (matching Text.Wrap + maximumLineCount: 2).
static qreal tileLabelHeight(const QFont &font, const QString &text, qreal width) {
  if (text.isEmpty()) return Paint::lineHeight(font);
  QTextLayout layout(text, font);
  QTextOption opt;
  opt.setUseDesignMetrics(true);
  opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(opt);
  layout.beginLayout();
  qreal y = 0;
  int lines = 0;
  for (;;) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(qMax<qreal>(1, width));
    line.setPosition(QPointF(0, y));
    if (lines < 2) y += line.height();
    ++lines;
  }
  layout.endLayout();
  return lines > 0 ? y : Paint::lineHeight(font);
}

void TileGrid::place() {
  const Tokens &tk = shown();
  const qreal md = tk.spacing("md");
  const QFont labelFont = tk.font("bodySmall");
  const qreal iconH = iconHeight();
  for (int i = 0; i < m_avatars.size(); ++i) {
    Avatar *a = m_avatars[i];
    if (!a) continue;
    const QRectF tile = tileRect(i);
    const qreal labelH = tileLabelHeight(labelFont, m_tiles[i].label, tile.width() - md * 2);
    const qreal contentH = iconH + md + labelH;
    const qreal contentY = tile.y() + std::floor((tile.height() - contentH) / 2.0 + 0.5);
    const qreal px = a->pixelSize();
    a->setGeometryF(QRectF(tile.x() + Paint::centered(tile.width(), px), contentY + Paint::centered(iconH, px), px, px));
  }
}

// Text.Wrap with maximumLineCount 2 and ElideRight, each line centered in `r`'s width.
static void drawLabel(QPainter &p, const QRectF &r, const QFont &font, const QColor &color, const QString &text) {
  QTextLayout layout(text, font);
  QTextOption opt;
  opt.setUseDesignMetrics(true);
  opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(opt);
  layout.beginLayout();
  qreal y = 0;
  for (;;) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(r.width());
    line.setPosition(QPointF(0, y));
    y += line.height();
  }
  layout.endLayout();
  const int count = layout.lineCount();
  p.save();
  p.setPen(color);
  for (int i = 0; i < qMin(2, count); ++i) {
    const QTextLine line = layout.lineAt(i);
    if (i == 1 && count > 2) {
      const QString rest = Paint::elided(font, text.mid(line.textStart()), r.width());
      Paint::text(p, QPointF(r.x() + (r.width() - Paint::textWidth(font, rest)) / 2, r.y() + line.y()), font, color, rest);
    } else {
      line.draw(&p, QPointF(r.x() + (r.width() - line.naturalTextWidth()) / 2, r.y()));
    }
  }
  p.restore();
}

void TileGrid::paintEvent(QPaintEvent *) {
  const Tokens &tk = shown();
  QPainter p(this);
  const qreal md = tk.spacing("md");
  const qreal iconH = iconHeight();
  const QFont glyphFont = tk.font("display");
  const QFont label = tk.font("bodySmall");
  for (int i = 0; i < m_tiles.size(); ++i) {
    const Tile &t = m_tiles[i];
    const QRectF r = tileRect(i);
    if (i == m_cursor) Paint::surface(p, r, tk.radius(), tk.fill(State::Hover), tk.border(State::Hover));
    else if (i == m_current) Paint::surface(p, r, tk.radius(), tk.fill(State::Selected), tk.border(State::Selected));
    const qreal innerW = r.width() - md * 2;
    const qreal labelH = tileLabelHeight(label, t.label, innerW);
    const qreal contentH = iconH + md + labelH;
    const qreal contentY = r.y() + std::floor((r.height() - contentH) / 2.0 + 0.5);
    if (!t.avatar && !t.glyph.isEmpty()) {
      Paint::opticalGlyph(p, QRectF(r.x() + md, contentY, innerW, iconH), glyphFont, tk.foreground(), t.glyph);
    }
    drawLabel(p, QRectF(r.x() + md, contentY + iconH + md, innerW, labelH), label, tk.foreground(), t.label);
  }
}

void TileGrid::mouseMoveEvent(QMouseEvent *e) {
  const int i = tileAt(e->position());
  if (i >= 0) emit pointerMoved(i, e->position());
}

void TileGrid::mousePressEvent(QMouseEvent *e) {
  m_pressed = e->button() == Qt::LeftButton ? tileAt(e->position()) : -1;
}

void TileGrid::mouseReleaseEvent(QMouseEvent *e) {
  const int i = tileAt(e->position());
  if (e->button() == Qt::LeftButton && i >= 0 && i == m_pressed) emit activated(i);
  m_pressed = -1;
}

}  // namespace Omarchy
