#include "Omarchy/Style.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGroupBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QTextEdit>
#include <QToolButton>
#include <cmath>

#include "Omarchy/Paint.h"

namespace Omarchy {

namespace {

const Tokens &tk() { return Theme::instance()->shown(); }

QString chevronDown() { return QString::fromUcs4(U"\U000F0140", 1); }

// The kit's control states from a style option: focus looks like hover.
struct Look {
  bool pressed, focus, hot, on, enabled;
};

Look look(const QStyleOption *opt) {
  const QStyle::State s = opt->state;
  Look l;
  l.enabled = s & QStyle::State_Enabled;
  l.pressed = l.enabled && (s & QStyle::State_Sunken);
  l.focus = l.enabled && (s & QStyle::State_HasFocus) && (s & QStyle::State_KeyboardFocusChange);
  l.hot = l.enabled && (s & QStyle::State_MouseOver);
  l.on = s & QStyle::State_On;
  return l;
}

// An input (TextField, Dropdown, NumberField): focus > hover > normal glass with its border.
void input(QPainter *p, const QRect &r, const QStyleOption *opt, bool keyboardOnlyFocus = false) {
  const Tokens &t = tk();
  const QStyle::State s = opt->state;
  const bool enabled = s & QStyle::State_Enabled;
  const bool focus = enabled && (s & QStyle::State_HasFocus) && (!keyboardOnlyFocus || (s & QStyle::State_KeyboardFocusChange));
  const bool hot = enabled && (s & QStyle::State_MouseOver);
  const Omarchy::State st = focus ? Omarchy::State::Focus : hot ? Omarchy::State::Hover : Omarchy::State::Normal;
  QColor fill = t.fill(st);
  if (!enabled) fill = Paint::alpha(fill, fill.alphaF() * 0.5);
  Paint::surface(*p, QRectF(r), t.radius(), fill, t.border(st));
}

// A Button (bordered or plain): pressed > focus > hover > selected > rest.
void button(QPainter *p, const QRect &r, const QStyleOption *opt, bool bordered) {
  const Tokens &t = tk();
  const Look l = look(opt);
  QColor fill = Qt::transparent;
  Border border;
  if (l.pressed) fill = t.fill(Omarchy::State::Pressed);
  else if (l.focus) fill = t.fill(Omarchy::State::Focus);
  else if (l.hot) fill = t.fill(Omarchy::State::Hover);
  else if (l.on) fill = t.fill(Omarchy::State::Selected);
  if (l.focus) border = t.border(Omarchy::State::Focus);
  else if (l.hot) border = t.border(Omarchy::State::Hover);
  else if (bordered) border = t.border(Omarchy::State::Normal);
  Paint::surface(*p, QRectF(r), t.radius(), fill, border);
}

QColor textColor(const QStyleOption *opt) {
  return (opt->state & QStyle::State_Enabled) ? tk().foreground() : tk().textFaint();
}

void glyph(QPainter *p, const QRect &r, const QString &g, const QColor &c, int px) {
  const QFont f = tk().fontPx(px);
  const QSizeF s = Paint::textSize(f, g);
  Paint::text(*p, QPointF(r.x() + Paint::centered(r.width(), s.width()), r.y() + Paint::centered(r.height(), s.height())), f, c, g);
}

// A chevron drawn with lines (the arrows of spin boxes, trees, menus), `dir` 0 down, 1 up, 2 left, 3 right.
void chevron(QPainter *p, const QRectF &r, int dir, const QColor &c) {
  p->save();
  p->setRenderHint(QPainter::Antialiasing, true);
  QPen pen(c, qMax<qreal>(1.0, tk().space(1.5)));
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  p->setPen(pen);
  const qreal s = qMin(r.width(), r.height()) * 0.28;
  const QPointF o = r.center();
  QPolygonF poly;
  switch (dir) {
  case 0: poly << o + QPointF(-s, -s / 2) << o + QPointF(0, s / 2) << o + QPointF(s, -s / 2); break;
  case 1: poly << o + QPointF(-s, s / 2) << o + QPointF(0, -s / 2) << o + QPointF(s, s / 2); break;
  case 2: poly << o + QPointF(s / 2, -s) << o + QPointF(-s / 2, 0) << o + QPointF(s / 2, s); break;
  default: poly << o + QPointF(-s / 2, -s) << o + QPointF(s / 2, 0) << o + QPointF(-s / 2, s); break;
  }
  p->drawPolyline(poly);
  p->restore();
}

// A label without its mnemonic marks ("&Save" -> "Save", "&&" -> "&"): shortcuts aren't
// underlined (SH_UnderlineShortcut), as in the rest of Omarchy.
QString plain(const QString &text) {
  QString out;
  out.reserve(text.size());
  for (int i = 0; i < text.size(); ++i) {
    if (text[i] == QLatin1Char('&') && i + 1 < text.size()) ++i;
    out += text[i];
  }
  return out;
}

int controlHeight() { return tk().spacing("controlHeight"); }
int borderWidth() { return qMax(tk().border(Omarchy::State::Normal).maxWidth(), tk().border(Omarchy::State::Hover).maxWidth()); }

bool isPopup(const QWidget *w) {
  return w && (qobject_cast<const QMenu *>(w) || w->inherits("QTipLabel") || w->inherits("QComboBoxPrivateContainer"));
}

}  // namespace

Style::Style() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {
  setObjectName(QStringLiteral("omarchy"));
}

void Style::polish(QPalette &pal) { QProxyStyle::polish(pal); }

void Style::polish(QWidget *w) {
  QProxyStyle::polish(w);
  if (qobject_cast<QAbstractButton *>(w) || qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractSpinBox *>(w) ||
      qobject_cast<QLineEdit *>(w) || qobject_cast<QSlider *>(w) || qobject_cast<QScrollBar *>(w) || qobject_cast<QTabBar *>(w) ||
      qobject_cast<QGroupBox *>(w) || qobject_cast<QHeaderView *>(w) || qobject_cast<QMenuBar *>(w))
    w->setAttribute(Qt::WA_Hover);
  if (auto *view = qobject_cast<QAbstractItemView *>(w)) view->viewport()->setAttribute(Qt::WA_Hover);
  if (isPopup(w)) {
    // Rounded popups draw their own corners over nothing.
    w->setAttribute(Qt::WA_TranslucentBackground);
    if (w->inherits("QTipLabel")) w->setFont(tk().font("bodySmall"));
  }
  if (auto *menu = qobject_cast<QMenu *>(w)) menu->setFont(tk().font("body"));
}

void Style::unpolish(QWidget *w) {
  if (isPopup(w)) w->setAttribute(Qt::WA_TranslucentBackground, false);
  QProxyStyle::unpolish(w);
}

bool Style::eventFilter(QObject *o, QEvent *e) { return QProxyStyle::eventFilter(o, e); }

int Style::pixelMetric(PixelMetric m, const QStyleOption *opt, const QWidget *w) const {
  const Tokens &t = tk();
  switch (m) {
  case PM_ButtonMargin: return t.spacing("controlPaddingX");
  case PM_DefaultFrameWidth: return borderWidth();
  case PM_ButtonDefaultIndicator:
  case PM_ButtonShiftHorizontal:
  case PM_ButtonShiftVertical: return 0;
  // Item views inset their text by this plus one.
  case PM_FocusFrameHMargin: return qobject_cast<const QAbstractItemView *>(w) ? t.spacing("controlPaddingX") - 1 : 0;
  case PM_FocusFrameVMargin: return 0;
  case PM_ComboBoxFrameWidth:
  case PM_SpinBoxFrameWidth: return borderWidth();
  case PM_IndicatorWidth:
  case PM_IndicatorHeight:
  case PM_ExclusiveIndicatorWidth:
  case PM_ExclusiveIndicatorHeight: return t.space(16);
  case PM_CheckBoxLabelSpacing:
  case PM_RadioButtonLabelSpacing: return t.spacing("rowGap");
  case PM_SliderThickness: return qMax(t.space(22), qMax(14, int(std::lround(controlHeight() * 0.38))) + t.spacing("md"));
  case PM_SliderLength: return qMax(14, int(std::lround(controlHeight() * 0.38)));
  case PM_SliderControlThickness: return qMax(14, int(std::lround(controlHeight() * 0.38)));
  case PM_SliderTickmarkOffset: return 0;
  case PM_ScrollBarExtent: return t.spacing("scrollbarThickness");
  case PM_ScrollBarSliderMin: return t.space(24);
  case PM_ScrollView_ScrollBarSpacing: return 0;
  case PM_ScrollView_ScrollBarOverlap: return 0;
  case PM_TabBarTabHSpace: return t.spacing("controlPaddingX") * 2;
  case PM_TabBarTabVSpace: return t.spacing("controlPaddingY") * 2;
  case PM_TabBarTabOverlap: return 0;
  case PM_TabBarBaseOverlap: return 1;
  case PM_TabBarBaseHeight: return 1;
  case PM_TabBarTabShiftHorizontal:
  case PM_TabBarTabShiftVertical: return 0;
  case PM_TabBar_ScrollButtonOverlap: return 0;
  case PM_MenuPanelWidth: return t.border(QStringLiteral("surfaces.menu.border")).maxWidth();
  case PM_MenuHMargin:
  case PM_MenuVMargin: return t.spacing("sm");
  case PM_SubMenuOverlap: return 0;
  case PM_MenuBarPanelWidth: return 0;
  case PM_MenuBarItemSpacing: return t.spacing("xxs");
  case PM_MenuBarHMargin:
  case PM_MenuBarVMargin: return t.spacing("xxs");
  case PM_ToolTipLabelFrameWidth: return t.border(QStringLiteral("surfaces.tooltip.border")).maxWidth() + t.spacing("controlPaddingY");
  case PM_ToolBarFrameWidth: return 0;
  case PM_ToolBarItemSpacing: return t.spacing("xxs");
  case PM_ToolBarItemMargin: return t.spacing("xxs");
  case PM_ToolBarSeparatorExtent: return t.spacing("controlGap") + 1;
  case PM_SplitterWidth: return t.spacing("hairline");
  case PM_HeaderMargin: return t.spacing("sm");
  case PM_LayoutHorizontalSpacing:
  case PM_LayoutVerticalSpacing: return t.spacing("controlGap");
  case PM_LayoutLeftMargin:
  case PM_LayoutRightMargin:
  case PM_LayoutTopMargin:
  case PM_LayoutBottomMargin: return (!w || w->isWindow()) ? t.spacing("panelPadding") : t.spacing("lg");
  case PM_SmallIconSize:
  case PM_ButtonIconSize:
  case PM_ListViewIconSize:
  case PM_TabBarIconSize: return t.fontSize("iconLarge");
  case PM_ToolBarIconSize: return t.fontSize("iconLarge");
  case PM_ProgressBarChunkWidth: return 1;
  default: break;
  }
  return QProxyStyle::pixelMetric(m, opt, w);
}

int Style::styleHint(StyleHint h, const QStyleOption *opt, const QWidget *w, QStyleHintReturn *ret) const {
  switch (h) {
  case SH_ComboBox_Popup: return 0;
  case SH_ComboBox_ListMouseTracking:
  case SH_Menu_MouseTracking:
  case SH_MenuBar_MouseTracking: return 1;
  case SH_Menu_SubMenuPopupDelay: return 150;
  case SH_ToolTip_WakeUpDelay: return 400;
  case SH_FocusFrame_AboveWidget: return 0;
  case SH_ItemView_ShowDecorationSelected: return 1;
  case SH_ScrollBar_Transient: return 0;
  case SH_ScrollBar_LeftClickAbsolutePosition: return 1;
  case SH_Slider_AbsoluteSetButtons: return Qt::LeftButton;
  case SH_DialogButtonBox_ButtonsHaveIcons: return 0;
  case SH_EtchDisabledText:
  case SH_DitherDisabledText: return 0;
  case SH_UnderlineShortcut: return 0;
  case SH_Widget_Animation_Duration: return tk().duration(tk().number(QStringLiteral("motion.fast"), 120));
  case SH_TabBar_Alignment: return Qt::AlignLeft;
  case SH_Table_GridLineColor: return int(tk().track().rgba());
  default: break;
  }
  return QProxyStyle::styleHint(h, opt, w, ret);
}

QSize Style::sizeFromContents(ContentsType ct, const QStyleOption *opt, const QSize &size, const QWidget *w) const {
  const Tokens &t = tk();
  const int bw = borderWidth();
  const int lineH = int(std::ceil(Paint::lineHeight(t.font("body")) - 1e-6));
  switch (ct) {
  case CT_PushButton: {
    // The kit's Button: padding controlPaddingX × controlPaddingY inside the reserved border.
    QSize s = size;
    const auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt);
    int wd = s.width() + 2 * (t.spacing("controlPaddingX") + bw);
    if (b && !b->icon.isNull() && !b->text.isEmpty()) wd += t.spacing("controlGap") - 4;
    return QSize(wd, qMax(controlHeight(), qMax(s.height(), lineH) + 2 * (t.spacing("controlPaddingY") + bw)));
  }
  case CT_ToolButton: {
    const int pad = t.spacing("sm");
    return QSize(size.width() + 2 * (pad + bw), qMax(size.height() + 2 * (pad + bw), t.space(22)));
  }
  case CT_LineEdit: {
    // TextField: controlPaddingX × inputPaddingY inside the border.
    return QSize(size.width(), qMax(controlHeight(), lineH + 2 * (t.spacing("inputPaddingY") + bw)));
  }
  case CT_ComboBox: {
    const int chevronRoom = t.spacing("md") + int(std::ceil(Paint::textWidth(t.font("body"), chevronDown()))) + t.spacing("controlGap");
    return QSize(size.width() + 2 * bw + t.spacing("controlPaddingX") + chevronRoom, qMax(controlHeight(), lineH + 2 * (t.spacing("controlPaddingY") + bw)));
  }
  case CT_SpinBox: {
    const int h = qMax(controlHeight(), lineH + 2 * t.spacing("controlPaddingY"));
    return QSize(size.width() + 2 * h + 2 * bw, h);
  }
  case CT_CheckBox:
  case CT_RadioButton: {
    QSize s = QProxyStyle::sizeFromContents(ct, opt, size, w);
    return QSize(s.width(), qMax(s.height(), t.space(16)));
  }
  case CT_MenuItem: {
    const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt);
    if (mi && mi->menuItemType == QStyleOptionMenuItem::Separator) return QSize(size.width(), 2 * t.spacing("xs") + 1);
    const int pad = t.spacing("controlPaddingX"), gap = t.spacing("controlGap"), box = t.fontSize("icon");
    int wd = size.width() + 2 * pad + 2 * (box + gap);
    if (mi && mi->text.contains(QLatin1Char('\t'))) wd += gap * 2;
    return QSize(wd, t.spacing("popupRowHeight"));
  }
  case CT_MenuBarItem: return QSize(size.width() + 2 * t.spacing("controlPaddingX"), qMax(size.height(), lineH) + 2 * t.spacing("xs"));
  case CT_TabBarTab: {
    const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(opt);
    const QFont bold = t.font("body", true);
    const qreal tw = tab ? Paint::textWidth(bold, tab->text) : size.width();
    return QSize(int(std::ceil(tw - 1e-6)) + 2 * t.spacing("controlPaddingX") + (tab && !tab->icon.isNull() ? t.fontSize("icon") + t.spacing("controlGap") : 0),
                 lineH + 2 * t.spacing("controlPaddingY") + t.spacing("xxs"));
  }
  case CT_ProgressBar: {
    QSize s = QProxyStyle::sizeFromContents(ct, opt, size, w);
    const auto *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(opt);
    if (pb && !pb->textVisible) return QSize(s.width(), t.spacing("meterThickness"));
    return QSize(s.width(), qMax(t.spacing("meterThickness"), lineH));
  }
  case CT_HeaderSection: return QSize(size.width() + 2 * t.spacing("controlPaddingX"), qMax(size.height(), lineH) + 2 * t.spacing("sm"));
  case CT_ItemViewItem: {
    QSize s = QProxyStyle::sizeFromContents(ct, opt, size, w);
    return QSize(s.width() + 2 * t.spacing("sm"), qMax(s.height() + 2 * t.spacing("xxs"), t.spacing("popupRowHeight") - t.spacing("xs")));
  }
  default: break;
  }
  return QProxyStyle::sizeFromContents(ct, opt, size, w);
}

QRect Style::subElementRect(SubElement se, const QStyleOption *opt, const QWidget *w) const {
  const Tokens &t = tk();
  const int bw = borderWidth();
  switch (se) {
  case SE_LineEditContents:
    if (const auto *f = qstyleoption_cast<const QStyleOptionFrame *>(opt); f && f->lineWidth <= 0) return opt->rect;
    return opt->rect.adjusted(bw + t.spacing("controlPaddingX"), bw, -(bw + t.spacing("controlPaddingX")), -bw);
  case SE_PushButtonContents: return opt->rect.adjusted(bw + t.spacing("controlPaddingX"), bw, -(bw + t.spacing("controlPaddingX")), -bw);
  case SE_PushButtonFocusRect: return QRect();
  case SE_ProgressBarGroove:
  case SE_ProgressBarContents: {
    const auto *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(opt);
    QRect r = opt->rect;
    if (pb && pb->textVisible) {
      const int tw = int(std::ceil(Paint::textWidth(t.font("caption"), QStringLiteral("100%")))) + t.spacing("controlGap");
      r.setRight(r.right() - tw);
    }
    const int th = t.spacing("meterThickness");
    return QRect(r.x(), r.y() + (r.height() - th) / 2, r.width(), th);
  }
  case SE_ProgressBarLabel: {
    const int tw = int(std::ceil(Paint::textWidth(t.font("caption"), QStringLiteral("100%"))));
    return QRect(opt->rect.right() - tw + 1, opt->rect.y(), tw, opt->rect.height());
  }
  case SE_TabBarTabText: return opt->rect;
  case SE_ItemViewItemFocusRect: return QRect();
  default: break;
  }
  return QProxyStyle::subElementRect(se, opt, w);
}

QRect Style::subControlRect(ComplexControl cc, const QStyleOptionComplex *opt, SubControl sc, const QWidget *w) const {
  const Tokens &t = tk();
  const int bw = borderWidth();
  const QRect r = opt->rect;
  switch (cc) {
  case CC_ComboBox: {
    // Dropdown: the label from controlPaddingX, the chevron controlGap from the right edge.
    const int chev = int(std::ceil(Paint::textWidth(t.font("body"), chevronDown())));
    const int arrowW = chev + t.spacing("controlGap") + bw;
    switch (sc) {
    case SC_ComboBoxFrame: return r;
    case SC_ComboBoxArrow: return QRect(r.right() + 1 - arrowW, r.y(), arrowW, r.height());
    case SC_ComboBoxEditField: return QRect(r.x() + bw + t.spacing("controlPaddingX"), r.y() + bw, r.width() - arrowW - bw - t.spacing("controlPaddingX") - t.spacing("md"), r.height() - 2 * bw);
    case SC_ComboBoxListBoxPopup: return r;
    default: break;
    }
    break;
  }
  case CC_SpinBox: {
    // NumberField: [−] value [+], each step a square inside the border.
    const int side = r.height() - 2 * bw;
    switch (sc) {
    case SC_SpinBoxFrame: return r;
    case SC_SpinBoxDown: return QRect(r.x() + bw, r.y() + bw, side, side);
    case SC_SpinBoxUp: return QRect(r.right() + 1 - bw - side, r.y() + bw, side, side);
    case SC_SpinBoxEditField: return QRect(r.x() + bw + side, r.y() + bw, r.width() - 2 * (bw + side), r.height() - 2 * bw);
    default: break;
    }
    break;
  }
  case CC_ScrollBar: {
    // A thin bar with no buttons.
    const auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(opt);
    if (!sb) break;
    const bool horiz = sb->orientation == Qt::Horizontal;
    const int len = horiz ? r.width() : r.height();
    switch (sc) {
    case SC_ScrollBarAddLine:
    case SC_ScrollBarSubLine: return QRect();
    case SC_ScrollBarGroove: return r;
    case SC_ScrollBarSlider: {
      const int range = sb->maximum - sb->minimum;
      int sl = range > 0 ? int(qint64(len) * sb->pageStep / (range + sb->pageStep)) : len;
      sl = qBound(qMin(len, pixelMetric(PM_ScrollBarSliderMin, opt, w)), sl, len);
      const int pos = range > 0 ? sliderPositionFromValue(sb->minimum, sb->maximum, sb->sliderPosition, len - sl, sb->upsideDown) : 0;
      return horiz ? QRect(r.x() + pos, r.y(), sl, r.height()) : QRect(r.x(), r.y() + pos, r.width(), sl);
    }
    case SC_ScrollBarAddPage:
    case SC_ScrollBarSubPage: {
      const QRect s = subControlRect(cc, opt, SC_ScrollBarSlider, w);
      if (sc == SC_ScrollBarSubPage) return horiz ? QRect(r.x(), r.y(), s.x() - r.x(), r.height()) : QRect(r.x(), r.y(), r.width(), s.y() - r.y());
      return horiz ? QRect(s.right() + 1, r.y(), r.right() - s.right(), r.height()) : QRect(r.x(), s.bottom() + 1, r.width(), r.bottom() - s.bottom());
    }
    default: break;
    }
    break;
  }
  case CC_Slider: {
    const auto *sl = qstyleoption_cast<const QStyleOptionSlider *>(opt);
    if (!sl) break;
    const int knob = pixelMetric(PM_SliderLength, opt, w);
    const bool horiz = sl->orientation == Qt::Horizontal;
    const int len = (horiz ? r.width() : r.height()) - knob;
    if (sc == SC_SliderHandle) {
      const int pos = sliderPositionFromValue(sl->minimum, sl->maximum, sl->sliderPosition, len, horiz ? sl->upsideDown : !sl->upsideDown);
      return horiz ? QRect(r.x() + pos, r.y() + (r.height() - knob) / 2, knob, knob) : QRect(r.x() + (r.width() - knob) / 2, r.y() + pos, knob, knob);
    }
    if (sc == SC_SliderGroove) {
      const int th = qMax(4, int(std::lround(controlHeight() * 0.11)));
      return horiz ? QRect(r.x() + knob / 2, r.y() + (r.height() - th) / 2, r.width() - knob, th)
                   : QRect(r.x() + (r.width() - th) / 2, r.y() + knob / 2, th, r.height() - knob);
    }
    break;
  }
  case CC_GroupBox: {
    const auto *gb = qstyleoption_cast<const QStyleOptionGroupBox *>(opt);
    if (!gb) break;
    const QFont f = t.font("caption", true);
    const int titleH = int(std::ceil(Paint::lineHeight(f) - 1e-6));
    const bool checkable = gb->subControls & SC_GroupBoxCheckBox;
    const int box = t.space(16);
    const int titleRow = qMax(titleH, checkable ? box : 0);
    switch (sc) {
    case SC_GroupBoxFrame: return gb->text.isEmpty() && !checkable ? r : r.adjusted(0, titleRow + t.spacing("lg"), 0, 0);
    case SC_GroupBoxContents: {
      const QRect frame = subControlRect(cc, opt, SC_GroupBoxFrame, w);
      const int pad = t.spacing("panelPadding") / 2 + bw;
      return frame.adjusted(pad, pad, -pad, -pad);
    }
    case SC_GroupBoxCheckBox: return checkable ? QRect(r.x(), r.y() + (titleRow - box) / 2, box, box) : QRect();
    case SC_GroupBoxLabel: {
      const int x = r.x() + (checkable ? box + t.spacing("rowGap") : 0);
      return QRect(x, r.y() + (titleRow - titleH) / 2, int(std::ceil(Paint::textWidth(f, gb->text))) + 1, titleH);
    }
    default: break;
    }
    break;
  }
  default: break;
  }
  return QProxyStyle::subControlRect(cc, opt, sc, w);
}

void Style::drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p, const QWidget *w) const {
  const Tokens &t = tk();
  switch (pe) {
  case PE_FrameFocusRect:
  case PE_FrameDefaultButton:
  case PE_FrameButtonBevel:
  case PE_FrameButtonTool:
  case PE_IndicatorToolBarHandle: return;
  case PE_PanelButtonCommand: {
    const auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt);
    button(p, opt->rect, opt, !(b && (b->features & QStyleOptionButton::Flat)));
    return;
  }
  case PE_PanelButtonTool: {
    const bool autoRaise = qobject_cast<const QToolButton *>(w) ? static_cast<const QToolButton *>(w)->autoRaise() : true;
    button(p, opt->rect, opt, !autoRaise);
    return;
  }
  case PE_PanelLineEdit: {
    // A line edit inside a spin box or editable combo is drawn by them.
    if (w && (qobject_cast<const QAbstractSpinBox *>(w->parentWidget()) || qobject_cast<const QComboBox *>(w->parentWidget()))) return;
    const auto *f = qstyleoption_cast<const QStyleOptionFrame *>(opt);
    if (f && f->lineWidth <= 0) return;
    input(p, opt->rect, opt);
    return;
  }
  case PE_FrameLineEdit: return;
  case PE_IndicatorCheckBox:
  case PE_IndicatorRadioButton:
  case PE_IndicatorItemViewItemCheck: {
    // Checkbox / Radio: a box (a circle for a radio), the hover tint under the cursor, the
    // selected fill when checked, an accent check or dot.
    const bool exclusive = pe == PE_IndicatorRadioButton;
    const QStyle::State s = opt->state;
    const bool enabled = s & State_Enabled;
    const bool hot = enabled && ((s & State_MouseOver) || ((s & State_HasFocus) && (s & State_KeyboardFocusChange)));
    const bool checked = s & (State_On | State_NoChange);
    const int box = qMin(opt->rect.width(), opt->rect.height());
    const QRect r(opt->rect.x() + (opt->rect.width() - box) / 2, opt->rect.y() + (opt->rect.height() - box) / 2, box, box);
    const Omarchy::State st = hot ? Omarchy::State::Hover : checked ? Omarchy::State::Selected : Omarchy::State::Normal;
    p->save();
    if (!enabled) p->setOpacity(0.5);
    Paint::surface(*p, QRectF(r), exclusive ? box / 2.0 : t.smallRadius(), t.fill(st), t.border(st));
    if (checked && exclusive) {
      const int dot = int(std::lround(box / 2.0));
      p->setRenderHint(QPainter::Antialiasing);
      p->setPen(Qt::NoPen);
      p->setBrush(t.accent());
      p->drawEllipse(QRectF(r.x() + Paint::centered(box, dot), r.y() + Paint::centered(box, dot), dot, dot));
    } else if (s & State_NoChange) {
      p->fillRect(QRectF(r.x() + box * 0.25, r.y() + box / 2.0 - 1, box * 0.5, 2), t.accent());
    } else if (checked) {
      QFont f = t.fontPx(std::lround(box * 0.85), true);
      const QString check = QStringLiteral("✓");
      const QSizeF ts = Paint::textSize(f, check);
      Paint::text(*p, QPointF(r.x() + Paint::centered(box, ts.width()), r.y() + Paint::centered(box, ts.height())), f, t.accent(), check);
    }
    p->restore();
    return;
  }
  case PE_FrameMenu:
  case PE_PanelMenu: {
    if (pe == PE_FrameMenu) return;
    Paint::surface(*p, QRectF(opt->rect), t.radius(), t.color(QStringLiteral("surfaces.menu.background")), t.border(QStringLiteral("surfaces.menu.border")));
    return;
  }
  case PE_PanelMenuBar: p->fillRect(opt->rect, t.background()); return;
  case PE_PanelTipLabel: {
    Paint::surface(*p, QRectF(opt->rect), t.radius(), t.color(QStringLiteral("surfaces.tooltip.background")), t.border(QStringLiteral("surfaces.tooltip.border")));
    return;
  }
  case PE_Frame:
  case PE_FrameWindow: {
    if (w && w->inherits("QComboBoxPrivateContainer")) {
      // The Dropdown's list: the popups surface.
      Paint::surface(*p, QRectF(opt->rect), t.radius(), t.color(QStringLiteral("surfaces.popups.background")), t.border(QStringLiteral("surfaces.popups.border")).withWidth(t.border(Omarchy::State::Normal).top));
      return;
    }
    Paint::surface(*p, QRectF(opt->rect), t.radius(), Qt::transparent, t.border(Omarchy::State::Normal));
    return;
  }
  case PE_FrameGroupBox: {
    Paint::surface(*p, QRectF(opt->rect), t.radius(), t.fill(Omarchy::State::Normal), t.border(Omarchy::State::Normal));
    return;
  }
  case PE_FrameTabWidget: {
    p->fillRect(QRect(opt->rect.x(), opt->rect.y(), opt->rect.width(), 1), t.track());
    return;
  }
  case PE_FrameTabBarBase: {
    p->fillRect(QRect(opt->rect.x(), opt->rect.bottom(), opt->rect.width(), 1), t.track());
    return;
  }
  case PE_FrameStatusBarItem: return;
  case PE_PanelStatusBar: p->fillRect(opt->rect, t.background()); return;
  case PE_PanelToolBar: p->fillRect(opt->rect, t.background()); return;
  case PE_IndicatorToolBarSeparator: {
    const QRect r = opt->rect;
    if (opt->state & State_Horizontal) p->fillRect(QRect(r.center().x(), r.y() + t.spacing("xs"), 1, r.height() - 2 * t.spacing("xs")), t.track());
    else p->fillRect(QRect(r.x() + t.spacing("xs"), r.center().y(), r.width() - 2 * t.spacing("xs"), 1), t.track());
    return;
  }
  case PE_PanelItemViewItem:
  case PE_PanelItemViewRow: {
    const auto *v = qstyleoption_cast<const QStyleOptionViewItem *>(opt);
    if (pe == PE_PanelItemViewRow) {
      if (v && (v->features & QStyleOptionViewItem::Alternate)) p->fillRect(opt->rect, t.fill(Omarchy::State::Normal));
      if (!(opt->state & State_Selected) && !(opt->state & State_MouseOver)) return;
      if (w && !static_cast<const QAbstractItemView *>(w)->selectionBehavior()) return;
    }
    const bool selected = opt->state & State_Selected;
    const bool hot = (opt->state & State_MouseOver) && (opt->state & State_Enabled);
    QColor fill = Qt::transparent;
    if (selected) fill = t.color(QStringLiteral("surfaces.menu.selectedBackground"));
    const qreal cellRadius = pe == PE_PanelItemViewRow ? t.smallRadius() : 0;
    if (hot) fill = t.fill(Omarchy::State::Hover);
    if (selected && hot) fill = t.fill(Omarchy::State::Selected);
    if (fill.alpha()) Paint::surface(*p, QRectF(opt->rect), cellRadius, fill);
    return;
  }
  case PE_IndicatorBranch: {
    if (!(opt->state & State_Children)) return;
    chevron(p, QRectF(opt->rect), (opt->state & State_Open) ? 0 : 3, t.textDim());
    return;
  }
  case PE_IndicatorArrowDown:
  case PE_IndicatorArrowUp:
  case PE_IndicatorArrowLeft:
  case PE_IndicatorArrowRight: {
    const int dir = pe == PE_IndicatorArrowDown ? 0 : pe == PE_IndicatorArrowUp ? 1 : pe == PE_IndicatorArrowLeft ? 2 : 3;
    chevron(p, QRectF(opt->rect), dir, textColor(opt));
    return;
  }
  case PE_IndicatorHeaderArrow: {
    const auto *h = qstyleoption_cast<const QStyleOptionHeader *>(opt);
    if (h) chevron(p, QRectF(opt->rect), h->sortIndicator == QStyleOptionHeader::SortUp ? 1 : 0, t.textDim());
    return;
  }
  case PE_IndicatorSpinUp:
  case PE_IndicatorSpinPlus:
  case PE_IndicatorSpinDown:
  case PE_IndicatorSpinMinus: {
    const bool up = pe == PE_IndicatorSpinUp || pe == PE_IndicatorSpinPlus;
    glyph(p, opt->rect, up ? QStringLiteral("+") : QStringLiteral("−"), textColor(opt), t.fontSize("body"));
    return;
  }
  default: break;
  }
  QProxyStyle::drawPrimitive(pe, opt, p, w);
}

void Style::drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p, const QWidget *w) const {
  const Tokens &t = tk();
  switch (ce) {
  case CE_PushButtonBevel: drawPrimitive(PE_PanelButtonCommand, opt, p, w); return;
  case CE_PushButtonLabel: {
    const auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt);
    if (!b) break;
    // Selected (checked) labels are bold; everything centered as the kit's Button centers.
    const bool on = b->state & State_On;
    QFont f = w ? w->font() : t.font("body");
    f.setBold(on || f.bold());
    const QColor c = textColor(opt);
    QRect r = b->rect;
    const QString label = plain(b->text);
    const qreal tw = Paint::textWidth(f, label);
    const int iconW = b->icon.isNull() ? 0 : b->iconSize.width();
    const qreal gap = (iconW && !b->text.isEmpty()) ? t.spacing("controlGap") : 0;
    const qreal rowW = iconW + gap + tw;
    qreal x = r.x() + Paint::centered(r.width(), rowW);
    if (iconW) {
      const QPixmap pm = b->icon.pixmap(b->iconSize, p->device()->devicePixelRatioF(), (b->state & State_Enabled) ? QIcon::Normal : QIcon::Disabled);
      p->drawPixmap(QPointF(x, r.y() + Paint::centered(r.height(), b->iconSize.height())), pm);
      x += iconW + gap;
    }
    const qreal lh = Paint::lineHeight(f);
    Paint::text(*p, QPointF(x, r.y() + Paint::centered(r.height(), lh)), f, c, label);
    if (b->features & QStyleOptionButton::HasMenu) chevron(p, QRectF(r.right() - r.height() / 2.0, r.y(), r.height() / 2.0, r.height()), 0, c);
    return;
  }
  case CE_CheckBoxLabel:
  case CE_RadioButtonLabel: {
    const auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt);
    if (!b) break;
    const QFont f = w ? w->font() : t.font("body");
    Paint::textIn(*p, QRectF(b->rect), Qt::AlignLeft | Qt::AlignVCenter, f, textColor(opt), plain(b->text));
    return;
  }
  case CE_MenuItem: {
    const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt);
    if (!mi) break;
    const QRect r = mi->rect;
    if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
      p->fillRect(QRect(r.x() + t.spacing("sm"), r.y() + r.height() / 2, r.width() - 2 * t.spacing("sm"), 1), t.track());
      return;
    }
    const bool enabled = mi->state & State_Enabled;
    const bool hot = enabled && (mi->state & State_Selected);
    const bool pressed = hot && (mi->state & State_Sunken);
    if (pressed || hot) Paint::surface(*p, QRectF(r), t.smallRadius(), pressed ? t.fill(Omarchy::State::Pressed) : t.color(QStringLiteral("surfaces.menu.selectedBackground")));
    const QColor ink = !enabled ? t.textFaint() : hot ? t.color(QStringLiteral("surfaces.menu.selectedText")) : t.color(QStringLiteral("surfaces.menu.text"));
    const int pad = t.spacing("controlPaddingX"), gap = t.spacing("controlGap"), box = t.fontSize("icon");
    const QFont f = t.font("body");
    int x = r.x() + pad;
    if (!mi->icon.isNull()) {
      const int s = qMin(box, r.height());
      const QPixmap pm = mi->icon.pixmap(QSize(s, s), p->device()->devicePixelRatioF(), enabled ? QIcon::Normal : QIcon::Disabled);
      p->drawPixmap(QPointF(x, r.y() + Paint::centered(r.height(), s)), pm);
    }
    if (mi->maxIconWidth > 0 || !mi->icon.isNull()) x += box + gap;
    const QStringList parts = plain(mi->text).split(QLatin1Char('\t'));
    const qreal lh = Paint::lineHeight(f);
    const int markRoom = box + gap;
    Paint::textIn(*p, QRectF(x, r.y() + Paint::centered(r.height(), lh), r.right() - x - pad - markRoom, lh), Qt::AlignLeft | Qt::AlignTop, f, ink, parts.value(0));
    if (parts.size() > 1) {
      const QFont sf = t.font("bodySmall");
      const qreal sh = Paint::lineHeight(sf);
      Paint::textIn(*p, QRectF(r.x(), r.y() + Paint::centered(r.height(), sh), r.width() - pad - markRoom, sh), Qt::AlignRight | Qt::AlignTop, sf,
                    enabled ? t.textDim() : t.textFaint(), parts.value(1));
    }
    const QRect mark(r.right() + 1 - pad - box, r.y(), box, r.height());
    if (mi->menuItemType == QStyleOptionMenuItem::SubMenu) {
      chevron(p, QRectF(mark), 3, enabled ? t.textDim() : t.textFaint());
    } else if (mi->checkType != QStyleOptionMenuItem::NotCheckable && mi->checked) {
      const bool radio = mi->checkType == QStyleOptionMenuItem::Exclusive;
      glyph(p, mark, radio ? QStringLiteral("●") : QStringLiteral("✓"), enabled ? t.accent() : t.textFaint(), radio ? t.fontSize("caption") : t.fontSize("body"));
    }
    return;
  }
  case CE_MenuBarEmptyArea: p->fillRect(opt->rect, t.background()); return;
  case CE_MenuBarItem: {
    const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt);
    if (!mi) break;
    p->fillRect(opt->rect, t.background());
    const bool hot = (mi->state & State_Selected) && (mi->state & State_Enabled);
    const bool pressed = mi->state & State_Sunken;
    if (hot || pressed) Paint::surface(*p, QRectF(opt->rect), t.smallRadius(), pressed ? t.fill(Omarchy::State::Pressed) : t.fill(Omarchy::State::Hover));
    const QFont f = w ? w->font() : t.font("body");
    const QString text = plain(mi->text);
    Paint::textIn(*p, QRectF(opt->rect), Qt::AlignCenter, f, textColor(opt), text);
    return;
  }
  case CE_TabBarTabShape: {
    // Tabs: text tabs, a hover fill, the selected one marked by an accent underline.
    const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(opt);
    if (!tab) break;
    const bool selected = tab->state & State_Selected;
    const bool hot = (tab->state & State_MouseOver) && (tab->state & State_Enabled);
    const bool pressed = tab->state & State_Sunken;
    const int ul = t.spacing("xxs");
    QRect r = tab->rect.adjusted(0, 0, 0, -ul);
    if (pressed || hot) {
      const QColor fill = pressed ? t.fill(Omarchy::State::Pressed) : t.fill(Omarchy::State::Hover);
      const qreal rad = qMax<qreal>(0, qMin<qreal>(t.smallRadius(), qMin(r.width() / 2.0, qreal(r.height()))));
      p->save();
      p->setPen(Qt::NoPen);
      p->setRenderHint(QPainter::Antialiasing, rad > 0);
      if (rad <= 0) {
        p->fillRect(r, fill);
      } else {
        const QRectF rf(r);
        const qreal d = rad * 2;
        QPainterPath path;
        path.moveTo(rf.left(), rf.bottom());
        path.lineTo(rf.left(), rf.top() + rad);
        path.arcTo(QRectF(rf.left(), rf.top(), d, d), 180, -90);
        path.lineTo(rf.right() - rad, rf.top());
        path.arcTo(QRectF(rf.right() - d, rf.top(), d, d), 90, -90);
        path.lineTo(rf.right(), rf.bottom());
        path.closeSubpath();
        p->fillPath(path, fill);
      }
      p->restore();
    }
    p->fillRect(QRect(tab->rect.x(), tab->rect.bottom(), tab->rect.width(), 1), t.track());
    if (selected) p->fillRect(QRect(tab->rect.x(), tab->rect.bottom() + 1 - ul, tab->rect.width(), ul), t.accent());
    return;
  }
  case CE_TabBarTabLabel: {
    const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(opt);
    if (!tab) break;
    const bool selected = tab->state & State_Selected;
    const bool hot = (tab->state & State_MouseOver) && (tab->state & State_Enabled);
    const QFont f = t.font("body", selected);
    const QColor c = !(tab->state & State_Enabled) ? t.textFaint() : selected ? t.selectedText() : hot ? t.foreground() : t.textDim();
    QRect r = tab->rect.adjusted(0, 0, 0, -t.spacing("xxs"));
    if (!tab->icon.isNull()) {
      const int s = t.fontSize("icon");
      const QPixmap pm = tab->icon.pixmap(QSize(s, s), p->device()->devicePixelRatioF());
      p->drawPixmap(QPointF(r.x() + t.spacing("controlPaddingX"), r.y() + Paint::centered(r.height(), s)), pm);
      r.setLeft(r.left() + t.spacing("controlPaddingX") + s);
    }
    const QString text = plain(tab->text);
    Paint::textIn(*p, QRectF(r), Qt::AlignCenter, f, c, text);
    return;
  }
  case CE_ScrollBarSlider:
  case CE_ScrollBarAddPage:
  case CE_ScrollBarSubPage:
  case CE_ScrollBarAddLine:
  case CE_ScrollBarSubLine:
  case CE_ScrollBarFirst:
  case CE_ScrollBarLast: return;
  case CE_ProgressBar: {
    const auto *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(opt);
    if (!pb) break;
    QStyleOptionProgressBar o(*pb);
    o.rect = subElementRect(SE_ProgressBarGroove, pb, w);
    drawControl(CE_ProgressBarGroove, &o, p, w);
    drawControl(CE_ProgressBarContents, &o, p, w);
    if (pb->textVisible) {
      o.rect = subElementRect(SE_ProgressBarLabel, pb, w);
      drawControl(CE_ProgressBarLabel, &o, p, w);
    }
    return;
  }
  case CE_ProgressBarGroove: {
    Paint::surface(*p, QRectF(opt->rect), t.smallRadius(), t.track());
    return;
  }
  case CE_ProgressBarContents: {
    const auto *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(opt);
    if (!pb) break;
    const QRect r = opt->rect;
    const qint64 range = qint64(pb->maximum) - pb->minimum;
    if (range <= 0) {
      // Busy: a third of the track.
      const int seg = r.width() / 3;
      Paint::surface(*p, QRectF(r.x() + seg, r.y(), seg, r.height()), t.smallRadius(), t.foreground());
      return;
    }
    const qreal frac = qBound(0.0, qreal(pb->progress - pb->minimum) / range, 1.0);
    const qreal fw = r.width() * frac;
    if (fw > 0) Paint::surface(*p, QRectF(pb->invertedAppearance ? r.right() + 1 - fw : r.x(), r.y(), fw, r.height()), t.smallRadius(), t.foreground());
    return;
  }
  case CE_ProgressBarLabel: {
    const auto *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(opt);
    if (!pb || !pb->textVisible) return;
    Paint::textIn(*p, QRectF(opt->rect), Qt::AlignRight | Qt::AlignVCenter, t.font("caption"), t.textDim(), pb->text);
    return;
  }
  case CE_HeaderSection: {
    p->fillRect(opt->rect, t.background());
    p->fillRect(QRect(opt->rect.x(), opt->rect.bottom(), opt->rect.width(), 1), t.track());
    if ((opt->state & State_MouseOver) && (opt->state & State_Enabled)) p->fillRect(opt->rect.adjusted(0, 0, 0, -1), t.fill(Omarchy::State::Hover));
    return;
  }
  case CE_HeaderLabel: {
    const auto *h = qstyleoption_cast<const QStyleOptionHeader *>(opt);
    if (!h) break;
    Paint::textIn(*p, QRectF(h->rect.adjusted(t.spacing("controlPaddingX"), 0, -t.spacing("controlPaddingX"), 0)), int(h->textAlignment | Qt::AlignVCenter),
                  t.font("caption", true), t.textDim(), h->text);
    return;
  }
  case CE_HeaderEmptyArea: {
    p->fillRect(opt->rect, t.background());
    p->fillRect(QRect(opt->rect.x(), opt->rect.bottom(), opt->rect.width(), 1), t.track());
    return;
  }
  case CE_ItemViewItem: {
    const auto *v = qstyleoption_cast<const QStyleOptionViewItem *>(opt);
    if (!v) break;
    QStyleOptionViewItem o(*v);
    // Selected rows keep the foreground glass; their text takes the menu's selected color.
    const QColor sel = t.color(QStringLiteral("surfaces.menu.selectedText"));
    o.palette.setColor(QPalette::HighlightedText, sel);
    o.palette.setColor(QPalette::Text, t.foreground());
    o.state &= ~State_HasFocus;
    QProxyStyle::drawControl(ce, &o, p, w);
    return;
  }
  case CE_Splitter: p->fillRect(opt->rect, t.track()); return;
  case CE_ShapedFrame: {
    const auto *f = qstyleoption_cast<const QStyleOptionFrame *>(opt);
    if (f && (f->frameShape == QFrame::HLine || f->frameShape == QFrame::VLine)) {
      const QRect r = opt->rect;
      if (f->frameShape == QFrame::HLine) p->fillRect(QRect(r.x(), r.center().y(), r.width(), 1), t.track());
      else p->fillRect(QRect(r.center().x(), r.y(), 1, r.height()), t.track());
      return;
    }
    if (f && f->frameShape == QFrame::NoFrame) return;
    break;
  }
  case CE_ToolBar: p->fillRect(opt->rect, t.background()); return;
  case CE_SizeGrip: return;
  case CE_FocusFrame: return;
  default: break;
  }
  QProxyStyle::drawControl(ce, opt, p, w);
}

void Style::drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt, QPainter *p, const QWidget *w) const {
  const Tokens &t = tk();
  switch (cc) {
  case CC_ComboBox: {
    const auto *cb = qstyleoption_cast<const QStyleOptionComboBox *>(opt);
    if (!cb) break;
    // Dropdown: the input's glass, a textDim chevron controlGap from the right.
    QStyleOption o(*cb);
    if (cb->state & State_On) o.state |= State_HasFocus | State_KeyboardFocusChange;
    input(p, cb->rect, &o, true);
    const QRect arrow = subControlRect(cc, cb, SC_ComboBoxArrow, w);
    const QFont f = t.font("body");
    const QString ch = chevronDown();
    const QSizeF s = Paint::textSize(f, ch);
    const QColor c = (cb->state & State_Enabled) ? t.textDim() : t.textFaint();
    Paint::text(*p, QPointF(arrow.x(), cb->rect.y() + Paint::centered(cb->rect.height(), s.height())), f, c, ch);
    return;
  }
  case CC_SpinBox: {
    const auto *sb = qstyleoption_cast<const QStyleOptionSpinBox *>(opt);
    if (!sb) break;
    // NumberField: the input's glass, [−] and [+] with hover and pressed fills.
    input(p, sb->rect, sb, true);
    for (const SubControl part : {SC_SpinBoxDown, SC_SpinBoxUp}) {
      const QRect r = subControlRect(cc, sb, part, w);
      const bool up = part == SC_SpinBoxUp;
      const bool enabled = (sb->state & State_Enabled) && (sb->stepEnabled & (up ? QAbstractSpinBox::StepUpEnabled : QAbstractSpinBox::StepDownEnabled));
      const bool pressed = enabled && (sb->activeSubControls & part) && (sb->state & State_Sunken);
      const bool hot = enabled && (sb->activeSubControls & part) && (sb->state & State_MouseOver);
      if (pressed || hot) Paint::surface(*p, QRectF(r), t.smallRadius(), pressed ? t.fill(Omarchy::State::Pressed) : t.fill(Omarchy::State::Hover));
      p->save();
      if (!enabled) p->setOpacity(0.4);
      glyph(p, r, up ? QStringLiteral("+") : QStringLiteral("−"), t.foreground(), w ? w->font().pixelSize() > 0 ? w->font().pixelSize() : t.fontSize("body") : t.fontSize("body"));
      p->restore();
    }
    return;
  }
  case CC_ScrollBar: {
    const auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(opt);
    if (!sb) break;
    // ScrollArea's bar: the track, a handle in the normal border color, textDim held or hovered.
    Paint::surface(*p, QRectF(sb->rect), t.smallRadius(), t.track());
    const QRect handle = subControlRect(cc, sb, SC_ScrollBarSlider, w);
    const bool hot = (sb->state & State_MouseOver) || (sb->state & State_Sunken);
    if (sb->maximum > sb->minimum) Paint::surface(*p, QRectF(handle), t.smallRadius(), hot ? t.textDim() : t.border(Omarchy::State::Normal).color);
    return;
  }
  case CC_Slider: {
    const auto *sl = qstyleoption_cast<const QStyleOptionSlider *>(opt);
    if (!sl) break;
    // Slider: a thin selected-fill track, a foreground fill to the value, a round foreground
    // knob ringed in the surface color.
    const QRect groove = subControlRect(cc, sl, SC_SliderGroove, w);
    const QRect handle = subControlRect(cc, sl, SC_SliderHandle, w);
    const bool horiz = sl->orientation == Qt::Horizontal;
    const qreal th = horiz ? groove.height() : groove.width();
    p->save();
    if (!(sl->state & State_Enabled)) p->setOpacity(0.5);
    Paint::surface(*p, QRectF(groove), th / 2, t.fill(Omarchy::State::Selected));
    QRectF fill = QRectF(groove);
    if (horiz) fill.setRight(handle.center().x() + 0.5);
    else fill.setTop(handle.center().y());
    Paint::surface(*p, fill, th / 2, t.foreground());
    if (sl->tickPosition != QSlider::NoTicks && sl->maximum > sl->minimum) {
      const int interval = sl->tickInterval > 0 ? sl->tickInterval : qMax(1, sl->pageStep);
      const qreal nw = qMax(1, t.space(2));
      for (int v = sl->minimum + interval; v < sl->maximum; v += interval) {
        const int pos = sliderPositionFromValue(sl->minimum, sl->maximum, v, horiz ? groove.width() : groove.height(), !horiz);
        const QRectF n = horiz ? QRectF(groove.x() + pos - nw / 2, groove.center().y() + 0.5 - (th + t.space(4)) / 2, nw, th + t.space(4))
                               : QRectF(groove.center().x() + 0.5 - (th + t.space(4)) / 2, groove.y() + pos - nw / 2, th + t.space(4), nw);
        Paint::surface(*p, n, 1, t.color(QStringLiteral("surfaces.popups.background")));
      }
    }
    const bool hot = (sl->state & State_MouseOver) || (sl->state & State_Sunken) || ((sl->state & State_HasFocus) && (sl->state & State_KeyboardFocusChange));
    QRectF k = QRectF(handle);
    if (hot) k = QRectF(k.center() - QPointF(k.width() * 0.575, k.height() * 0.575), k.size() * 1.15);
    Paint::surface(*p, k, k.width() / 2, t.foreground(), Border::flat(t.color(QStringLiteral("surfaces.popups.background")), qMax(1, t.space(2))));
    p->restore();
    return;
  }
  case CC_ToolButton: {
    const auto *tb = qstyleoption_cast<const QStyleOptionToolButton *>(opt);
    if (!tb) break;
    QStyleOptionToolButton o(*tb);
    if ((tb->state & State_Sunken) || (tb->state & State_On) || (tb->state & State_MouseOver) || !(tb->state & State_AutoRaise) ||
        ((tb->state & State_HasFocus) && (tb->state & State_KeyboardFocusChange)))
      drawPrimitive(PE_PanelButtonTool, &o, p, w);
    o.state &= ~(State_Sunken | State_Raised);
    o.subControls &= ~SC_ToolButton;
    o.activeSubControls = SC_None;
    // The label in the kit's colors: the base draws icon and text from the palette.
    QProxyStyle::drawComplexControl(cc, &o, p, w);
    return;
  }
  case CC_GroupBox: {
    const auto *gb = qstyleoption_cast<const QStyleOptionGroupBox *>(opt);
    if (!gb) break;
    // A group: its title as a SectionHeader, the contents in a bordered glass frame.
    const QRect frame = subControlRect(cc, gb, SC_GroupBoxFrame, w);
    if (!(gb->features & QStyleOptionFrame::Flat)) {
      QStyleOption fo(*gb);
      fo.rect = frame;
      drawPrimitive(PE_FrameGroupBox, &fo, p, w);
    }
    if (gb->subControls & SC_GroupBoxCheckBox) {
      QStyleOptionButton box;
      box.QStyleOption::operator=(*gb);
      box.rect = subControlRect(cc, gb, SC_GroupBoxCheckBox, w);
      drawPrimitive(PE_IndicatorCheckBox, &box, p, w);
    }
    if (!gb->text.isEmpty()) {
      const QRect label = subControlRect(cc, gb, SC_GroupBoxLabel, w);
      Paint::text(*p, label.topLeft(), t.font("caption", true), (gb->state & State_Enabled) ? t.textDim() : t.textFaint(), plain(gb->text));
    }
    return;
  }
  default: break;
  }
  QProxyStyle::drawComplexControl(cc, opt, p, w);
}

QPixmap Style::standardPixmap(StandardPixmap sp, const QStyleOption *opt, const QWidget *w) const { return QProxyStyle::standardPixmap(sp, opt, w); }

}  // namespace Omarchy
