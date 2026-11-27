// Feedback (spec/components/feedback.md): what clicks and keys do to banners, toasts and empty
// states, the meter's tones and sizes, and the spinner standing still under reduced motion.

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

#include <Omarchy/Widgets.h>

#include "harness.h"

using namespace Omarchy;

class Feedback : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void meter_clamps_and_colors_by_tone() {
    Meter m(1.4);
    QCOMPARE(m.value(), 1.4);
    const Tokens &tk = shown();
    QCOMPARE(m.fillColor(), tk.foreground());
    m.setTone(QStringLiteral("success"));
    QCOMPARE(m.fillColor(), tk.success());
    m.setTone(QStringLiteral("error"));
    QCOMPARE(m.fillColor(), tk.urgent());
    m.setTone(QStringLiteral("warning"));
    m.setAlarming(true);
    QCOMPARE(m.fillColor(), tk.urgent());  // alarming wins over the tone
    ProgressBar bar(0.4);
    QCOMPARE(bar.fillColor(), tk.accent());
  }

  void meter_sizes_follow_markers_and_header() {
    Meter plain(0.5);
    QCOMPARE(plain.sizeHint(), QSize(200, 6));
    plain.setMarkers({0.5, 0.8});
    QCOMPARE(plain.sizeHint().height(), 15);  // the ticks are 2.5 × the thickness
    Meter battery(0.65);
    battery.setLabel(QStringLiteral("Battery"));
    battery.setValueText(QStringLiteral("65%"));
    QCOMPARE(battery.sizeHint().height(), 16 + 6 + 6);
    QCOMPARE(battery.accessibleName(), QStringLiteral("Battery"));
  }

  void spinner_holds_still_under_reduced_motion() {
    Spinner s;
    Harness::show(&s);
    QCOMPARE(s.sizeHint(), QSize(19, 19));
    QVERIFY(!s.isAnimating());
    QCOMPARE(s.rotation(), 0.0);
    QCOMPARE(s.glyphOpacity(), 1.0);
    s.setPulse(true);
    QCOMPARE(s.glyphOpacity(), 0.5);  // a pulse rests at half opacity

    // With motion on, it turns (and stops when hidden or not running).
    const QByteArray still = QJsonDocument(Theme::instance()->json()).toJson(QJsonDocument::Compact);
    QJsonObject moving = Theme::instance()->json();
    moving.insert(QStringLiteral("reduceMotion"), false);
    Theme::instance()->deliver(QJsonDocument(moving).toJson(QJsonDocument::Compact));
    s.setPulse(false);
    QVERIFY(s.isAnimating());
    QTRY_VERIFY(s.rotation() > 0);
    s.setRunning(false);
    QVERIFY(!s.isAnimating());
    QCOMPARE(s.rotation(), 0.0);
    s.setRunning(true);
    s.setPulse(true);
    QTRY_VERIFY(s.glyphOpacity() > 0.5);
    s.hide();
    QVERIFY(!s.isAnimating());
    Theme::instance()->deliver(still);
    QVERIFY(shown().reduceMotion());
  }

  void indeterminate_bar_holds_mid_track() {
    ProgressBar bar;
    bar.setIndeterminate(true);
    Harness::show(&bar, QSize(340, 6));
    QCOMPARE(bar.trackRect(), QRectF(0, 0, 340, 6));
    QCOMPARE(bar.grab().toImage().pixelColor(170, 3), shown().accent());  // the segment sits in the middle
    QVERIFY(bar.grab().toImage().pixelColor(10, 3) != shown().accent());
  }

  void banner_action_and_dismiss_click_and_take_keys() {
    Banner b(QStringLiteral("Omarchy 3.2 is available"));
    b.setActionText(QStringLiteral("Update"));
    b.setDismissible(true);
    Harness::show(&b, QSize(724, b.heightForWidth(724)));
    QCOMPARE(b.height(), 40);
    QSignalSpy action(&b, &Banner::actionClicked);
    QSignalSpy dismissed(&b, &Banner::dismissed);
    QVERIFY(b.actionButton()->isVisible());
    QVERIFY(b.dismissButton()->isVisible());
    QVERIFY2(b.dismissButton()->geometry().right() < b.width() - 4, "dismiss ends the row, inside the padding");
    QTest::mouseClick(b.actionButton(), Qt::LeftButton);
    QCOMPARE(action.count(), 1);
    QTest::mouseClick(b.dismissButton(), Qt::LeftButton);
    QCOMPARE(dismissed.count(), 1);
    // Both are Tab stops; Return and Space press them.
    QCOMPARE(b.actionButton()->focusPolicy(), Qt::StrongFocus);
    QCOMPARE(b.dismissButton()->focusPolicy(), Qt::StrongFocus);
    b.actionButton()->setFocus();
    QTest::keyClick(b.actionButton(), Qt::Key_Return);
    QCOMPARE(action.count(), 2);
    b.dismissButton()->setFocus();
    QTest::keyClick(b.dismissButton(), Qt::Key_Space);
    QCOMPARE(dismissed.count(), 2);
    QCOMPARE(b.dismissButton()->accessibleName(), QStringLiteral("Dismiss"));
    b.setHasCursor(true);
    QVERIFY(b.actionButton()->hasCursor());
  }

  void banner_tones_and_glyphs() {
    Banner b(QStringLiteral("Backup finished"));
    QCOMPARE(b.toneColor(), shown().accent());
    QCOMPARE(b.toneGlyph(), QString::fromUcs4(U"\U000f02fd"));
    b.setTone(QStringLiteral("neutral"));
    QCOMPARE(b.toneColor(), shown().accent());  // neutral draws as info
    b.setTone(QStringLiteral("warning"));
    QCOMPARE(b.toneColor(), shown().warning());
    QCOMPARE(b.toneGlyph(), QString::fromUcs4(U"\U000f002a"));
    b.setGlyph(QStringLiteral("x"));
    QCOMPARE(b.toneGlyph(), QStringLiteral("x"));
    QVERIFY(!b.actionButton()->isVisibleTo(&b));
    QVERIFY(!b.dismissButton()->isVisibleTo(&b));
    QCOMPARE(b.heightForWidth(724), 40);
  }

  void toast_action_clicks_and_takes_keys() {
    Toast toast(QStringLiteral("Export finished"));
    toast.setGlyph(QString::fromUcs4(U"\U000f012c"));
    toast.setText(QStringLiteral("omarchy-demo.mp4 · 42 MB"));
    toast.setActionText(QStringLiteral("Open"));
    toast.setCountdown(0.6);
    QCOMPARE(toast.sizeHint(), QSize(360, 59));
    Harness::show(&toast, toast.sizeHint());
    QSignalSpy action(&toast, &Toast::actionClicked);
    QTest::mouseClick(toast.actionButton(), Qt::LeftButton);
    QCOMPARE(action.count(), 1);
    toast.actionButton()->setFocus();
    QTest::keyClick(toast.actionButton(), Qt::Key_Enter);
    QCOMPARE(action.count(), 2);
    // The countdown line: 0.6 of the inner width, along the inner bottom edge.
    const QImage img = toast.grab().toImage();
    const QColor line = shown().color(QStringLiteral("surfaces.notifications.countdown"));
    QCOMPARE(img.pixelColor(100, 55), line);
    QVERIFY(img.pixelColor(300, 55) != line);
  }

  void empty_state_action_clicks() {
    EmptyState e(QStringLiteral("No networks found"));
    e.setGlyph(QString::fromUcs4(U"\U000f05aa"));
    e.setHint(QStringLiteral("Turn on Wi-Fi to scan"));
    QVERIFY(!e.actionButton()->isVisibleTo(&e));
    e.setActionText(QStringLiteral("Scan"));
    Harness::show(&e, QSize(350, e.heightForWidth(350)));
    QCOMPARE(e.height(), 97);
    QSignalSpy action(&e, &EmptyState::actionClicked);
    QTest::mouseClick(e.actionButton(), Qt::LeftButton);
    QCOMPARE(action.count(), 1);
    e.actionButton()->setFocus();
    QTest::keyClick(e.actionButton(), Qt::Key_Space);
    QCOMPARE(action.count(), 2);
    QVERIFY(qAbs(e.actionButton()->geometry().center().x() - 175) <= 1);  // centered in the width
    e.setHasCursor(true);
    QVERIFY(e.actionButton()->hasCursor());
    e.setOverlay(true);
    QCOMPARE(e.size(), QStringLiteral("overlay"));
  }
};

OMARCHY_TEST(Feedback)

#include "tst_feedback.moc"
