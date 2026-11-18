// The Qt binding's behavior tests: Qt Quick Test over every tst_*.qml in this directory.
//
//   build/qt-tests/omarchy-qt-tests                    every case
//   build/qt-tests/omarchy-qt-tests -functions         list them
//   build/qt-tests/omarchy-qt-tests SearchList::test_escape_clears_then_closes
//
// Offscreen, under the Basic style, in the tokyo-night fixture at radius 0 unless the
// environment says otherwise, so a run doesn't depend on the desktop.
//
// The live-switch test (spec/following.md) is qt/tests-live/tst_follow.qml (outside this
// directory, so a plain run doesn't pick it up), run in a desktop of its own:
//
//   tools/live/run build/qt-tests/omarchy-qt-tests -input qt/tests-live
//   OMARCHY_UI_REDUCE_MOTION=1 tools/live/run build/qt-tests/omarchy-qt-tests -input qt/tests-live
//
// Under tools/live/run nothing is pinned, and `LiveSwitch.to(name)` switches the theme with
// tools/live/theme-set, as `omarchy theme set` would.

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QSaveFile>
#include <QtQml>
#include <QtQuickTest>

#include "omarchyui.h"

// `import Omarchy.Test`: LiveSwitch, for qt/tests-live/tst_follow.qml.
class LiveSwitch : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available CONSTANT)

public:
  bool available() const { return !qEnvironmentVariableIsEmpty("OMARCHY_UI_LIVE_HOME"); }

  // `omarchy theme set <name>` under the test HOME; true when it ran.
  Q_INVOKABLE bool to(const QString &name) {
    if (!available()) return false;
    return QProcess::execute(qEnvironmentVariable("OMARCHY_UI_LIVE_SWITCH"),
                             {qEnvironmentVariable("OMARCHY_UI_LIVE_HOME"), name}) == 0;
  }

  Q_INVOKABLE bool writeColors(const QString &colors) {
    if (!available()) return false;
    QSaveFile file(qEnvironmentVariable("OMARCHY_UI_LIVE_HOME")
                   + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml"));
    const QByteArray bytes = colors.toUtf8();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
  }
};

class Setup : public QObject {
  Q_OBJECT

public:
  Setup() {
    // Before the application exists: the platform and style are read when it starts.
    auto preset = [](const char *name, const QByteArray &value) {
      if (!qEnvironmentVariableIsSet(name)) qputenv(name, value);
    };
    preset("QT_QPA_PLATFORM", "offscreen");
    preset("QT_QUICK_CONTROLS_STYLE", "Basic");
    // A live run follows its own HOME instead of a pinned fixture.
    if (qEnvironmentVariableIsEmpty("OMARCHY_UI_LIVE_HOME"))
      preset("OMARCHY_UI_THEME_DIR", QUICK_TEST_SOURCE_DIR "/../../fixtures/themes/tokyo-night");
    preset("OMARCHY_UI_ROUNDING", "0");
    preset("QT_FORCE_STDERR_LOGGING", "1");
  }

public slots:
  void applicationAvailable() {
    OmarchyUi::registerTypes();
    qmlRegisterSingletonType<LiveSwitch>("Omarchy.Test", 1, 0, "LiveSwitch",
                                         [](QQmlEngine *, QJSEngine *) -> QObject * { return new LiveSwitch(); });
  }
};

QUICK_TEST_MAIN_WITH_SETUP(omarchy_ui, Setup)

#include "main.moc"
