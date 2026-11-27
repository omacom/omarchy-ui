// Mirrors qt/tests/tst_surfaces.qml, plus the buttons, inputs and slider: what keys and clicks do
// to the kit's tier-one widgets.

#include <QSignalSpy>
#include <QTest>

#include <Omarchy/Widgets.h>

#include "harness.h"

using namespace Omarchy;

class Surfaces : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void pane_action_ends_the_header_and_runs() {
    Pane pane(QStringLiteral("Night light"), QStringLiteral("Sunset to sunrise"));
    pane.setBordered(true);
    pane.setFixedWidth(240);
    auto *sw = new ToggleSwitch;
    sw->setChecked(true);
    pane.addAction(sw);
    auto *row = new SettingRow(QStringLiteral("Strength"));
    setFill(row);
    auto *slider = new Slider;
    slider->setValue(0.6);
    slider->setFixedWidth(100);
    row->addControl(slider);
    pane.addWidget(row);
    Harness::show(&pane, QSize(240, pane.sizeHint().height()));
    const int right = sw->mapTo(&pane, QPoint(sw->width(), 0)).x();
    const int inner = pane.width() - shown().spacing("panelPadding");
    QVERIFY2(right <= inner && right > inner - 40, "the action ends the header, not the title");
    QVERIFY(sw->isChecked());
    QTest::mouseClick(sw, Qt::LeftButton);
    QVERIFY(!sw->isChecked());
  }

  void toggle_row_flips_on_click_and_space() {
    Toggle toggle(QStringLiteral("Wi-Fi"), QStringLiteral("Connected"));
    Harness::show(&toggle, QSize(300, toggle.heightForWidth(300)));
    QSignalSpy clicked(&toggle, &Toggle::clicked);
    QTest::mouseClick(&toggle, Qt::LeftButton, {}, QPoint(20, 10));
    QVERIFY2(toggle.isChecked(), "the whole row is the control");
    toggle.setFocus();
    QTest::keyClick(&toggle, Qt::Key_Space);
    QVERIFY(!toggle.isChecked());
    QCOMPARE(clicked.count(), 2);
  }

  void button_clicks_on_mouse_and_keys_when_focusable() {
    Button b(QStringLiteral("Save"));
    Harness::show(&b);
    QSignalSpy clicked(&b, &Button::clicked);
    QSignalSpy right(&b, &Button::rightClicked);
    QTest::mouseClick(&b, Qt::LeftButton);
    QTest::mouseClick(&b, Qt::RightButton);
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(right.count(), 1);
    QTest::keyClick(&b, Qt::Key_Return);
    QCOMPARE(clicked.count(), 1);  // not a Tab stop by default
    b.setFocusable(true);
    b.setFocus();
    for (const auto k : {Qt::Key_Return, Qt::Key_Enter, Qt::Key_Space}) QTest::keyClick(&b, k);
    QCOMPARE(clicked.count(), 4);
  }

  void button_group_walks_with_h_l_and_chooses() {
    ButtonGroup g;
    g.setOptions(QStringList{QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
    g.setValue(QStringLiteral("Center"));
    Harness::show(&g);
    QSignalSpy changed(&g, &ButtonGroup::changed);
    g.setFocus();
    QCOMPARE(g.focusedIndex(), 1);  // lands on the current choice
    QVERIFY(g.button(1)->hasCursor());
    QTest::keyClick(&g, Qt::Key_L);
    QCOMPARE(g.focusedIndex(), 2);
    QTest::keyClick(&g, Qt::Key_Right);
    QCOMPARE(g.focusedIndex(), 2);
    QTest::keyClick(&g, Qt::Key_Return);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(g.value(), QStringLiteral("Right"));
    QTest::keyClick(&g, Qt::Key_H);
    QTest::keyClick(&g, Qt::Key_Left);
    QTest::keyClick(&g, Qt::Key_Left);
    QCOMPARE(g.focusedIndex(), 0);
    QTest::mouseClick(g.button(1), Qt::LeftButton);
    QCOMPARE(g.value(), QStringLiteral("Center"));
  }

  void number_field_steps_with_buttons_keys_and_clamps() {
    NumberField n(QStringLiteral("Gaps"));
    n.setRange(0, 40);
    n.setValue(10);
    Harness::show(&n);
    QSignalSpy modified(&n, &NumberField::modified);
    QTest::mouseClick(&n, Qt::LeftButton, {}, n.upRect().center().toPoint());
    QCOMPARE(n.value(), 11);
    QTest::mouseClick(&n, Qt::LeftButton, {}, n.downRect().center().toPoint());
    QTest::mouseClick(&n, Qt::LeftButton, {}, n.downRect().center().toPoint());
    QCOMPARE(n.value(), 9);
    n.editor()->setFocus();
    QTest::keyClick(n.editor(), Qt::Key_Up);
    QCOMPARE(n.value(), 10);
    QTest::keyClick(n.editor(), Qt::Key_Down);
    QCOMPARE(n.value(), 9);
    n.setValue(40);
    QTest::keyClick(n.editor(), Qt::Key_Up);
    QCOMPARE(n.value(), 40);
    n.editor()->selectAll();
    QTest::keyClicks(n.editor(), QStringLiteral("25"));
    QCOMPARE(n.value(), 25);
    QCOMPARE(modified.count(), 7);  // typing sets it per keystroke: 2, then 25
  }

  void dropdown_opens_walks_and_chooses() {
    Dropdown d(QStringLiteral("Theme"));
    d.setOptions({{QStringLiteral("tokyo-night"), QStringLiteral("Tokyo Night")}, {QStringLiteral("nord"), QStringLiteral("Nord")},
                  {QStringLiteral("gruvbox"), QStringLiteral("Gruvbox")}});
    d.setValue(QStringLiteral("tokyo-night"));
    Harness::show(&d);
    QSignalSpy changed(&d, &Dropdown::changed);
    d.setFocus();
    QTest::keyClick(&d, Qt::Key_Return);
    QVERIFY(d.isPopupOpen());
    QCOMPARE(d.popupIndex(), 0);
    QTest::keyClick(d.popup(), Qt::Key_J);
    QTest::keyClick(d.popup(), Qt::Key_Down);
    QCOMPARE(d.popupIndex(), 2);
    QTest::keyClick(d.popup(), Qt::Key_K);
    QTest::keyClick(d.popup(), Qt::Key_Return);
    QVERIFY(!d.isPopupOpen());
    QCOMPARE(d.value(), QStringLiteral("nord"));
    QCOMPARE(changed.count(), 1);
    QTest::mouseClick(&d, Qt::LeftButton, {}, d.triggerRect().center().toPoint());
    QVERIFY(d.isPopupOpen());
    QTest::keyClick(d.popup(), Qt::Key_Escape);
    QVERIFY(!d.isPopupOpen());
    QCOMPARE(d.value(), QStringLiteral("nord"));
  }

  void slider_drags_and_steps() {
    Slider s;
    s.setFixedWidth(200);
    s.setValue(0.5);
    Harness::show(&s, QSize(200, s.sizeHint().height()));
    QSignalSpy released(&s, &Slider::released);
    const QRectF track = s.trackRect();
    QTest::mouseClick(&s, Qt::LeftButton, {}, QPoint(int(track.x() + track.width() * 0.25), s.height() / 2));
    QVERIFY(qAbs(s.value() - 0.25) < 0.02);
    s.setFocus();
    QTest::keyClick(&s, Qt::Key_Right);
    QVERIFY(qAbs(s.value() - 0.30) < 0.02);
    QTest::keyClick(&s, Qt::Key_End);
    QCOMPARE(s.value(), 1.0);
    QTest::keyClick(&s, Qt::Key_H);
    QVERIFY(qAbs(s.value() - 0.95) < 0.001);
    QCOMPARE(released.count(), 4);
  }

  void confirm_dialog_keys_and_clicks() {
    QWidget host;
    host.resize(340, 170);
    ConfirmDialog d(QStringLiteral("Remove it?"), &host);
    Harness::show(&host, QSize(340, 170));
    d.open();
    QSignalSpy confirmed(&d, &ConfirmDialog::confirmed);
    QSignalSpy canceled(&d, &ConfirmDialog::canceled);
    QCOMPARE(d.selectedIndex(), 1);
    QTest::keyClick(&d, Qt::Key_Return);
    QCOMPARE(confirmed.count(), 1);
    QTest::keyClick(&d, Qt::Key_H);
    QCOMPARE(d.selectedIndex(), 0);
    QTest::keyClick(&d, Qt::Key_Return);
    QCOMPARE(canceled.count(), 1);
    QTest::keyClick(&d, Qt::Key_Escape);
    QCOMPARE(canceled.count(), 2);
    QTest::mouseClick(&d, Qt::LeftButton, {}, d.buttonRect(1).center());
    QCOMPARE(confirmed.count(), 2);
    QTest::mouseClick(&d, Qt::LeftButton, {}, QPoint(2, 2));
    QCOMPARE(canceled.count(), 3);
  }

  void text_field_takes_typing() {
    TextField f;
    f.setPlaceholderText(QStringLiteral("Search…"));
    Harness::show(&f, QSize(220, f.sizeHint().height()));
    f.setFocus();
    QTest::keyClicks(&f, QStringLiteral("omarchy"));
    QCOMPARE(f.text(), QStringLiteral("omarchy"));
    QCOMPARE(f.sizeHint().height(), 32);
  }

  void labeled_inputs_and_setting_row_match_polish_metrics() {
    NumberField nf(QStringLiteral("Gaps"));
    Dropdown dd(QStringLiteral("Theme"));
    MultiSelect ms(QStringLiteral("Notify for"));
    QCOMPARE(nf.sizeHint().height(), dd.sizeHint().height());
    QCOMPARE(ms.sizeHint().height(), dd.sizeHint().height());

    SettingRow row(QStringLiteral("Wi-Fi"));
    row.addControl(new ToggleSwitch);
    QCOMPARE(row.heightForWidth(320), 40);

    QWidget host;
    host.resize(340, 170);
    ConfirmDialog cd(QStringLiteral("Remove it?"), &host);
    cd.setConfirmText(QStringLiteral("Permanently Delete Everything"));
    QVERIFY(cd.buttonRect(1).width() > shown().space(88));
  }
};

OMARCHY_TEST(Surfaces)

#include "tst_surfaces.moc"
