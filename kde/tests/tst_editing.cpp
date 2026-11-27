// Editing (spec/components/editing.md): what keys, clicks and drags do to the timeline, the
// scrubber, trim and crop handles, drag grips and the color picker.

#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include <Omarchy/Widgets.h>

#include "harness.h"

using namespace Omarchy;

static void setupTimeline(Timeline &t) {
  t.setDuration(30);
  t.setPosition(12);
  t.setTracks({{{0, 7.5, QStringLiteral("Intro")}, {8, 18, QStringLiteral("Demo")}, {18.5, 30, QStringLiteral("Outro")}},
               {{2, 12, QStringLiteral("Voiceover")}, {14, 26, QStringLiteral("Music")}}});
  t.setSelected(0, 1);
  QObject::connect(&t, &Timeline::seek, &t, &Timeline::setPosition);
  QObject::connect(&t, &Timeline::clipPressed, &t, &Timeline::setSelected);
  Harness::show(&t, QSize(720, 160));
}

static void drag(QWidget *w, QPoint from, QPoint to) {
  QTest::mousePress(w, Qt::LeftButton, {}, from);
  QTest::mouseMove(w, (from + to) / 2);
  QTest::mouseMove(w, to);
  QTest::mouseRelease(w, Qt::LeftButton, {}, to);
}

class Editing : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void timeline_geometry_matches_the_kit() {
    Timeline t;
    setupTimeline(t);
    QCOMPARE(t.pixelsPerSecond(), 24.0);
    QCOMPARE(t.tickStep(), 5.0);
    QCOMPARE(t.laneHeight(), 66.0);
    QCOMPARE(t.clipRect(0, 1), QRectF(192, 26, 240, 62));
    QCOMPARE(t.playheadHeadRect(), QRectF(283, 14, 10, 10));
    QCOMPARE(t.format(75), QStringLiteral("1:15"));
    QVERIFY(t.trimHandle(QStringLiteral("left"))->isVisible());
  }

  void timeline_keys_move_the_playhead() {
    Timeline t;
    setupTimeline(t);
    QSignalSpy seek(&t, &Timeline::seek);
    t.setFocus();
    QTest::keyClick(&t, Qt::Key_Right);
    QCOMPARE(t.position(), 13.0);
    QTest::keyClick(&t, Qt::Key_Right, Qt::ShiftModifier);  // a major step
    QCOMPARE(t.position(), 18.0);
    QTest::keyClick(&t, Qt::Key_Left);
    QCOMPARE(t.position(), 17.0);
    QTest::keyClick(&t, Qt::Key_End);
    QCOMPARE(t.position(), 30.0);
    QTest::keyClick(&t, Qt::Key_Right);
    QCOMPARE(t.position(), 30.0);  // clamped
    QTest::keyClick(&t, Qt::Key_Home);
    QCOMPARE(t.position(), 0.0);
    QCOMPARE(seek.count(), 6);
    QVERIFY(t.isPlayheadHot());  // focus heats it
  }

  void timeline_press_seeks_selects_and_drags() {
    Timeline t;
    setupTimeline(t);
    QSignalSpy pressed(&t, &Timeline::clipPressed);
    QTest::mouseClick(&t, Qt::LeftButton, {}, QPoint(240, 5));  // the ruler
    QCOMPARE(t.position(), 10.0);
    drag(&t, QPoint(10, 140), QPoint(480, 140));  // an empty lane, dragged
    QCOMPARE(t.position(), 20.0);
    QTest::mouseClick(&t, Qt::LeftButton, {}, QPoint(600, 60));  // Outro
    QCOMPARE(pressed.count(), 1);
    QCOMPARE(pressed.at(0).at(0).toInt(), 0);
    QCOMPARE(pressed.at(0).at(1).toInt(), 2);
    QCOMPARE(t.selectedClip(), 2);
    QCOMPARE(t.position(), 20.0);  // a clip press doesn't seek
    // The playhead's head: dragging it seeks.
    const QPoint head = t.playheadHeadRect().center().toPoint();
    drag(&t, head, head + QPoint(48, 0));
    QCOMPARE(t.position(), 22.0);
  }

  void timeline_trim_handles_drag_and_nudge() {
    Timeline t;
    setupTimeline(t);
    QSignalSpy trimmed(&t, &Timeline::trimmed);
    TrimHandle *left = t.trimHandle(QStringLiteral("left"));
    const QPoint c = left->rect().center();
    drag(left, c, c + QPoint(24, 0));
    QVERIFY(trimmed.count() >= 1);
    QCOMPARE(trimmed.last().at(2).toString(), QStringLiteral("left"));
    QCOMPARE(trimmed.last().at(3).toReal(), 9.0);
    trimmed.clear();
    left->setFocus();
    QTest::keyClick(left, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(trimmed.count(), 1);
    QCOMPARE(trimmed.last().at(3).toReal(), 8 + 10 / 24.0);
    QTest::keyClick(left, Qt::Key_Up);  // a trim cap moves only sideways
    QCOMPARE(trimmed.count(), 1);
    TrimHandle *right = t.trimHandle(QStringLiteral("right"));
    right->setFocus();
    QTest::keyClick(right, Qt::Key_Left);
    QCOMPARE(trimmed.last().at(2).toString(), QStringLiteral("right"));
    QCOMPARE(trimmed.last().at(3).toReal(), 18 - 1 / 24.0);
  }

  void timeline_wheel_zooms_around_the_pointer() {
    Timeline t;
    setupTimeline(t);
    QSignalSpy view(&t, &Timeline::viewChanged);
    QWheelEvent e(QPointF(360, 100), t.mapToGlobal(QPointF(360, 100)), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                  Qt::NoScrollPhase, false);
    QApplication::sendEvent(&t, &e);
    QCOMPARE(view.count(), 1);
    QCOMPARE(t.zoom(), 1.25);
    QCOMPARE(t.viewStart(), 15 - 360 / (720 * 1.25 / 30));  // the time under the pointer stays put
    QWheelEvent out(QPointF(360, 100), t.mapToGlobal(QPointF(360, 100)), QPoint(), QPoint(0, -360), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
    QApplication::sendEvent(&t, &out);
    QCOMPARE(t.zoom(), 1.0);  // clamped to the whole duration
    QCOMPARE(t.viewStart(), 0.0);
  }

  void scrubber_keys_and_clicks_seek() {
    Scrubber s;
    s.setPosition(42);
    s.setDuration(130);
    Harness::show(&s, QSize(360, 22));
    QCOMPARE(s.format(42), QStringLiteral("0:42"));
    QCOMPARE(s.format(3725), QStringLiteral("1:02:05"));
    QSignalSpy seek(&s, &Scrubber::seek);
    s.setFocus();
    QTest::keyClick(&s, Qt::Key_Right);
    QCOMPARE(seek.last().at(0).toReal(), 47.0);
    QTest::keyClick(&s, Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(seek.last().at(0).toReal(), 12.0);
    QTest::keyClick(&s, Qt::Key_End);
    QCOMPARE(seek.last().at(0).toReal(), 130.0);
    QTest::keyClick(&s, Qt::Key_Home);
    QCOMPARE(seek.last().at(0).toReal(), 0.0);
    const QRectF track = s.trackRect();
    QTest::mouseClick(&s, Qt::LeftButton, {}, QPointF(track.x() + track.width() / 2, 11).toPoint());
    QVERIFY(qAbs(seek.last().at(0).toReal() - 65) < 0.5);
    seek.clear();
    QTest::mouseClick(&s, Qt::LeftButton, {}, QPoint(5, 11));  // the elapsed time isn't the bar
    QCOMPARE(seek.count(), 0);
    QCOMPARE(s.knobRect(), QRectF(124.375, 4, 14, 14));
  }

  void crop_handles_report_their_edge() {
    CropFrame f;
    Harness::show(&f, QSize(240, 140));
    QSignalSpy pressed(&f, &CropFrame::pressed);
    QSignalSpy moved(&f, &CropFrame::moved);
    QSignalSpy released(&f, &CropFrame::released);
    QSignalSpy nudged(&f, &CropFrame::nudged);
    TrimHandle *corner = f.handle(QStringLiteral("bottom-right"));
    QCOMPARE(corner->handleRect().translated(corner->pos()), QRectF(222, 122, 18, 18));
    const QPoint c = corner->rect().center();
    drag(corner, c, c + QPoint(-10, -6));
    QCOMPARE(pressed.count(), 1);
    QCOMPARE(pressed.last().at(0).toString(), QStringLiteral("bottom-right"));
    QCOMPARE(moved.last().at(1).toReal(), -10.0);
    QCOMPARE(moved.last().at(2).toReal(), -6.0);
    QCOMPARE(released.count(), 1);
    TrimHandle *top = f.handle(QStringLiteral("top"));
    top->setFocus();
    QTest::keyClick(top, Qt::Key_Left);  // a top bar moves only up and down
    QCOMPARE(nudged.count(), 0);
    QTest::keyClick(top, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(nudged.count(), 1);
    QCOMPARE(nudged.last().at(0).toString(), QStringLiteral("top"));
    QCOMPARE(nudged.last().at(2).toReal(), -10.0);
    corner->setFocus();
    QTest::keyClick(corner, Qt::Key_Right);
    QCOMPARE(nudged.last().at(1).toReal(), 1.0);
    f.setCursorEdge(QStringLiteral("left"));
    QVERIFY(f.handle(QStringLiteral("left"))->isHandleHot());
    QVERIFY(!f.handle(QStringLiteral("right"))->isHandleHot());
  }

  void drag_handle_drags_and_asks_to_move() {
    DragHandle h;
    Harness::show(&h, QSize(16, 22));
    QEvent leave(QEvent::Leave);  // the offscreen pointer starts over the window
    QApplication::sendEvent(&h, &leave);
    QSignalSpy pressed(&h, &DragHandle::pressed);
    QSignalSpy moved(&h, &DragHandle::moved);
    QSignalSpy released(&h, &DragHandle::released);
    QSignalSpy move(&h, &DragHandle::moveRequested);
    h.clearFocus();
    QVERIFY2(!h.isGripHot(), "textDim at rest");
    h.setFocus();
    QVERIFY2(h.isGripHot(), "focus heats it");
    drag(&h, QPoint(8, 11), QPoint(8, 40));
    QCOMPARE(pressed.count(), 1);
    QCOMPARE(moved.last().at(1).toReal(), 29.0);
    QCOMPARE(released.count(), 1);
    QVERIFY(h.hasFocus() || h.isGripHot());
    QTest::keyClick(&h, Qt::Key_Up);
    QTest::keyClick(&h, Qt::Key_Down);
    QTest::keyClick(&h, Qt::Key_Down);
    QCOMPARE(move.count(), 3);
    QCOMPARE(move.at(0).at(0).toInt(), -1);
    QCOMPARE(move.at(2).at(0).toInt(), 1);
  }

  void drop_indicator_centers_on_the_boundary() {
    QWidget list;
    list.resize(360, 112);
    auto *d = new DropIndicator(&list);
    d->placeAt(0, 84, 360);
    QCOMPARE(d->geometry(), QRect(0, 83, 360, 2));
    QVERIFY(d->testAttribute(Qt::WA_TransparentForMouseEvents));
  }

  void color_picker_square_and_strips_take_keys() {
    ColorPicker c;
    QVERIFY(c.setColor(QStringLiteral("#7aa2f7")));
    Harness::show(&c, QSize(300, 260));
    QCOMPARE(c.hex(), QStringLiteral("#7aa2f7"));
    QCOMPARE(c.hexField()->text(), QStringLiteral("#7aa2f7"));
    QVERIFY(qAbs(c.hue() * 360 - 220.8) < 0.01);
    QCOMPARE(c.square()->geometry(), QRect(0, 0, 260, 192));
    QCOMPARE(c.squareKnobRect(), QRectF(125, 0, 14, 14));
    QSignalSpy edited(&c, &ColorPicker::edited);
    const qreal s = c.saturation(), v = c.value();
    c.square()->setFocus();
    QTest::keyClick(c.square(), Qt::Key_Right);
    QCOMPARE(c.saturation(), s + 0.01);
    QTest::keyClick(c.square(), Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(c.value(), v - 0.1);
    QCOMPARE(edited.count(), 2);
    QCOMPARE(c.hexField()->text(), c.hex());  // the field follows
    const qreal h = c.hue();
    c.hueStrip()->setFocus();
    QTest::keyClick(c.hueStrip(), Qt::Key_Down);
    QCOMPARE(c.hue(), h + 0.01);
    QTest::keyClick(c.alphaStrip(), Qt::Key_Down);
    QCOMPARE(c.alpha(), 0.99);
    QCOMPARE(c.hex().size(), 9);  // #rrggbbaa below full alpha
    QVERIFY(c.hex().endsWith(QStringLiteral("fc")));
    QCOMPARE(edited.count(), 4);
  }

  void color_picker_mouse_picks() {
    ColorPicker c;
    c.setColor(QStringLiteral("#7aa2f7"));
    Harness::show(&c, QSize(300, 260));
    QTest::mouseClick(c.square(), Qt::LeftButton, {}, QPoint(260, 0));
    QCOMPARE(c.saturation(), 1.0);
    QCOMPARE(c.value(), 1.0);
    QVERIFY(c.square()->hasFocus());
    drag(c.square(), QPoint(10, 10), QPoint(0, 192));
    QCOMPARE(c.saturation(), 0.0);
    QCOMPARE(c.value(), 0.0);
    // The hue strip: the knob's middle travels from knob/2 to height − knob/2.
    QTest::mouseClick(c.hueStrip(), Qt::LeftButton, {}, QPoint(7, 7));
    QCOMPARE(c.hue(), 0.0);
    QTest::mouseClick(c.hueStrip(), Qt::LeftButton, {}, QPoint(7, 185));
    QCOMPARE(c.hue(), 0.9999);
    QTest::mouseClick(c.alphaStrip(), Qt::LeftButton, {}, QPoint(7, 96));
    QCOMPARE(c.alpha(), 0.5);
  }

  void color_picker_swatches_and_hex_field() {
    ColorPicker c;
    c.setColor(QStringLiteral("#7aa2f780"));
    Harness::show(&c, QSize(300, 260));
    QCOMPARE(c.swatches().size(), 8);
    QCOMPARE(c.swatches().at(0), QStringLiteral("#f7768e"));  // tokyo-night's red
    QSignalSpy edited(&c, &ColorPicker::edited);
    QTest::mouseClick(c.swatch(0), Qt::LeftButton);
    QCOMPARE(c.hex(), QStringLiteral("#f7768e80"));  // the alpha stays
    c.swatch(3)->setFocus();
    QTest::keyClick(c.swatch(3), Qt::Key_Return);
    QCOMPARE(c.hex().left(7), c.swatches().at(3));
    QTest::keyClick(c.swatch(2), Qt::Key_Space);
    QCOMPARE(c.hex().left(7), c.swatches().at(2));
    QCOMPARE(edited.count(), 3);
    TextField *field = c.hexField();
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, QStringLiteral("#ABCDEF"));
    QCOMPARE(c.hex(), QStringLiteral("#abcdef"));  // applies as soon as it is valid
    QCOMPARE(field->text(), QStringLiteral("#ABCDEF"));  // not rewritten under the typist
    QTest::keyClick(field, Qt::Key_Backspace);
    QCOMPARE(c.hex(), QStringLiteral("#abcdef"));  // an invalid hex changes nothing
    QTest::keyClick(field, Qt::Key_F);
    QTest::keyClick(field, Qt::Key_Return);
    QCOMPARE(field->text(), QStringLiteral("#abcdef"));
    QVERIFY(!c.setColor(QStringLiteral("nope")));
  }
};

OMARCHY_TEST(Editing)

#include "tst_editing.moc"
