#include "Omarchy/Feedback.h"

#include <QFontMetricsF>
#include <QPainter>
#include <cmath>

#include "Omarchy/Button.h"
#include "Omarchy/Paint.h"

namespace Omarchy {

static int ceilPx(qreal v) { return int(std::ceil(v - 1e-6)); }

static QColor toneColorOf(const Tokens &tk, const QString &tone, const QColor &neutral) {
  if (tone == QLatin1String("info")) return tk.accent();
  if (tone == QLatin1String("success")) return tk.success();
  if (tone == QLatin1String("warning")) return tk.warning();
  if (tone == QLatin1String("error")) return tk.urgent();
  return neutral;
}

// Re-evaluate an animation's running state when the theme changes (reduced motion can flip).
template <typename F>
static void onThemeChange(QObject *owner, F f) {
  QObject::connect(Theme::instance(), &Theme::changed, owner, [f] { f(); });
}

// ----------------------------------------------------------------------------------------- Meter

Meter::Meter(QWidget *parent) : Control(parent) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_fill = new QVariantAnimation(this);
  m_fill->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_fill, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    m_shown = v.toReal();
    update();
  });
  m_segment = new QVariantAnimation(this);
  m_segment->setStartValue(0.0);
  m_segment->setEndValue(1.0);
  m_segment->setDuration(1400);
  m_segment->setLoopCount(-1);
  m_segment->setEasingCurve(QEasingCurve::InOutQuad);
  connect(m_segment, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    m_sweep = v.toReal();
    update();
  });
  setAccessibleDescription(QStringLiteral("progress bar"));
  onThemeChange(this, [this] { sync(); });
}

Meter::Meter(qreal value, QWidget *parent) : Meter(parent) { setValue(value); }

void Meter::setValue(qreal v) {
  m_value = v;
  const qreal target = qBound(0.0, v, 1.0);
  const int ms = shown().duration(160);
  m_fill->stop();
  if (ms <= 0 || !isVisible()) {
    m_shown = target;
  } else {
    m_fill->setDuration(ms);
    m_fill->setStartValue(m_shown);
    m_fill->setEndValue(target);
    m_fill->start();
  }
  update();
}

void Meter::setTone(const QString &tone) {
  m_tone = tone;
  update();
}
void Meter::setAlarming(bool on) {
  m_alarming = on;
  update();
}
void Meter::setMarkers(const QList<qreal> &fractions) {
  m_markers = fractions;
  updateGeometry();
  update();
}
void Meter::setLabel(const QString &t) {
  m_label = t;
  setAccessibleName(t);
  updateGeometry();
  update();
}
void Meter::setValueText(const QString &t) {
  m_valueText = t;
  updateGeometry();
  update();
}
void Meter::setIndeterminate(bool on) {
  m_indeterminate = on;
  sync();
  update();
}
void Meter::setThickness(qreal px) {
  m_thickness = px;
  updateGeometry();
  update();
}

qreal Meter::thickness() const { return m_thickness > 0 ? m_thickness : t().spacing("meterThickness"); }

QColor Meter::fillColor() const {
  const Tokens &tk = t();
  return m_alarming ? tk.urgent() : toneColorOf(tk, m_tone, tk.foreground());
}

qreal Meter::headerHeight() const { return Paint::lineHeight(t().font("body")); }

qreal Meter::barHeight() const {
  const qreal th = thickness();
  return !m_markers.isEmpty() ? qMax(th, qreal(std::round(th * 2.5))) : th;
}

QSize Meter::sizeHint() const {
  const Tokens &tk = t();
  const qreal h = (hasHeader() ? headerHeight() + tk.spacing("md") : 0) + barHeight();
  return QSize(tk.space(200), ceilPx(h));
}

QRectF Meter::trackRect() const {
  const QRectF r = paintRect();
  const qreal barY = r.y() + (hasHeader() ? headerHeight() + t().spacing("md") : 0);
  const qreal th = thickness();
  return QRectF(r.x(), barY + std::round((barHeight() - th) / 2), r.width(), th);
}

void Meter::sync() {
  const bool run = m_indeterminate && isVisible() && !shown().reduceMotion();
  if (run && m_segment->state() != QAbstractAnimation::Running) m_segment->start();
  else if (!run) m_segment->stop();
}

void Meter::showEvent(QShowEvent *e) {
  Control::showEvent(e);
  sync();
}

void Meter::hideEvent(QHideEvent *e) {
  Control::hideEvent(e);
  sync();
}

void Meter::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF r = paintRect();
  if (hasHeader()) {
    const QFont body = tk.font("body");
    const qreal vw = Paint::textWidth(body, m_valueText);
    const qreal lw = qMax<qreal>(0, r.width() - vw - tk.spacing("controlGap"));
    Paint::text(p, r.topLeft(), body, tk.foreground(), Paint::elided(body, m_label, lw));
    Paint::text(p, QPointF(r.right() - vw, r.y()), body, tk.textDim(), m_valueText);
  }
  const QRectF track = trackRect();
  const qreal th = track.height();
  const qreal radius = qMin<qreal>(tk.smallRadius(), th / 2);
  const QColor fill = fillColor();
  Paint::surface(p, track, radius, tk.track());
  if (m_indeterminate) {
    const qreal segW = std::round(track.width() * 0.25);
    const qreal x = m_segment->state() == QAbstractAnimation::Running ? -segW + m_sweep * (track.width() + segW) : (track.width() - segW) / 2;
    p.save();
    p.setClipRect(track);
    Paint::surface(p, QRectF(track.x() + x, track.y(), segW, th), radius, fill);
    p.restore();
    return;
  }
  Paint::surface(p, QRectF(track.x(), track.y(), track.width() * m_shown, th), radius, fill);
  const qreal tw = qMax(2.0, std::round(th * 0.5)), tick = std::round(th * 2.5);
  const qreal barY = track.y() - std::round((barHeight() - th) / 2);
  const bool mirrored = layoutDirection() == Qt::RightToLeft;
  for (qreal m : m_markers) {
    const qreal along = (track.width() - tw) * qBound(0.0, m, 1.0);
    const qreal x = mirrored ? track.width() - tw - along : along;
    Paint::surface(p, QRectF(track.x() + x, barY + std::round((barHeight() - tick) / 2), tw, tick), qMin<qreal>(tk.smallRadius(), tw / 2), fill);
  }
}

ProgressBar::ProgressBar(QWidget *parent) : Meter(parent) { setTone(QStringLiteral("info")); }
ProgressBar::ProgressBar(qreal value, QWidget *parent) : ProgressBar(parent) { setValue(value); }

// --------------------------------------------------------------------------------------- Spinner

Spinner::Spinner(QWidget *parent) : Control(parent), m_glyph(QString::fromUcs4(U"\U000f0996")) {
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setAccessibleName(QStringLiteral("Busy"));
  m_anim = new QVariantAnimation(this);
  m_anim->setStartValue(0.0);
  m_anim->setEndValue(1.0);
  m_anim->setLoopCount(-1);
  connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    if (m_pulse) m_phase = v.toReal();
    else m_angle = v.toReal() * 360;
    update();
  });
  onThemeChange(this, [this] { sync(); });
}

void Spinner::setGlyph(const QString &g) {
  m_glyph = g;
  update();
}
int Spinner::size() const { return m_size > 0 ? m_size : t().fontSize("icon"); }
void Spinner::setSize(int px) {
  m_size = px;
  updateGeometry();
  update();
}
void Spinner::setPulse(bool on) {
  m_pulse = on;
  m_anim->stop();
  m_angle = 0;
  m_phase = 0;
  sync();
  update();
}
QColor Spinner::color() const { return m_color.isValid() ? m_color : t().foreground(); }
void Spinner::setColor(const QColor &c) {
  m_color = c;
  update();
}
void Spinner::setRunning(bool on) {
  m_running = on;
  sync();
}

bool Spinner::isAnimating() const { return m_anim->state() == QAbstractAnimation::Running; }

void Spinner::sync() {
  const bool run = m_running && isVisible() && !shown().reduceMotion();
  if (!run) {
    m_anim->stop();
    m_angle = 0;
    m_phase = 0;
    update();
    return;
  }
  if (isAnimating()) return;
  // A turn is linear; a pulse is two eased halves (0.5 → 1 → 0.5), worked out in glyphOpacity().
  m_anim->setEasingCurve(QEasingCurve::Linear);
  m_anim->setDuration(m_pulse ? 840 : 900);
  m_anim->start();
}

qreal Spinner::glyphOpacity() const {
  if (!m_pulse) return 1.0;
  if (!isAnimating()) return 0.5;
  static const QEasingCurve io(QEasingCurve::InOutQuad);
  return m_phase < 0.5 ? 0.5 + 0.5 * io.valueForProgress(m_phase * 2) : 1.0 - 0.5 * io.valueForProgress((m_phase - 0.5) * 2);
}

void Spinner::showEvent(QShowEvent *e) {
  Control::showEvent(e);
  sync();
}

void Spinner::hideEvent(QHideEvent *e) {
  Control::hideEvent(e);
  sync();
}

QSize Spinner::sizeHint() const {
  const int side = ceilPx(Paint::lineHeight(t().fontPx(size())));
  return QSize(side, side);
}

void Spinner::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  const QRectF r = paintRect();
  const QFont font = tk.fontPx(size());
  // The glyph's ink centered across the box, so it turns about its own middle.
  const QRectF ink = QFontMetricsF(font).tightBoundingRect(m_glyph);
  const qreal lh = Paint::lineHeight(font);
  const QPointF at(r.x() + std::round((r.width() - ink.width()) / 2 - ink.x()), r.y() + std::round((r.height() - lh) / 2));
  if (m_angle != 0) {
    const QPointF c = r.center();
    p.translate(c);
    p.rotate(m_angle);
    p.translate(-c);
  }
  p.setOpacity(glyphOpacity());
  Paint::text(p, at, font, color(), m_glyph);
}

// ------------------------------------------------------------------------------------ EmptyState

EmptyState::EmptyState(const QString &title, QWidget *parent) : Control(parent), m_title(title) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  m_action = new Button(QString(), this);
  m_action->setBordered(true);
  m_action->setFocusable(true);
  m_action->hide();
  connect(m_action, &Button::clicked, this, &EmptyState::actionClicked);
  connect(this, &Control::hasCursorChanged, m_action, &Control::setHasCursor);
}

void EmptyState::setTitle(const QString &t) {
  m_title = t;
  updateGeometry();
  place();
}
void EmptyState::setGlyph(const QString &g) {
  m_glyph = g;
  updateGeometry();
  place();
}
void EmptyState::setHint(const QString &t) {
  m_hint = t;
  updateGeometry();
  place();
}
void EmptyState::setActionText(const QString &t) {
  m_actionText = t;
  m_action->setText(t);
  m_action->setVisible(!t.isEmpty());
  updateGeometry();
  place();
}
void EmptyState::setSize(const QString &s) {
  m_overlay = s == QLatin1String("overlay");
  updateGeometry();
  place();
}

QFont EmptyState::glyphFont() const { return t().font(m_overlay ? "displayLarge" : "iconLarge"); }
QFont EmptyState::titleFont() const { return t().font(m_overlay ? "title" : "body"); }
QFont EmptyState::hintFont() const { return t().font(m_overlay ? "body" : "bodySmall"); }

// Qt Quick's Column: the visible parts stacked `gap` apart, at fractional positions.
EmptyState::Parts EmptyState::parts(qreal width) const {
  const qreal gap = t().spacing(m_overlay ? "lg" : "sm");
  Parts o{};
  qreal y = 0;
  int n = 0;
  auto add = [&](qreal &top, qreal &h, qreal height) {
    if (n++) y += gap;
    top = y;
    h = height;
    y += height;
  };
  if (!m_glyph.isEmpty()) add(o.glyphY, o.glyphH, Paint::lineHeight(glyphFont()));
  add(o.titleY, o.titleH, Paint::wrappedHeight(titleFont(), m_title, width));
  if (!m_hint.isEmpty()) add(o.hintY, o.hintH, Paint::wrappedHeight(hintFont(), m_hint, width));
  qreal ah = 0;
  if (!m_actionText.isEmpty()) add(o.actionY, ah, m_action->implicitSize().height());
  o.height = y;
  return o;
}

QSizeF EmptyState::implicitSize() const {
  qreal w = Paint::textWidth(titleFont(), m_title);
  if (!m_glyph.isEmpty()) w = qMax(w, Paint::textWidth(glyphFont(), m_glyph));
  if (!m_hint.isEmpty()) w = qMax(w, Paint::textWidth(hintFont(), m_hint));
  if (!m_actionText.isEmpty()) w = qMax(w, m_action->implicitSize().width());
  return QSizeF(w, parts(w).height);
}

qreal EmptyState::implicitHeightFor(qreal width) const { return parts(width).height; }

QSize EmptyState::sizeHint() const {
  const QSizeF s = implicitSize();
  return QSize(ceilPx(s.width()), ceilPx(s.height()));
}

int EmptyState::heightForWidth(int w) const { return ceilPx(parts(w).height); }

void EmptyState::place() {
  const QRectF r = paintRect();
  const Parts o = parts(r.width());
  if (!m_actionText.isEmpty()) {
    const QSizeF s = m_action->implicitSize();
    m_action->setGeometryF(QRectF(r.x() + Paint::centered(r.width(), s.width()), r.y() + o.actionY, s.width(), s.height()));
  }
  update();
}

void EmptyState::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF r = paintRect();
  const Parts o = parts(r.width());
  if (!m_glyph.isEmpty()) {
    const QFont f = glyphFont();
    p.save();
    if (m_overlay) p.setOpacity(0.8);
    const qreal w = Paint::textWidth(f, m_glyph);
    Paint::text(p, QPointF(r.x() + Paint::centered(r.width(), w), r.y() + o.glyphY), f,
                m_overlay ? tk.color(QStringLiteral("surfaces.menu.selectedText")) : tk.textDim(), m_glyph);
    p.restore();
  }
  p.save();
  if (m_overlay) p.setOpacity(0.7);
  Paint::wrapped(p, QRectF(r.x(), r.y() + o.titleY, r.width(), o.titleH), titleFont(), m_overlay ? tk.foreground() : tk.textDim(), m_title,
                 Qt::AlignHCenter);
  p.restore();
  if (!m_hint.isEmpty())
    Paint::wrapped(p, QRectF(r.x(), r.y() + o.hintY, r.width(), o.hintH), hintFont(), m_overlay ? tk.textDim() : tk.textFaint(), m_hint,
                   Qt::AlignHCenter);
}

// ---------------------------------------------------------------------------------------- Banner

Banner::Banner(const QString &text, QWidget *parent) : Control(parent), m_text(text) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setAccessibleName(text);
  m_action = new Button(QString(), this);
  m_action->setBordered(true);
  m_action->setFocusable(true);
  m_action->hide();
  connect(m_action, &Button::clicked, this, &Banner::actionClicked);
  m_dismiss = new IconButton(QString::fromUcs4(U"\U000f0156"), this);
  m_dismiss->setFocusable(true);
  m_dismiss->setAccessibleName(QStringLiteral("Dismiss"));
  m_dismiss->hide();
  connect(m_dismiss, &IconButton::clicked, this, &Banner::dismissed);
  connect(this, &Control::hasCursorChanged, m_action, &Control::setHasCursor);
}

void Banner::setText(const QString &t) {
  m_text = t;
  setAccessibleName(t);
  updateGeometry();
  place();
}
void Banner::setTone(const QString &t) {
  m_tone = t;
  update();
}
void Banner::setGlyph(const QString &g) {
  m_glyph = g;
  updateGeometry();
  place();
}
void Banner::setActionText(const QString &t) {
  m_actionText = t;
  m_action->setText(t);
  m_action->setVisible(!t.isEmpty());
  updateGeometry();
  place();
}
void Banner::setDismissible(bool on) {
  m_dismissible = on;
  m_dismiss->setVisible(on);
  updateGeometry();
  place();
}

QColor Banner::toneColor() const {
  const Tokens &tk = t();
  return toneColorOf(tk, m_tone == QLatin1String("neutral") ? QStringLiteral("info") : m_tone, tk.accent());
}

QString Banner::toneGlyph() const {
  if (!m_glyph.isEmpty()) return m_glyph;
  if (m_tone == QLatin1String("success")) return QString::fromUcs4(U"\U000f012c");
  if (m_tone == QLatin1String("warning")) return QString::fromUcs4(U"\U000f002a");
  if (m_tone == QLatin1String("error")) return QString::fromUcs4(U"\U000f015a");
  return QString::fromUcs4(U"\U000f02fd");
}

QMargins Banner::insets() const {
  const Tokens &tk = t();
  const int bw = qMax(1, tk.border(State::Normal).maxWidth());
  const bool trailing = !m_actionText.isEmpty() || m_dismissible;
  const int px = tk.spacing("controlPaddingX"), sm = tk.spacing("sm");
  return QMargins(bw + px, bw + sm, bw + (trailing ? sm : px), bw + sm);
}

// The RowLayout: glyph, text (fills, wraps), action, dismiss; implicit sizes ceiled, each
// centered across the row.
Banner::Geo Banner::geometry(qreal width) const {
  const Tokens &tk = t();
  const QMargins in = insets();
  const qreal gap = tk.spacing("controlGap");
  const QFont icon = tk.font("icon"), body = tk.font("body");
  Geo g{};
  const qreal rowW = width - in.left() - in.right();
  g.glyphW = ceilPx(Paint::textWidth(icon, toneGlyph()));
  qreal others = g.glyphW, h = ceilPx(Paint::lineHeight(icon));
  int n = 2;
  if (!m_actionText.isEmpty()) {
    g.actionW = m_action->sizeHint().width();
    others += g.actionW;
    h = qMax<qreal>(h, m_action->sizeHint().height());
    ++n;
  }
  if (m_dismissible) {
    others += m_dismiss->sizeHint().width();
    h = qMax<qreal>(h, m_dismiss->sizeHint().height());
    ++n;
  }
  g.textX = g.glyphW + gap;
  g.textW = qMax<qreal>(0, rowW - others - gap * (n - 1));
  g.textH = ceilPx(Paint::wrappedHeight(body, m_text, g.textW));
  g.actionX = g.textX + g.textW + gap;
  g.dismissX = rowW - m_dismiss->sizeHint().width();
  const int borderMax = qMax(tk.border(State::Normal).maxWidth(), tk.border(State::Hover).maxWidth());
  const qreal buttonMinH = ceilPx(Paint::lineHeight(body)) + tk.spacing("controlPaddingY") * 2 + borderMax * 2;
  g.rowH = qMax<qreal>(qMax<qreal>(tk.spacing("controlHeight"), buttonMinH), qMax(h, g.textH));
  g.height = g.rowH + in.top() + in.bottom();
  g.rowY = in.top();
  return g;
}

QSize Banner::sizeHint() const {
  const Tokens &tk = t();
  const QMargins in = insets();
  qreal w = ceilPx(Paint::textWidth(tk.font("icon"), toneGlyph())) + ceilPx(Paint::textWidth(tk.font("body"), m_text));
  int n = 2;
  if (!m_actionText.isEmpty()) w += m_action->sizeHint().width(), ++n;
  if (m_dismissible) w += m_dismiss->sizeHint().width(), ++n;
  w += tk.spacing("controlGap") * (n - 1) + in.left() + in.right();
  return QSize(ceilPx(w), heightForWidth(ceilPx(w)));
}

QSize Banner::minimumSizeHint() const { return QSize(0, sizeHint().height()); }

int Banner::heightForWidth(int w) const { return ceilPx(geometry(w).height); }

void Banner::place() {
  const Geo g = geometry(width());
  const QMargins in = insets();
  const qreal innerH = qMax<qreal>(g.rowH, height() - in.top() - in.bottom());
  auto centerIn = [&](QWidget *w, qreal x) {
    const QSize s = w->sizeHint();
    w->setGeometry(QRect(int(in.left() + x), int(in.top() + std::round((innerH - s.height()) / 2)), s.width(), s.height()));
  };
  if (!m_actionText.isEmpty()) centerIn(m_action, g.actionX);
  if (m_dismissible) centerIn(m_dismiss, g.dismissX);
  update();
}

void Banner::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QColor tone = toneColor();
  const int bw = qMax(1, tk.border(State::Normal).maxWidth());
  Paint::surface(p, QRectF(rect()), tk.radius(), tk.fill(State::Normal), Border::flat(tone, bw));
  const Geo g = geometry(width());
  const QMargins in = insets();
  const qreal innerH = qMax<qreal>(g.rowH, height() - in.top() - in.bottom());
  const QFont icon = tk.font("icon"), body = tk.font("body");
  const qreal gh = ceilPx(Paint::lineHeight(icon));
  Paint::text(p, QPointF(in.left(), in.top() + std::round((innerH - gh) / 2)), icon, tone, toneGlyph());
  Paint::wrapped(p, QRectF(in.left() + g.textX, in.top() + std::round((innerH - g.textH) / 2), g.textW, g.textH), body, tk.foreground(), m_text);
}

// ----------------------------------------------------------------------------------------- Toast

Toast::Toast(const QString &title, QWidget *parent) : Control(parent), m_title(title) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setAccessibleName(title);
  m_action = new Button(QString(), this);
  m_action->setBordered(true);
  m_action->setFocusable(true);
  m_action->hide();
  connect(m_action, &Button::clicked, this, &Toast::actionClicked);
}

void Toast::setTitle(const QString &t) {
  m_title = t;
  setAccessibleName(t);
  updateGeometry();
  place();
}
void Toast::setText(const QString &t) {
  m_text = t;
  updateGeometry();
  place();
}
void Toast::setGlyph(const QString &g) {
  m_glyph = g;
  updateGeometry();
  place();
}
void Toast::setActionText(const QString &t) {
  m_actionText = t;
  m_action->setText(t);
  m_action->setVisible(!t.isEmpty());
  updateGeometry();
  place();
}
void Toast::setCountdown(qreal left) {
  const bool had = m_countdown >= 0;
  m_countdown = left;
  if (had != (left >= 0)) place();
  else update();
}

static Border toastBorder(const Tokens &tk) { return tk.border(QStringLiteral("surfaces.notifications.border")); }

Toast::Geo Toast::geometry(qreal width) const {
  const Tokens &tk = t();
  const Border b = toastBorder(tk);
  const qreal px = tk.space(12), py = tk.space(10), gap = tk.space(12);
  const QFont glyph = tk.font("iconLarge"), title = tk.font("body", true), body = tk.font("body");
  Geo g{};
  const qreal rowW = width - b.left - b.right - px * 2;
  qreal others = 0, h = 0;
  int n = 1;
  if (!m_glyph.isEmpty()) {
    g.glyphW = ceilPx(Paint::textWidth(glyph, m_glyph));
    others += g.glyphW;
    h = ceilPx(Paint::lineHeight(glyph));
    ++n;
  }
  if (!m_actionText.isEmpty()) {
    g.actionW = m_action->sizeHint().width();
    others += g.actionW;
    h = qMax<qreal>(h, m_action->sizeHint().height());
    ++n;
  }
  g.colX = m_glyph.isEmpty() ? 0 : g.glyphW + gap;
  g.colW = qMax<qreal>(0, rowW - others - gap * (n - 1));
  g.titleH = ceilPx(Paint::lineHeight(title));
  qreal colH = g.titleH;
  if (!m_text.isEmpty()) {
    g.textH = ceilPx(Paint::wrappedHeight(body, m_text, g.colW));
    g.textY = g.titleH + tk.space(3);
    colH = g.textY + g.textH;
  }
  g.actionX = g.colX + g.colW + gap;
  g.rowH = qMax(h, qreal(ceilPx(colH)));
  g.height = g.rowH + b.top + b.bottom + py * 2;
  return g;
}

QSize Toast::sizeHint() const {
  const int w = t().space(360);
  return QSize(w, heightForWidth(w));
}

int Toast::heightForWidth(int w) const { return ceilPx(geometry(w).height); }

void Toast::place() {
  const Tokens &tk = t();
  const Geo g = geometry(width());
  const Border b = toastBorder(tk);
  const qreal py = tk.space(10);
  const qreal line = m_countdown >= 0 ? qMax(1, tk.space(2)) : 0;
  const qreal y0 = b.top + py + std::floor((height() - b.top - b.bottom - py * 2 - line - g.rowH) / 2.0 + 0.5);
  if (!m_actionText.isEmpty()) {
    const QSize s = m_action->sizeHint();
    m_action->setGeometry(QRect(int(b.left + tk.space(12) + g.actionX), int(y0 + std::round((g.rowH - s.height()) / 2)), s.width(), s.height()));
  }
  update();
}

void Toast::paintEvent(QPaintEvent *) {
  const Tokens &tk = t();
  QPainter p(this);
  const QRectF r(rect());
  const Border b = toastBorder(tk);
  const QColor text = tk.color(QStringLiteral("surfaces.notifications.text"));
  Paint::surface(p, r, tk.radius(), tk.color(QStringLiteral("surfaces.notifications.background")), b);
  // The keyboard's toast: the hover fill inside the border, rounded to the inner radius.
  if (hasCursor())
    Paint::surface(p, r.adjusted(b.left, b.top, -b.right, -b.bottom), qMax<qreal>(0, tk.radius() - qMax(b.left, b.top)),
                   tk.fill(State::Hover, text));
  const Geo g = geometry(width());
  const qreal py = tk.space(10);
  const qreal line = m_countdown >= 0 ? qMax(1, tk.space(2)) : 0;
  const qreal x0 = b.left + tk.space(12);
  const qreal y0 = b.top + py + std::floor((r.height() - b.top - b.bottom - py * 2 - line - g.rowH) / 2.0 + 0.5);
  if (!m_glyph.isEmpty()) {
    const QFont f = tk.font("iconLarge");
    const qreal gh = ceilPx(Paint::lineHeight(f));
    Paint::text(p, QPointF(x0, y0 + std::round((g.rowH - gh) / 2)), f, text, m_glyph);
  }
  const qreal colH = m_text.isEmpty() ? g.titleH : g.textY + g.textH;
  const qreal cy = y0 + std::round((g.rowH - ceilPx(colH)) / 2);
  const QFont title = tk.font("body", true);
  Paint::text(p, QPointF(x0 + g.colX, cy), title, text, Paint::elided(title, m_title, g.colW));
  if (!m_text.isEmpty()) Paint::wrapped(p, QRectF(x0 + g.colX, cy + g.textY, g.colW, g.textH), tk.font("body"), Paint::alpha(text, 0.78), m_text);
  if (m_countdown >= 0) {
    const qreal radius = tk.radius();
    const qreal w = (r.width() - b.left - b.right - radius * 2) * qBound(0.0, m_countdown, 1.0);
    Paint::surface(p, QRectF(b.left + radius, r.height() - b.bottom - line, w, line), 0, tk.color(QStringLiteral("surfaces.notifications.countdown")));
  }
}

}  // namespace Omarchy
