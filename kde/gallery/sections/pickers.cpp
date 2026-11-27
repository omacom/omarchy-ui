#include <QTimer>

#include "gallery.h"

using namespace Omarchy;

namespace {

void build(Column *body, QWidget *parent) {
  // A closed MultiSelect, and one held open in a 280-tall box its popup draws in.
  Row *top = Gallery::row(body, parent, Gallery::sp("xxl"));
  auto *modules = new MultiSelect(QStringLiteral("Bar modules"));
  modules->setOptions(QStringList{QStringLiteral("Clock"), QStringLiteral("Battery"), QStringLiteral("Network"), QStringLiteral("Volume"),
                                  QStringLiteral("Bluetooth")});
  modules->setValues({QStringLiteral("Clock"), QStringLiteral("Battery"), QStringLiteral("Network"), QStringLiteral("Volume")});
  top->addWidget(modules, Qt::AlignTop);
  auto *box = new QWidget;
  auto *notify = new MultiSelect(QStringLiteral("Notify for"), box);
  notify->setOptions(QStringList{QStringLiteral("Calendar"), QStringLiteral("Mail"), QStringLiteral("Messages"), QStringLiteral("Updates"),
                                 QStringLiteral("Battery")});
  notify->setValues({QStringLiteral("Mail"), QStringLiteral("Updates")});
  box->setFixedSize(notify->sizeHint().width(), 280);
  notify->setGeometry(0, 0, box->width(), notify->sizeHint().height());
  top->addWidget(box, Qt::AlignTop);
  // Once the window has its size, which the popup clamps to.
  QTimer::singleShot(100, notify, [notify] { notify->open(); });

  // Color wells: accent, success with the cursor, urgent.
  Row *wells = Gallery::row(body, parent, Gallery::sp("controlGap"));
  auto *accent = new ColorWell;
  accent->setSwatch([](const Tokens &tk) { return tk.accent(); });
  wells->addWidget(accent);
  auto *success = new ColorWell;
  success->setSwatch([](const Tokens &tk) { return tk.success(); });
  success->setHasCursor(true);
  wells->addWidget(success);
  auto *urgent = new ColorWell;
  urgent->setSwatch([](const Tokens &tk) { return tk.urgent(); });
  wells->addWidget(urgent);

  auto *strip = new SwatchStrip;
  strip->setColors([](const Tokens &tk) {
    return QList<QColor>{tk.accent(), tk.urgent(), tk.warning(), tk.success(), tk.foreground(), tk.textDim(), tk.textFaint(), tk.muted()};
  });
  strip->setCurrentIndex(2);
  body->addWidget(strip);

  // A Dialog held open over the card it would cover, so the scrim shows.
  auto *stage = new QWidget;
  stage->setFixedSize(420, 240);
  auto *card = new Card(QStringLiteral("Displays"), stage);
  card->setGeometry(stage->rect());
  auto *rows = new QWidget;
  setFill(rows);
  auto *rc = new Column(rows);
  auto *builtIn = new MenuRow(QStringLiteral("Built-in display"));
  builtIn->setChecked(true);
  rc->addWidget(builtIn);
  rc->addWidget(new MenuRow(QStringLiteral("External monitor")));
  rc->addWidget(new MenuRow(QStringLiteral("Projector")));
  card->addWidget(rows);
  auto *dialog = new Dialog(QStringLiteral("Discard changes?"), QStringLiteral("Your edits to Tokyo Night will be lost."), stage);
  dialog->setSecondaryText(QStringLiteral("Keep editing"));
  dialog->setPrimaryText(QStringLiteral("Discard"));
  dialog->setDestructive(true);
  dialog->setGeometry(stage->rect());
  dialog->setOpened(true);
  body->addWidget(stage);
}

Gallery::Register reg("pickers", "Pickers", build);

}  // namespace
