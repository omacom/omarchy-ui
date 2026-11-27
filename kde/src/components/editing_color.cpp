// ColorPicker (spec/components/editing.md): the picker draws everything itself, in Qt Quick's
// stacking; the square, the strips and the swatches are input parts on top of it (transparent
// child widgets that take focus, the pointer and keys), and the hex field is the kit's TextField.

#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QRegularExpression>
#include <cmath>

#include "Omarchy/Editing.h"
#include "Omarchy/Inputs.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

static qreal jsRound(qreal v) { return std::floor(v + 0.5); }

static QString hex2(int n) { return QString::number(n, 16).rightJustified(2, QLatin1Char('0')); }

// One input part of the picker: the square, a strip or a swatch.
class PickerPart : public Control {
public:
  enum Kind { Square, Hue, Alpha, Swatch };
  PickerPart(Kind kind, int index, ColorPicker *picker) : Control(picker), m_kind(kind), m_index(index), m_picker(picker) {
    setFocusPolicy(Qt::StrongFocus);
    setCursor(kind == Square ? Qt::CrossCursor : Qt::PointingHandCursor);
  }
  Kind kind() const { return m_kind; }
  int index() const { return m_index; }
  bool isDown() const { return m_down; }

protected:
  void paintEvent(QPaintEvent *) override {}
  void mousePressEvent(QMouseEvent *e) override {
    if (e->button() != Qt::LeftButton) return;
    if (m_kind == Swatch) return Control::mousePressEvent(e);
    setFocus(Qt::MouseFocusReason);
    m_down = true;
    pick(e->position());
    e->accept();
  }
  void mouseMoveEvent(QMouseEvent *e) override {
    if (m_down) pick(e->position());
  }
  void mouseReleaseEvent(QMouseEvent *e) override {
    if (m_kind == Swatch) return Control::mouseReleaseEvent(e);
    if (e->button() != Qt::LeftButton) return;
    m_down = false;
    m_picker->update();
  }
  void activate(Qt::MouseButton b) override {
    if (b == Qt::LeftButton) m_picker->chooseSwatch(m_index);
  }
  void keyPressEvent(QKeyEvent *e) override { m_picker->keyOnPart(this, e); }
  void enterEvent(QEnterEvent *e) override {
    Control::enterEvent(e);
    m_picker->update();
  }
  void leaveEvent(QEvent *e) override {
    Control::leaveEvent(e);
    m_picker->update();
  }
  void focusInEvent(QFocusEvent *e) override {
    Control::focusInEvent(e);
    m_picker->update();
  }
  void focusOutEvent(QFocusEvent *e) override {
    Control::focusOutEvent(e);
    m_picker->update();
  }

private:
  void pick(const QPointF &p) {
    if (m_kind == Square) {
      m_picker->pickSquare(p);
    } else {
      const int pad = shown().space(2);
      const int k = qMax(8, width() - pad * 2);
      const qreal span = qMax(1, height() - k - pad * 2);
      m_picker->pickStrip(this, qBound(0.0, (p.y() - pad - k / 2.0) / span, 1.0));
    }
  }
  Kind m_kind;
  int m_index;
  bool m_down = false;
  ColorPicker *m_picker;
};

ColorPicker::ColorPicker(QWidget *parent) : Control(parent) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  m_paletteNames = {QStringLiteral("red"),  QStringLiteral("orange"), QStringLiteral("yellow"),  QStringLiteral("green"),
                    QStringLiteral("cyan"), QStringLiteral("blue"),   QStringLiteral("magenta"), QStringLiteral("foreground")};
  m_square = new PickerPart(PickerPart::Square, 0, this);
  m_hueStrip = new PickerPart(PickerPart::Hue, 0, this);
  m_alphaStrip = new PickerPart(PickerPart::Alpha, 0, this);
  m_field = new TextField(this);
  connect(m_field, &QLineEdit::textEdited, this, [this](const QString &text) {
    m_editing = true;
    if (setColor(text.trimmed())) changed();
    m_editing = false;
  });
  connect(m_field, &QLineEdit::editingFinished, this, [this] { m_field->setText(hex()); });
  connect(Theme::instance(), &Theme::changed, this, [this] {
    reloadPalette();
    place();
  });
  reloadPalette();
  sync();
}

void ColorPicker::setHue(qreal h) {
  m_h = h;
  sync();
}
void ColorPicker::setSaturation(qreal s) {
  m_s = s;
  sync();
}
void ColorPicker::setValue(qreal v) {
  m_v = v;
  sync();
}
void ColorPicker::setAlpha(qreal a) {
  m_a = a;
  sync();
}
void ColorPicker::setShowAlpha(bool on) {
  m_showAlpha = on;
  m_alphaStrip->setVisible(on);
  place();
}
void ColorPicker::setSurfaceColor(const QColor &c) {
  m_surface = c;
  update();
}
void ColorPicker::setPaletteNames(const QStringList &names) {
  m_paletteNames = names;
  reloadPalette();
  place();
}

void ColorPicker::hsvToRgb(qreal h, qreal s, qreal v, int rgb[3]) {
  const qreal h6 = (h - std::floor(h)) * 6;
  const int i = int(std::floor(h6));
  const qreal f = h6 - i;
  const qreal p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  const qreal table[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
  for (int c = 0; c < 3; ++c) rgb[c] = int(jsRound(table[i % 6][c] * 255));
}

QColor ColorPicker::color() const {
  int c[3];
  hsvToRgb(m_h, m_s, m_v, c);
  return QColor::fromRgbF(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, m_a);
}

QString ColorPicker::hex() const {
  int c[3];
  hsvToRgb(m_h, m_s, m_v, c);
  QString out = QLatin1Char('#') + hex2(c[0]) + hex2(c[1]) + hex2(c[2]);
  return m_a < 1 ? out + hex2(int(jsRound(m_a * 255))) : out;
}

bool ColorPicker::setColor(const QString &s) {
  static const QRegularExpression re(QStringLiteral("^#?([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?$"));
  const QRegularExpressionMatch m = re.match(s);
  if (!m.hasMatch()) return false;
  const QString rgb = m.captured(1);
  const qreal r = rgb.mid(0, 2).toInt(nullptr, 16) / 255.0, g = rgb.mid(2, 2).toInt(nullptr, 16) / 255.0, b = rgb.mid(4, 2).toInt(nullptr, 16) / 255.0;
  const qreal mx = qMax(r, qMax(g, b)), mn = qMin(r, qMin(g, b)), d = mx - mn;
  qreal h = m_h;
  if (d > 0) {
    if (mx == r) h = ((g - b) / d) / 6;
    else if (mx == g) h = ((b - r) / d + 2) / 6;
    else h = ((r - g) / d + 4) / 6;
    if (h < 0) h += 1;
  }
  m_h = h;
  m_s = mx > 0 ? d / mx : 0;
  m_v = mx;
  m_a = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt(nullptr, 16) / 255.0;
  sync();
  return true;
}

bool ColorPicker::setColor(const QColor &c) {
  return c.isValid() && setColor(QLatin1Char('#') + hex2(c.red()) + hex2(c.green()) + hex2(c.blue()) + hex2(c.alpha()));
}

void ColorPicker::changed() { emit edited(color()); }

// The field follows the color unless the user is typing in it.
void ColorPicker::sync() {
  if (!m_editing && m_field->text() != hex()) m_field->setText(hex());
  update();
}

void ColorPicker::reloadPalette() {
  QString dir = qEnvironmentVariable("OMARCHY_UI_THEME_DIR");
  if (dir.isEmpty()) dir = QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme");
  QFile f(dir + QStringLiteral("/colors.toml"));
  QHash<QString, QString> named;
  if (f.open(QIODevice::ReadOnly)) {
    static const QRegularExpression line(QStringLiteral("^\\s*([A-Za-z0-9_-]+)\\s*=\\s*[\"']?(#[0-9A-Fa-f]{6})"));
    for (const QString &l : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
      const QRegularExpressionMatch m = line.match(l);
      if (m.hasMatch()) named.insert(m.captured(1), m.captured(2).toLower());
    }
  }
  const Tokens &tk = shown();
  if (!named.contains(QStringLiteral("foreground"))) named.insert(QStringLiteral("foreground"), tk.foreground().name());
  if (!named.contains(QStringLiteral("background"))) named.insert(QStringLiteral("background"), tk.background().name());
  m_swatches.clear();
  for (const QString &n : m_paletteNames)
    if (named.contains(n)) m_swatches << named.value(n);
  while (m_swatchWidgets.size() > m_swatches.size()) delete m_swatchWidgets.takeLast();
  while (m_swatchWidgets.size() < m_swatches.size()) {
    auto *w = new PickerPart(PickerPart::Swatch, int(m_swatchWidgets.size()), this);
    w->show();
    m_swatchWidgets << w;
  }
}

void ColorPicker::chooseSwatch(int i) {
  if (i < 0 || i >= m_swatches.size()) return;
  const qreal a = m_a;
  setColor(m_swatches[i]);
  m_a = a;
  sync();
  changed();
}

int ColorPicker::knobSize() const { return qMax(14, int(std::lround(t().spacing("controlHeight") * 0.38))); }

QSize ColorPicker::sizeHint() const { return QSize(t().space(300), t().space(260)); }

// The ColumnLayout: the square and strips take what the field and palette leave.
void ColorPicker::place() {
  const Tokens &tk = t();
  const int W = width(), H = height();
  const int lg = tk.spacing("lg"), md = tk.spacing("md"), strip = tk.space(14), sw = tk.space(20);
  const int field = m_field->sizeHint().height();
  const int palette = m_swatches.isEmpty() ? 0 : sw;
  const int top = qMax(0, H - lg - field - lg - palette);
  const int square = qMax(0, W - md - strip - (m_showAlpha ? md + strip : 0));
  m_square->setGeometry(0, 0, square, top);
  m_hueStrip->setGeometry(square + md, 0, strip, top);
  m_alphaStrip->setGeometry(square + md + strip + md, 0, strip, top);
  const int hy = top + lg;
  m_field->setGeometry(field + md, hy, qMax(0, W - field - md), field);
  const int py = hy + field + lg;
  for (int i = 0; i < m_swatchWidgets.size(); ++i) m_swatchWidgets[i]->setGeometry(i * (sw + md), py, sw, sw);
  update();
}

void ColorPicker::pickSquare(const QPointF &p) {
  m_s = qBound(0.0, p.x() / qMax(1, m_square->width()), 1.0);
  m_v = 1 - qBound(0.0, p.y() / qMax(1, m_square->height()), 1.0);
  sync();
  changed();
}

void ColorPicker::pickStrip(QWidget *strip, qreal f) {
  if (strip == m_hueStrip) m_h = qMin(f, 0.9999);
  else m_a = 1 - f;
  sync();
  changed();
}

void ColorPicker::keyOnPart(QWidget *part, QKeyEvent *e) {
  auto *pp = static_cast<PickerPart *>(part);
  const qreal step = (e->modifiers() & Qt::ShiftModifier) ? 0.1 : 0.01;
  const int key = e->key();
  if (pp->kind() == PickerPart::Square) {
    if (key == Qt::Key_Left) m_s = qMax(0.0, m_s - step);
    else if (key == Qt::Key_Right) m_s = qMin(1.0, m_s + step);
    else if (key == Qt::Key_Up) m_v = qMin(1.0, m_v + step);
    else if (key == Qt::Key_Down) m_v = qMax(0.0, m_v - step);
    else return e->ignore();
    sync();
    changed();
  } else if (pp->kind() == PickerPart::Swatch) {
    if (key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Space) return e->ignore();
    chooseSwatch(pp->index());
  } else {
    const qreal fraction = pp->kind() == PickerPart::Hue ? m_h : 1 - m_a;
    if (key == Qt::Key_Up) pickStrip(part, qMax(0.0, fraction - step));
    else if (key == Qt::Key_Down) pickStrip(part, qMin(1.0, fraction + step));
    else return e->ignore();
  }
  e->accept();
}

QRectF ColorPicker::squareKnobRect() const {
  const int k = knobSize();
  const qreal x = qBound<qreal>(0, jsRound(m_s * m_square->width() - k / 2.0), qMax(0, m_square->width() - k));
  const qreal y = qBound<qreal>(0, jsRound((1 - m_v) * m_square->height() - k / 2.0), qMax(0, m_square->height() - k));
  return QRectF(x, y, k, k);
}

// SV square knob: concentric 2-tone ring (1px background outer + 1px foreground inner + opaque fill), 1.15× when hot.
static void svKnob(QPainter &p, const QRectF &r, const QColor &fill, const Tokens &tk, bool hot) {
  p.save();
  if (hot) {
    const QPointF c = r.center();
    p.translate(c);
    p.scale(1.15, 1.15);
    p.translate(-c);
  }
  Paint::surface(p, r, r.width() / 2, tk.background());
  const QRectF mid = r.adjusted(1, 1, -1, -1);
  Paint::surface(p, mid, mid.width() / 2, tk.foreground());
  const QRectF inner = mid.adjusted(1, 1, -1, -1);
  Paint::surface(p, inner, inner.width() / 2, fill);
  p.restore();
}

// Hue/alpha strip knob: 1px background outer ring + foreground inner circle, 1.15× when hot.
static void stripKnobDraw(QPainter &p, const QRectF &r, const Tokens &tk, bool hot) {
  p.save();
  if (hot) {
    const QPointF c = r.center();
    p.translate(c);
    p.scale(1.15, 1.15);
    p.translate(-c);
  }
  Paint::surface(p, r, r.width() / 2, tk.background());
  const QRectF inner = r.adjusted(1, 1, -1, -1);
  Paint::surface(p, inner, inner.width() / 2, tk.foreground());
  p.restore();
}

// A Rectangle with a gradient (Qt Quick's software renderer: a QLinearGradient across the
// rectangle, antialiased only when rounded).
static void gradientRect(QPainter &p, const QRectF &r, qreal radius, Qt::Orientation o, const QGradientStops &stops) {
  QLinearGradient g(r.topLeft(), o == Qt::Horizontal ? r.topRight() : r.bottomLeft());
  g.setStops(stops);
  p.save();
  p.setRenderHint(QPainter::Antialiasing, radius > 0);
  p.setPen(Qt::NoPen);
  p.fillPath(Paint::roundedRect(r, radius), g);
  p.restore();
}

void ColorPicker::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const qreal small = tk.smallRadius();
  int rgb[3], pure[3];
  hsvToRgb(m_h, m_s, m_v, rgb);
  hsvToRgb(m_h, 1, 1, pure);
  const QColor opaque(rgb[0], rgb[1], rgb[2]);

  // The square: the hue, white from the left, black from the bottom.
  const QRectF sq(m_square->geometry());
  Paint::surface(p, sq, small, QColor(pure[0], pure[1], pure[2]));
  gradientRect(p, sq, small, Qt::Horizontal, {{0, QColor(255, 255, 255, 255)}, {1, QColor(255, 255, 255, 0)}});
  gradientRect(p, sq, small, Qt::Vertical, {{0, QColor(0, 0, 0, 0)}, {1, QColor(0, 0, 0, 255)}});
  auto *square = static_cast<PickerPart *>(m_square);
  svKnob(p, squareKnobRect().translated(sq.topLeft()), opaque, tk, hasCursor() || square->hasFocus() || square->isDown());

  // The hue strip, and the alpha strip (opaque at the top) over the track.
  const int stripPad = tk.space(2);
  auto stripKnob = [&](QWidget *w, qreal fraction) {
    auto *part = static_cast<PickerPart *>(w);
    const QRectF r(w->geometry());
    const qreal sk = qMax<qreal>(8, r.width() - stripPad * 2);
    const qreal y = r.y() + stripPad + jsRound(fraction * qMax<qreal>(0, r.height() - sk - stripPad * 2));
    stripKnobDraw(p, QRectF(r.x() + jsRound((r.width() - sk) / 2), y, sk, sk), tk, part->hasFocus() || part->isDown());
  };
  const QRectF hue(m_hueStrip->geometry());
  QGradientStops hues;
  const QColor stops[] = {Qt::red, Qt::yellow, Qt::green, Qt::cyan, Qt::blue, Qt::magenta, Qt::red};
  for (int i = 0; i < 7; ++i) hues << QGradientStop(i / 6.0, stops[i]);
  gradientRect(p, hue, small, Qt::Vertical, hues);
  stripKnob(m_hueStrip, m_h);
  if (m_showAlpha) {
    const QRectF al(m_alphaStrip->geometry());
    Paint::surface(p, al, small, tk.track());
    QColor clear = opaque;
    clear.setAlpha(0);
    gradientRect(p, al, small, Qt::Vertical, {{0, opaque}, {1, clear}});
    stripKnob(m_alphaStrip, 1 - m_a);
  }

  // The preview, beside the field.
  const QRect f = m_field->geometry();
  const Border normal = Border::flat(tk.border(State::Normal).color, 1);
  Paint::surface(p, QRectF(0, f.y(), f.height(), f.height()), tk.radius(), color(), normal);

  // The palette.
  const QString current = hex().left(7);
  for (int i = 0; i < m_swatchWidgets.size(); ++i) {
    auto *w = static_cast<PickerPart *>(m_swatchWidgets[i]);
    const bool chosen = m_swatches[i] == current;
    const bool hot = w->isHovered() || w->hasFocus();
    const Border b = chosen ? Border::flat(tk.foreground(), qMax(1, tk.space(2))) : hot ? Border::flat(tk.border(State::Hover).color, 1) : normal;
    Paint::surface(p, QRectF(w->geometry()), small, QColor(m_swatches[i]), b);
  }
}

}  // namespace Omarchy
