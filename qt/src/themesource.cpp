#include "themesource.h"

#include "omarchyui.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

namespace {

QString home() { return QDir::homePath(); }
QString currentDir() { return home() + QStringLiteral("/.local/state/omarchy/current"); }
QString userShellPath() { return home() + QStringLiteral("/.config/omarchy/shell.toml"); }
QString languagePath() { return home() + QStringLiteral("/.config/omarchy/language"); }

QString readText(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return QString();
  return QString::fromUtf8(f.readAll());
}

bool envNumber(const char *name, double *out) {
  const QByteArray raw = qgetenv(name).trimmed();
  if (raw.isEmpty()) return false;
  bool ok = false;
  const double n = raw.toDouble(&ok);
  if (ok) *out = n;
  return ok;
}

}  // namespace

ThemeSource::ThemeSource(QObject *parent) : QObject(parent) {
  const QString pinnedDir = qEnvironmentVariable("OMARCHY_UI_THEME_DIR");
  m_pinned = !pinnedDir.isEmpty();
  m_themeDir = m_pinned ? QFileInfo(pinnedDir).absoluteFilePath() : currentDir() + QStringLiteral("/theme");

  const QString family = qEnvironmentVariable("OMARCHY_UI_FONT_FAMILY").trimmed();
  if (!family.isEmpty()) m_fontFamily = family;

  // A theme switch is a burst: the directory swap, theme.name, then Hyprland reloading. One
  // re-read after it settles keeps every binding from re-evaluating three times.
  m_settle.setSingleShot(true);
  m_settle.setInterval(150);
  connect(&m_settle, &QTimer::timeout, this, &ThemeSource::reload);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));

  // Hyprland announces `configreloaded` once a theme's or the user's config is in effect, which
  // is when rounding, gaps and the animations toggle can be read back. A restarted Hyprland (or
  // one not up yet) is retried every two seconds.
  m_reconnect.setSingleShot(true);
  m_reconnect.setInterval(2000);
  connect(&m_reconnect, &QTimer::timeout, this, &ThemeSource::connectEvents);
  connect(&m_events, &QLocalSocket::disconnected, &m_reconnect, qOverload<>(&QTimer::start));
  connect(&m_events, &QLocalSocket::errorOccurred, &m_reconnect, qOverload<>(&QTimer::start));
  connect(&m_events, &QLocalSocket::readyRead, this, [this] {
    m_eventBuffer += m_events.readAll();
    qsizetype nl;
    while ((nl = m_eventBuffer.indexOf('\n')) >= 0) {
      if (m_eventBuffer.left(nl).startsWith("configreloaded>>")) m_settle.start();
      m_eventBuffer.remove(0, nl + 1);
    }
  });

  // The instance outlives the event loop; stop reconnecting and watching when the app quits.
  if (QCoreApplication::instance()) {
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
      m_events.disconnect(this);
      m_events.disconnect(&m_reconnect);
      m_reconnect.stop();
      m_settle.stop();
      m_events.abort();
    });
  }

  armWatcher();
  readFiles();
  readHyprland();
  readFont();
  m_language = readText(languagePath()).trimmed();
  if (!m_pinned) connectEvents();
}

ThemeSource *ThemeSource::instance() {
  static ThemeSource *source = new ThemeSource();
  return source;
}

ThemeSource *ThemeSource::create(QQmlEngine *, QJSEngine *) { return instance(); }

void ThemeSource::reload() {
  armWatcher();
  bool any = false;
  const QString colors = m_colors, shell = m_shell, user = m_userShell, name = m_themeName, icons = m_iconTheme;
  readFiles();
  if (colors != m_colors || shell != m_shell || user != m_userShell || name != m_themeName || icons != m_iconTheme) {
    any = true;
    emit themeChanged();
  }

  const int rounding = m_rounding, border = m_borderSize;
  const double gaps = m_gapsOut;
  const bool animations = m_animations;
  readHyprland();
  if (rounding != m_rounding || gaps != m_gapsOut || animations != m_animations || border != m_borderSize) {
    any = true;
    emit hyprlandChanged();
  }

  const QString language = m_language;
  m_language = readText(languagePath()).trimmed();
  if (language != m_language) {
    any = true;
    emit languageChanged();
  }

  const QString font = m_resolvedFontFamily;
  readFont();
  if (font != m_resolvedFontFamily) {
    any = true;
    emit fontChanged();
  }

  if (any) emit settled();
}

void ThemeSource::publishTokens(const QJsonObject &tokens, int revision, bool change) {
  OmarchyUi::Theme::instance()->publish(tokens, revision, change);
}

void ThemeSource::readFiles() {
  m_colors = readText(m_themeDir + QStringLiteral("/colors.toml"));
  m_shell = readText(m_themeDir + QStringLiteral("/shell.toml"));
  // A pinned theme renders the same on every machine, so the user's override stays out of it.
  m_userShell = m_pinned ? QString() : readText(userShellPath());
  m_themeName = m_pinned ? QFileInfo(m_themeDir).fileName()
                         : readText(currentDir() + QStringLiteral("/theme.name")).trimmed();

  // The icon theme, as omarchy-theme-set-gnome applies it: icons.theme's line, else Yaru-blue.
  m_iconTheme = qEnvironmentVariable("OMARCHY_UI_ICON_THEME").trimmed();
  if (m_iconTheme.isEmpty()) {
    const QStringList lines = readText(m_themeDir + QStringLiteral("/icons.theme")).split(QLatin1Char('\n'));
    for (const QString &line : lines)
      if (!line.trimmed().isEmpty()) { m_iconTheme = line.trimmed(); break; }
  }
  if (m_iconTheme.isEmpty()) m_iconTheme = QStringLiteral("Yaru-blue");
}

void ThemeSource::armWatcher() {
  // Files replaced by rename drop off a watcher, so re-add after every change.
  QStringList roots{m_themeDir};
  if (!m_pinned)
    roots << currentDir() << QFileInfo(userShellPath()).absolutePath()
          << home() + QStringLiteral("/.config/fontconfig");

  // A fresh home may have none of these directories yet. Follow the nearest
  // existing parent until its intended watch target appears.
  QStringList wanted;
  for (const QString &root : roots) {
    QString path = root;
    while (!QFileInfo(path).isDir()) {
      const QString parent = QFileInfo(path).absolutePath();
      if (parent == path) break;
      path = parent;
    }
    if (QFileInfo(path).isDir()) wanted << path;
  }
  wanted.removeDuplicates();

  // A switch can replace current/theme at the same path. Renew that watch
  // even if QFileSystemWatcher still reports it.
  for (const QString &path : m_watcher.directories()) {
    if (!wanted.contains(path) || (!m_pinned && path == m_themeDir))
      m_watcher.removePath(path);
  }
  for (const QString &path : wanted)
    if (!m_watcher.directories().contains(path)) m_watcher.addPath(path);
  // A directory watch sees files appear, vanish and be renamed over, but not a file rewritten in
  // place (an editor that saves without a rename, `>` in a shell), so the files read are watched
  // as files too. A save by rename leaves the watch on the old file, so every re-read re-arms
  // them; one that doesn't exist yet is picked up by its directory's watch once it appears.
  QStringList files{m_themeDir + QStringLiteral("/colors.toml"), m_themeDir + QStringLiteral("/shell.toml"),
                    m_themeDir + QStringLiteral("/icons.theme")};
  if (!m_pinned)
    files << userShellPath() << languagePath() << home() + QStringLiteral("/.config/fontconfig/fonts.conf");
  for (const QString &f : files) {
    m_watcher.removePath(f);
    if (QFileInfo::exists(f)) m_watcher.addPath(f);
  }
}

QString ThemeSource::hyprDir() const {
  const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
  if (runtime.isEmpty()) return QString();
  const QString base = runtime + QStringLiteral("/hypr");
  const QString sig = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
  if (!sig.isEmpty() && QFileInfo::exists(base + "/" + sig + "/.socket.sock")) return base + "/" + sig;
  // Launched outside the session environment: take the most recently started instance.
  QDir dir(base);
  const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
  for (const QFileInfo &e : entries)
    if (QFileInfo::exists(e.absoluteFilePath() + "/.socket.sock")) return e.absoluteFilePath();
  return QString();
}

QByteArray ThemeSource::hyprRequest(const QString &request) const {
  const QString dir = hyprDir();
  if (dir.isEmpty()) return QByteArray();
  QLocalSocket s;
  s.connectToServer(dir + QStringLiteral("/.socket.sock"));
  if (!s.waitForConnected(300)) return QByteArray();
  s.write(request.toUtf8());
  s.waitForBytesWritten(300);
  QByteArray out;
  while (s.waitForReadyRead(300)) out += s.readAll();
  return out;
}

void ThemeSource::readHyprland() {
  if (!m_pinned) {
    auto option = [this](const char *name) {
      return QJsonDocument::fromJson(hyprRequest(QStringLiteral("j/getoption ") + QLatin1String(name))).object();
    };
    const QJsonObject rounding = option("decoration:rounding");
    if (rounding.contains("int")) m_rounding = rounding.value("int").toInt();

    const QJsonObject gaps = option("general:gaps_out");
    const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("-?\\d+(?:\\.\\d+)?"))
                                          .match(gaps.value("css").toString());
    if (m.hasMatch()) m_gapsOut = m.captured(0).toDouble();
    else if (gaps.contains("int")) m_gapsOut = gaps.value("int").toDouble();

    const QJsonObject animations = option("animations:enabled");
    if (animations.contains("bool")) m_animations = animations.value("bool").toBool();

    const QJsonObject border = option("general:border_size");
    if (border.contains("int")) m_borderSize = border.value("int").toInt();
  }

  double n;
  if (envNumber("OMARCHY_UI_ROUNDING", &n)) m_rounding = qRound(n);
  if (envNumber("OMARCHY_UI_GAPS_OUT", &n)) m_gapsOut = n;
  const QByteArray reduce = qgetenv("OMARCHY_UI_REDUCE_MOTION").trimmed();
  if (!reduce.isEmpty()) m_animations = reduce != "1";
}

void ThemeSource::readFont() {
  QProcess fc;
  fc.start(QStringLiteral("fc-match"), {QStringLiteral("-f"), QStringLiteral("%{family[0]}"), m_fontFamily});
  if (fc.waitForFinished(1000) && fc.exitStatus() == QProcess::NormalExit) {
    const QString name = QString::fromUtf8(fc.readAllStandardOutput()).trimmed();
    if (!name.isEmpty()) m_resolvedFontFamily = name;
  }
}

void ThemeSource::connectEvents() {
  const QString dir = hyprDir();
  if (dir.isEmpty()) {
    m_reconnect.start();
    return;
  }
  m_events.abort();
  m_eventBuffer.clear();
  m_events.connectToServer(dir + QStringLiteral("/.socket2.sock"));
}
