// Mirrors qt/tests/tst_data.qml: a click selects a table row or folds a tree branch, a header
// click asks to sort, and the keys move the cursor, choose its row and (in the tree) fold and
// unfold. Plus the calendar's keys and steppers, the sidebar's keys, and a split view drag.

#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>

#include <Omarchy/Widgets.h>

#include "harness.h"

using namespace Omarchy;

namespace {

using G = QString;

// The table tst_data.qml builds; its sortRequested handler is the app's, as there.
void fillTable(Table &table) {
  table.setColumns({{G("Name")}, {G("Kind"), 90}, {G("Size"), 70, true}});
  const G dash = G::fromUtf8("—");
  table.setRows({{G("assets"), G("Folder"), dash},
                 {G("Cargo.toml"), G("TOML"), G("1.2 KB")},
                 {G("README.md"), G("Markdown"), G("4.8 KB")},
                 {G("src"), G("Folder"), dash},
                 {G("target"), G("Folder"), dash}});
  table.setSortColumn(0);
  QObject::connect(&table, &Table::sortRequested, &table, [&table](int column) {
    if (column == table.sortColumn()) table.setSortAscending(!table.sortAscending());
    else {
      table.setSortColumn(column);
      table.setSortAscending(true);
    }
  });
}

// The app owns `expanded` and `selectedId`, as the gallery's tree does.
void fillTree(Tree &tree) {
  tree.setExpanded({G("root"), G("qt"), G("spec")});
  tree.setNodes({TreeNode(G("root"), G("omarchy-ui"), G(),
                          {TreeNode(G("qt"), G("qt"), G(), {TreeNode(G("qt/button"), G("Button.qml")), TreeNode(G("qt/table"), G("Table.qml"))}),
                           TreeNode(G("rune"), G("rune"), G(), {TreeNode(G("rune/lib"), G("lib.rs"))}),
                           TreeNode(G("spec"), G("spec"), G(), {TreeNode(G("spec/tokens"), G("tokens.md"))})})});
  QObject::connect(&tree, &Tree::toggled, &tree, [&tree](const QString &id) {
    QStringList next = tree.expanded();
    if (next.contains(id)) next.removeAll(id);
    else next << id;
    tree.setExpanded(next);
  });
  QObject::connect(&tree, &Tree::activated, &tree, [&tree](const QString &id) { tree.setSelectedId(id); });
}

QStringList visibleIds(const Tree &tree) {
  QStringList out;
  for (const Tree::Row &r : tree.visibleRows()) out << r.id;
  return out;
}

}  // namespace

class Data : public QObject {
  Q_OBJECT
private Q_SLOTS:
  void table_selects_on_click_and_sorts_on_header() {
    Table table;
    fillTable(table);
    Harness::show(&table, QSize(480, table.sizeHint().height()));
    QTest::mouseClick(&table, Qt::LeftButton, {}, table.rowRect(3).center());
    QCOMPARE(table.selectedRow(), 3);
    QTest::mouseClick(&table, Qt::LeftButton, {}, table.headerRect(0).center());
    QVERIFY2(!table.sortAscending(), "the sorted column's header flips the order");
    QTest::mouseClick(&table, Qt::LeftButton, {}, table.headerRect(1).center());
    QCOMPARE(table.sortColumn(), 1);
    QVERIFY(table.sortAscending());
  }

  void table_keys_move_the_cursor_and_choose() {
    Table table;
    fillTable(table);
    Harness::show(&table, QSize(480, table.sizeHint().height()));
    QSignalSpy activated(&table, &Table::rowActivated);
    table.setFocus();
    QTest::keyClick(&table, Qt::Key_Down);
    QCOMPARE(table.cursorRow(), 0);  // the first key lands on the first row
    QTest::keyClick(&table, Qt::Key_Down);
    QTest::keyClick(&table, Qt::Key_Return);
    QCOMPARE(table.selectedRow(), 1);
    QTest::keyClick(&table, Qt::Key_End);
    QTest::keyClick(&table, Qt::Key_Down);
    QCOMPARE(table.cursorRow(), 4);  // stops at the last row
    QTest::keyClick(&table, Qt::Key_Space);
    QCOMPARE(table.selectedRow(), 4);
    QTest::keyClick(&table, Qt::Key_Home);
    QTest::keyClick(&table, Qt::Key_Up);
    QCOMPARE(table.cursorRow(), 0);  // stops at the first row
    QTest::keyClick(&table, Qt::Key_Enter);
    QCOMPARE(table.selectedRow(), 0);
    QCOMPARE(activated.count(), 3);
  }

  void table_scrolls_under_the_header_and_reads_a_model() {
    QStandardItemModel model(0, 2);
    model.setHorizontalHeaderLabels({G("Name"), G("Size")});
    for (int i = 0; i < 20; ++i) model.appendRow({new QStandardItem(G("file %1").arg(i)), new QStandardItem(G("%1 KB").arg(i))});
    Table table;
    table.setModel(&model);
    QCOMPARE(table.rowCount(), 20);
    QCOMPARE(table.columns().at(1).title, G("Size"));
    const int rh = table.rowHeight();
    Harness::show(&table, QSize(300, rh * 6));  // the header and five rows
    table.setFocus();
    QTest::keyClick(&table, Qt::Key_End);
    QCOMPARE(table.cursorRow(), 19);
    QCOMPARE(table.rowRect(19).bottom() + 1, table.height());  // scrolled to show it
    QTest::keyClick(&table, Qt::Key_PageUp);
    QCOMPARE(table.cursorRow(), 14);
    model.removeRows(0, 10);
    QCOMPARE(table.rowCount(), 10);
    QCOMPARE(table.rows().at(0).at(0), G("file 10"));
  }

  void tree_folds_and_selects_on_click() {
    Tree tree;
    fillTree(tree);
    Harness::show(&tree, QSize(220, tree.sizeHint().height()));
    // The chevron sits in the slot before the label.
    QTest::mouseClick(&tree, Qt::LeftButton, {}, tree.chevronRect(G("qt")).center());
    QVERIFY2(!tree.expanded().contains(G("qt")), "the chevron folds qt");
    QVERIFY(!visibleIds(tree).contains(G("qt/button")));
    QVERIFY2(tree.selectedId().isEmpty(), "the chevron doesn't choose");
    QTest::mouseClick(&tree, Qt::LeftButton, {}, tree.rowRect(G("spec/tokens")).center());
    QCOMPARE(tree.selectedId(), G("spec/tokens"));
    // A double click on a branch row folds it.
    QTest::mouseDClick(&tree, Qt::LeftButton, {}, tree.rowRect(G("spec")).center());
    QVERIFY(!tree.expanded().contains(G("spec")));
  }

  void tree_keys_fold_unfold_and_choose() {
    Tree tree;
    fillTree(tree);
    Harness::show(&tree, QSize(220, tree.sizeHint().height()));
    tree.setFocus();
    QTest::keyClick(&tree, Qt::Key_Down);
    QCOMPARE(tree.cursorId(), G("root"));
    QTest::keyClick(&tree, Qt::Key_Down);
    QCOMPARE(tree.cursorId(), G("qt"));
    QTest::keyClick(&tree, Qt::Key_Left);
    QVERIFY2(!tree.expanded().contains(G("qt")), "Left folds an open branch");
    QTest::keyClick(&tree, Qt::Key_Left);
    QVERIFY2(!tree.expanded().contains(G("qt")), "and does nothing to a folded one");
    QTest::keyClick(&tree, Qt::Key_Right);
    QVERIFY2(tree.expanded().contains(G("qt")), "Right unfolds it");
    QTest::keyClick(&tree, Qt::Key_Down);
    QCOMPARE(tree.cursorId(), G("qt/button"));
    QTest::keyClick(&tree, Qt::Key_Right);
    QCOMPARE(tree.expanded().size(), 3);  // a leaf doesn't fold
    QTest::keyClick(&tree, Qt::Key_Return);
    QCOMPARE(tree.selectedId(), G("qt/button"));
  }

  void sidebar_keys_walk_across_sections_and_choose() {
    Sidebar sidebar;
    sidebar.setSections({{G("Library"), {{G("all"), G("All files"), G()}, {G("recent"), G("Recent"), G()}}},
                         {G("Places"), {{G("documents"), G("Documents"), G()}}}});
    Harness::show(&sidebar, QSize(200, sidebar.sizeHint().height()));
    QSignalSpy activated(&sidebar, &Sidebar::activated);
    sidebar.setFocus();
    QTest::keyClick(&sidebar, Qt::Key_Down);
    QCOMPARE(sidebar.cursorId(), G("all"));
    QTest::keyClick(&sidebar, Qt::Key_Down);
    QTest::keyClick(&sidebar, Qt::Key_Down);
    QTest::keyClick(&sidebar, Qt::Key_Down);
    QCOMPARE(sidebar.cursorId(), G("documents"));  // across the section, and stops at the end
    QTest::keyClick(&sidebar, Qt::Key_Space);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toString(), G("documents"));
    QTest::mouseClick(&sidebar, Qt::LeftButton, {}, sidebar.rowRect(G("recent")).center().toPoint());
    QCOMPARE(activated.last().at(0).toString(), G("recent"));
    sidebar.setCollapsed(true);
    QCOMPARE(sidebar.sizeHint().width(), sidebar.railWidth());
  }

  void calendar_keys_move_the_cursor_step_months_and_pick() {
    Calendar cal;
    cal.setMonth(2026, 11);
    cal.setToday(QDate(2026, 11, 8));
    cal.setSelected(QDate(2026, 11, 14));
    Harness::show(&cal, cal.sizeHint());
    QCOMPARE(cal.firstShown(), QDate(2026, 10, 26));  // weeks start on Monday
    QSignalSpy picked(&cal, &Calendar::picked);
    cal.setFocus();
    QTest::keyClick(&cal, Qt::Key_Right);
    QCOMPARE(cal.cursorDate(), QDate(2026, 11, 15));  // from the selected day
    QTest::keyClick(&cal, Qt::Key_Down);
    QTest::keyClick(&cal, Qt::Key_Down);
    QCOMPARE(cal.cursorDate(), QDate(2026, 11, 29));
    QTest::keyClick(&cal, Qt::Key_Down);
    QCOMPARE(cal.cursorDate(), QDate(2026, 12, 6));
    QCOMPARE(cal.month(), 12);  // the month follows the cursor
    QTest::keyClick(&cal, Qt::Key_Left);
    QTest::keyClick(&cal, Qt::Key_Up);
    QCOMPARE(cal.cursorDate(), QDate(2026, 11, 28));
    QCOMPARE(cal.month(), 11);
    QTest::keyClick(&cal, Qt::Key_Return);
    QCOMPARE(cal.selected(), QDate(2026, 11, 28));
    QCOMPARE(picked.count(), 1);
    QTest::keyClick(&cal, Qt::Key_PageDown);
    QTest::keyClick(&cal, Qt::Key_PageDown);
    QCOMPARE(cal.year(), 2027);
    QCOMPARE(cal.month(), 1);
    QTest::keyClick(&cal, Qt::Key_PageUp);
    QCOMPARE(cal.month(), 12);
    QTest::mouseClick(cal.previousButton(), Qt::LeftButton);
    QCOMPARE(cal.month(), 11);
    QCOMPARE(cal.previousButton()->toolTip(), G("Previous month"));
    QTest::mouseClick(&cal, Qt::LeftButton, {}, cal.dayRect(QDate(2026, 11, 3)).center());
    QCOMPARE(cal.selected(), QDate(2026, 11, 3));
    QCOMPARE(picked.count(), 2);
    cal.resize(400, cal.sizeHint().height());
    QVERIFY(cal.nextButton()->geometry().right() + 1 <= cal.sizeHint().width());
  }

  void split_view_drags_the_handle_within_the_pane_limits() {
    SplitView split;
    auto *side = new QWidget;
    auto *content = new QWidget;
    split.addPane(side, 200, 120, 300);
    split.addPane(content, -1, -1, -1, true);
    Harness::show(&split, QSize(720, 220));
    QCOMPARE(side->width(), 200);
    QCOMPARE(split.handleThickness(), shown().spacing("md"));
    QCOMPARE(content->x(), 200 + split.handleThickness());
    QSplitterHandle *handle = split.handle(1);
    QVERIFY(handle->cursor().shape() == Qt::SplitHCursor);
    // The handle follows the pointer (opaque resize): each step is aimed in the view's coordinates.
    auto drag = [&](int dx) {
      const QPoint grab = handle->mapTo(&split, QPoint(handle->width() / 2, 100));
      auto at = [&](int d) { return handle->mapFrom(&split, grab + QPoint(d, 0)); };
      QTest::mousePress(handle, Qt::LeftButton, {}, at(0));
      QTest::mouseMove(handle, at(dx / 2));
      QTest::mouseMove(handle, at(dx));
      QTest::mouseRelease(handle, Qt::LeftButton, {}, at(dx));
    };
    drag(40);
    QCOMPARE(side->width(), 240);
    drag(-200);
    QCOMPARE(side->width(), 120);  // clamped to the minimum
    drag(400);
    QCOMPARE(side->width(), 300);  // and the maximum
    // A hidden pane takes its handle with it.
    side->hide();
    QTest::qWait(10);
    QVERIFY(handle->isHidden());
    QCOMPARE(content->width(), 720);
  }
};

OMARCHY_TEST(Data)

#include "tst_data.moc"
