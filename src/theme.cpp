#include "theme.h"

#include <QDir>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QRegularExpression>
#include <cmath>

Theme::Theme(QObject *parent)
    : QObject(parent), m_directory(QDir::homePath() + "/.local/state/omarchy/current") {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        m_hyprctl = QStandardPaths::findExecutable("hyprctl");
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const auto response = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object();
        const int radius = response.value("int").toInt(-1);
        if (code == 0 && status == QProcess::NormalExit && radius >= 0 && radius != m_radius) {
            m_radius = radius;
            emit changed();
        }
    });
    if (!m_hyprctl.isEmpty()) {
        m_roundingPoll.setInterval(1200);
        m_roundingPoll.start();
        refreshRounding();
    }
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

QString Theme::accentForeground() const {
    const QColor color(m_accent);
    auto linear = [](double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
    const double luminance = 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
    return luminance > 0.179 ? "black" : "white";
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
}

void Theme::reload() {
    const auto oldPaths = m_watcher.files() + m_watcher.directories();
    if (!oldPaths.isEmpty())
        m_watcher.removePaths(oldPaths);

    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    for (const QString &path : {ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                                m_directory + "/theme", m_directory + "/theme/colors.toml"}) {
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            m_watcher.addPath(path);
    }

    QString accent = "#FFD60A";
    QFile file(m_directory + "/theme/colors.toml");
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(R"re(^\s*accent\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            accent = match.captured(1);
    }
    if (accent != m_accent) {
        m_accent = accent;
        emit changed();
    }
}
